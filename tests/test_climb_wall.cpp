#include "kke/ClimbWall.h"
#include "kke/Climber.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace {

using kke::ClimbHold;
using kke::ClimbWall;
using kke::ClimbWallDesc;
using kke::Climber;

constexpr float kDt = 1.0f / 60.0f;

ClimbWall wall(uint32_t seed) {
    ClimbWallDesc d;
    d.seed = seed;
    return ClimbWall::generate(d);
}

// ----------------------------------------------------------------- wall

TEST(ClimbWall, SameSeedSameMountain) {
    const ClimbWall a = wall(7), b = wall(7), c = wall(8);
    ASSERT_EQ(a.holds().size(), b.holds().size());
    for (size_t i = 0; i < a.holds().size(); ++i) {
        EXPECT_EQ(a.holds()[i].position, b.holds()[i].position);
        EXPECT_EQ(a.holds()[i].kind, b.holds()[i].kind);
    }
    EXPECT_EQ(a.surfaceZ(1.3f, 12.7f), b.surfaceZ(1.3f, 12.7f));
    EXPECT_NE(a.surfaceZ(1.3f, 12.7f), c.surfaceZ(1.3f, 12.7f));
}

TEST(ClimbWall, EveryMountainCanBeClimbed) {
    for (uint32_t seed = 1; seed <= 25; ++seed) {
        const ClimbWall w = wall(seed);
        const std::vector<int> r = w.route(w.desc().routeStep + 0.01f);
        ASSERT_FALSE(r.empty()) << "seed " << seed;
        EXPECT_LE(w.holds()[static_cast<size_t>(r.front())].position.y, 2.2f);
        const ClimbHold& top = w.holds()[static_cast<size_t>(r.back())];
        EXPECT_EQ(top.kind, ClimbHold::Kind::Edge);
        EXPECT_EQ(top.ledge, -1);
        EXPECT_FLOAT_EQ(top.position.y, w.summitY());
        for (int h : r) EXPECT_FALSE(w.holds()[static_cast<size_t>(h)].loose);
        // The drawn line: steps within reach, every ledge on it, ends on the summit.
        const std::vector<int>& line = w.line();
        ASSERT_GE(line.size(), 10u);
        std::vector<bool> ledgeOn(w.ledges().size(), false);
        for (size_t i = 0; i < line.size(); ++i) {
            const ClimbHold& h = w.holds()[static_cast<size_t>(line[i])];
            if (h.kind == ClimbHold::Kind::Edge && h.ledge >= 0) ledgeOn[static_cast<size_t>(h.ledge)] = true;
            if (i > 0) {
                EXPECT_LE(ClimbWall::reachDistance(h.position, w.holds()[static_cast<size_t>(line[i - 1])].position), 1.55f) << "seed " << seed << " step " << i;
            }
        }
        for (size_t l = 0; l < ledgeOn.size(); ++l) EXPECT_TRUE(ledgeOn[l]) << "seed " << seed << " misses ledge " << l;
        EXPECT_EQ(w.holds()[static_cast<size_t>(line.back())].ledge, -1);
    }
}

TEST(ClimbWall, HasBandsOfSlabVerticalAndOverhang) {
    for (uint32_t seed = 1; seed <= 10; ++seed) {
        const ClimbWall w = wall(seed);
        float most = -90.0f;
        for (float y = 0.0f; y < w.summitY(); y += 0.5f) most = std::max(most, w.leanAt(y));
        EXPECT_GT(most, 8.0f) << "seed " << seed << ": no overhang (the crux)";
        EXPECT_LE(w.leanAt(0.5f), 1.0f) << "the start should not overhang";
        // An overhang's rock at the top of the band is further out than at its foot.
    }
}

TEST(ClimbWall, HoldsSitOnTheRockAndApart) {
    const ClimbWall w = wall(3);
    const auto& holds = w.holds();
    EXPECT_GT(holds.size(), 300u);
    int loose = 0;
    for (size_t i = 0; i < holds.size(); ++i) {
        const ClimbHold& h = holds[i];
        loose += h.loose ? 1 : 0;
        if (h.kind == ClimbHold::Kind::Edge) continue;
        const float rock = w.surfaceZ(h.position.x, h.position.y);
        EXPECT_GT(h.position.z, rock - 0.02f);
        EXPECT_LT(h.position.z, rock + 0.2f);
        EXPECT_GE(h.position.x, -w.desc().width * 0.5f + w.desc().margin - 0.05f);
        EXPECT_LE(h.position.x, w.desc().width * 0.5f - w.desc().margin + 0.05f);
        if (h.route) continue;
        for (size_t j = 0; j < i; ++j) {
            if (holds[j].kind == ClimbHold::Kind::Edge) continue;
            const float d = glm::length(glm::vec2(holds[j].position) - glm::vec2(h.position));
            EXPECT_GE(d, w.desc().spacing * 0.99f);
        }
    }
    EXPECT_GT(loose, 3);
}

TEST(ClimbWall, LedgesStickOutAndKeepTheirBandClear) {
    const ClimbWall w = wall(5);
    ASSERT_EQ(w.ledges().size(), 3u);
    for (size_t i = 0; i < w.ledges().size(); ++i) {
        const kke::ClimbLedge& l = w.ledges()[i];
        // The front stands clear of the rock, the back is inside it.
        const float rock = w.surfaceZ(l.center.x, l.top());
        EXPECT_GT(l.center.z + l.halfExtents.z, rock + 0.8f);
        EXPECT_LT(l.center.z - l.halfExtents.z, rock);
        int edges = 0;
        for (const ClimbHold& h : w.holds()) {
            if (h.kind == ClimbHold::Kind::Edge && h.ledge == static_cast<int>(i)) ++edges;
            if (h.kind == ClimbHold::Kind::Edge) continue;
            const bool inside = std::abs(h.position.x - l.center.x) < l.halfExtents.x && std::abs(h.position.y - l.top()) < 0.18f;
            EXPECT_FALSE(inside) << "a hold inside ledge " << i;
        }
        EXPECT_GE(edges, 5);
    }
}

TEST(ClimbWall, MeshIsCleanAndFacesTheClimber) {
    const ClimbWall w = wall(2);
    const kke::ClimbMesh m = w.buildMesh();
    ASSERT_EQ(m.indices.size() % 3, 0u);
    ASSERT_EQ(m.positions.size(), m.normals.size());
    ASSERT_EQ(m.positions.size(), m.colors.size());
    EXPECT_GT(m.indices.size(), 3u * 10000u);
    int toward = 0, faces = 0;
    for (size_t t = 0; t < m.indices.size(); t += 3) {
        const glm::vec3 a = m.positions[m.indices[t]], b = m.positions[m.indices[t + 1]], c = m.positions[m.indices[t + 2]];
        const glm::vec3 n = m.normals[m.indices[t]];
        ASSERT_NEAR(glm::length(n), 1.0f, 1e-3f);
        // Counter-clockwise seen from the side the normal points to.
        EXPECT_GT(glm::dot(glm::cross(b - a, c - a), n), 0.0f);
        if (a.y > 0.5f && a.y < w.summitY() - 0.5f && std::abs(a.x) < 5.0f) {
            ++faces;
            toward += n.z > 0.0f ? 1 : 0;
        }
    }
    EXPECT_GT(toward, faces * 9 / 10);
}

// -------------------------------------------------------------- climber

struct Rig {
    ClimbWall w = wall(4);
    Climber c{ w };
    // Feet on the ground in front of the first route hold.
    glm::vec3 base() const {
        const std::vector<int> r = w.route(w.desc().routeStep + 0.01f);
        const glm::vec3 p = w.holds()[static_cast<size_t>(r.front())].position;
        return glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f);
    }
    // Onto the rock with both hands (the second one reaches up if the
    // start only found one hold).
    bool mount() {
        if (!c.start(base())) return false;
        run({}, 0.5f);
        for (int h = 0; h < 2; ++h)
            if (c.handHold(h) < 0) {
                Climber::Input in;
                in.aim = glm::vec2(0.0f, 1.0f);
                in.reach[h] = true;
                run(in, 0.7f);
            }
        return c.handHold(0) >= 0 && c.handHold(1) >= 0;
    }
    void run(Climber::Input in, float seconds) {
        for (int i = 0; i < static_cast<int>(std::lround(seconds / kDt)); ++i) {
            c.update(in, kDt);
            in.reach[0] = in.reach[1] = false;
        }
    }
};

TEST(Climber, GrabsTheRockFromTheGround) {
    Rig r;
    EXPECT_FALSE(r.c.start(glm::vec3(0.0f, 0.0f, 30.0f))) << "nothing to hold far from the rock";
    ASSERT_TRUE(r.c.start(r.base()));
    r.run({}, 0.5f);
    EXPECT_EQ(r.c.state(), Climber::State::Climbing);
    EXPECT_TRUE(r.c.handHold(0) >= 0 || r.c.handHold(1) >= 0);
    // The body hangs below the hands and off the rock.
    const glm::vec3 hips = r.c.hips();
    EXPECT_GT(hips.z, r.w.surfaceZ(hips.x, hips.y) + 0.2f);
}

TEST(Climber, PreciseReachGoesWhereItAimsAndCostsLittle) {
    Rig r;
    ASSERT_TRUE(r.c.start(r.base()));
    r.run({}, 0.5f);
    const int hand = r.c.handHold(Climber::kLeft) >= 0 && r.c.handHold(Climber::kRight) >= 0
                         ? (r.w.holds()[static_cast<size_t>(r.c.handHold(0))].position.y < r.w.holds()[static_cast<size_t>(r.c.handHold(1))].position.y ? 0 : 1)
                         : (r.c.handHold(0) >= 0 ? 1 : 0);
    Climber::Input aim;
    aim.aim = glm::vec2(0.0f, 1.0f);
    r.c.update(aim, kDt);
    const int target = r.c.aimTarget(hand);
    ASSERT_GE(target, 0);
    const float before = r.c.stamina();
    Climber::Input go = aim;
    go.reach[hand] = true;
    r.c.update(go, kDt);
    EXPECT_EQ(r.c.handMove(hand), Climber::Move::Precise);
    EXPECT_NEAR(before - r.c.stamina(), r.c.settings().costPrecise, 0.3f);
    r.run(aim, r.c.settings().reachTime + 0.05f);
    EXPECT_EQ(r.c.handHold(hand), target);
    EXPECT_TRUE(glm::length(r.c.hand(hand) - r.w.holds()[static_cast<size_t>(target)].position) < 1e-3f);
}

TEST(Climber, LungeReachesFurtherThanPreciseAndCostsMore) {
    Rig r;
    ASSERT_TRUE(r.mount());
    // A hold beyond precise reach of the right hand's hold, within a full lunge.
    const glm::vec3 pivot = r.w.holds()[static_cast<size_t>(r.c.handHold(1))].position;
    int far = -1;
    for (size_t i = 0; i < r.w.holds().size(); ++i) {
        const ClimbHold& h = r.w.holds()[i];
        const float d = glm::length(h.position - pivot);
        if (!h.loose && h.kind != ClimbHold::Kind::Edge && kke::ClimbWall::reachDistance(h.position, pivot) > r.c.settings().span + 0.2f && d < r.c.settings().lungeSpan - 0.1f &&
            h.position.y > pivot.y + 0.8f && std::abs(h.position.x - pivot.x) < 0.6f) {
            far = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(far, 0);
    Climber::Input in;
    in.pick[0] = far;
    in.reach[0] = true; // a bumper can't get there
    r.c.update(in, kDt);
    EXPECT_FALSE(r.c.handMoving(0));
    // Trigger held to a full charge, then let go.
    in.reach[0] = false;
    in.power[0] = 1.0f;
    r.run(in, r.c.settings().chargeTime + 0.1f);
    EXPECT_NEAR(r.c.charge(0), 1.0f, 1e-3f);
    EXPECT_EQ(r.c.aimTarget(0), far);
    const float before = r.c.stamina();
    in.power[0] = 0.0f;
    r.c.update(in, kDt);
    EXPECT_EQ(r.c.handMove(0), Climber::Move::Lunge);
    EXPECT_NEAR(before - r.c.stamina(), r.c.settings().costLungeMax, 0.5f);
    r.run(in, r.c.settings().lungeTime + 0.05f);
    EXPECT_EQ(r.c.handHold(0), far);
}

TEST(Climber, QuickSnatchIsFasterThanAPreciseReach) {
    Rig r;
    ASSERT_TRUE(r.mount());
    Climber::Input in;
    in.aim = glm::vec2(0.0f, 1.0f);
    in.power[1] = 0.7f;
    r.c.update(in, kDt);
    in.reach[1] = true;
    r.c.update(in, kDt);
    EXPECT_EQ(r.c.handMove(1), Climber::Move::Quick);
    in.reach[1] = false;
    in.power[1] = 0.0f;
    r.run(in, r.c.settings().quickTime + 0.03f);
    EXPECT_FALSE(r.c.handMoving(1));
    EXPECT_GE(r.c.handHold(1), 0);
}

TEST(Climber, LooseHoldsBreakUnderALunge) {
    Rig r;
    ASSERT_TRUE(r.mount());
    const glm::vec3 pivot = r.w.holds()[static_cast<size_t>(r.c.handHold(1))].position;
    // Any loose hold within a lunge of the right hand (search the whole wall's lower part).
    int loose = -1;
    for (size_t i = 0; i < r.w.holds().size(); ++i)
        if (r.w.holds()[i].loose && kke::ClimbWall::reachDistance(r.w.holds()[i].position, pivot) < r.c.settings().lungeSpan - 0.05f) loose = static_cast<int>(i);
    if (loose < 0) GTEST_SKIP() << "no loose hold near the start on this seed";
    Climber::Input in;
    in.pick[0] = loose;
    in.power[0] = 1.0f;
    r.run(in, r.c.settings().chargeTime + 0.1f);
    in.power[0] = 0.0f;
    bool broke = false;
    for (int i = 0; i < 40; ++i) {
        r.c.update(in, kDt);
        broke = broke || r.c.brokeHold() == loose;
    }
    EXPECT_TRUE(broke);
    EXPECT_TRUE(r.c.holdGone(loose));
    EXPECT_EQ(r.c.handHold(0), -1);
    EXPECT_EQ(r.c.state(), Climber::State::Climbing) << "the other hand still holds";
}

TEST(Climber, OneHandTiresFasterAndEmptyArmsLetGo) {
    Rig r;
    ASSERT_TRUE(r.mount());
    const float two = r.c.drainRate();
    // While the left hand reaches, the right one holds the body alone.
    Climber::Input in;
    in.aim = glm::vec2(0.0f, 1.0f);
    in.reach[0] = true;
    r.c.update(in, kDt);
    ASSERT_TRUE(r.c.handMoving(0));
    in.reach[0] = false;
    r.c.update(in, kDt);
    EXPECT_GT(r.c.drainRate(), two * 1.5f);
    r.run({}, 1.0f);
    // Hang until the arms give out.
    for (int i = 0; i < 60 * 120 && r.c.state() == Climber::State::Climbing; ++i) r.c.update({}, kDt);
    EXPECT_EQ(r.c.state(), Climber::State::Fell);
    EXPECT_EQ(r.c.stamina(), 0.0f);
    r.c.recover(50.0f, 1.0f);
    EXPECT_NEAR(r.c.stamina(), 50.0f, 1e-3f);
}

TEST(Climber, LetGoFalls) {
    Rig r;
    ASSERT_TRUE(r.c.start(r.base()));
    r.run({}, 0.5f);
    Climber::Input in;
    in.letGo = true;
    r.c.update(in, kDt);
    EXPECT_EQ(r.c.state(), Climber::State::Fell);
    EXPECT_TRUE(r.c.fell());
}

TEST(Climber, TheBotClimbsToTheSummitAndMantles) {
    for (uint32_t seed : { 4u, 9u, 21u }) {
        ClimbWall w = wall(seed);
        Climber::Settings s;
        s.maxStamina = 1e6f; // this checks the route and the moves, not the rests
        Climber c(w, s);
        kke::ClimbBot bot(w.line());
        const glm::vec3 p = w.holds()[static_cast<size_t>(bot.route().front())].position;
        ASSERT_TRUE(c.start(glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f))) << "seed " << seed;
        float t = 0.0f;
        for (; t < 240.0f && c.state() != Climber::State::Topped && c.state() != Climber::State::Fell; t += kDt) c.update(bot.think(c, kDt), kDt);
        EXPECT_EQ(c.state(), Climber::State::Topped) << "seed " << seed << " stuck at y " << c.hips().y << " after " << t << " s";
        EXPECT_EQ(c.mantleLedge(), -1);
        EXPECT_NEAR(c.feet().y, w.summitY(), 0.05f);
        EXPECT_LT(c.feet().z, w.summitZ());
    }
}

TEST(Climber, StaminaMattersOnTheWayUp) {
    // Every mountain, with real stamina: the bot has to pace itself (shake out on jugs,
    // stand on ledges); it still gets up, and it gets tired doing it.
    for (uint32_t seed = 1; seed <= 12; ++seed) {
        ClimbWall w = wall(seed);
        Climber c(w);
        kke::ClimbBot bot(w.line());
        const glm::vec3 p = w.holds()[static_cast<size_t>(bot.route().front())].position;
        ASSERT_TRUE(c.start(glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f)));
        float lowest = 1.0f, t = 0.0f;
        for (; t < 400.0f; t += kDt) {
            ASSERT_NE(c.state(), Climber::State::Fell) << "seed " << seed << " fell at y " << c.hips().y;
            c.update(bot.think(c, kDt), kDt);
            lowest = std::min(lowest, c.staminaFraction());
            if (c.state() != Climber::State::Topped) continue;
            if (c.mantleLedge() < 0) break;
            // Onto a ledge: stand, rest, go again (the game does this
            // with Locomotion in between).
            const glm::vec3 stand = c.mantleFeet();
            c.recover(30.0f, 3.0f);
            ASSERT_TRUE(c.start(stand)) << "seed " << seed << ": nothing to grab above ledge " << c.mantleLedge();
        }
        EXPECT_EQ(c.state(), Climber::State::Topped) << "seed " << seed;
        EXPECT_EQ(c.mantleLedge(), -1);
        EXPECT_LT(lowest, 0.7f) << "seed " << seed << ": the climb should tire you";
        EXPECT_GT(t, 15.0f) << "seed " << seed << ": 36 m should take a while";
    }
}

} // namespace
