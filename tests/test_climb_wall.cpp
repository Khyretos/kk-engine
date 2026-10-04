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

// Over the top of a ledge (a mantle onto it): stand up and climb on from
// there, as the game does. False once on the summit.
bool onAgain(Climber& c) {
    if (c.state() != Climber::State::Topped) return true;
    if (c.mantleLedge() < 0) return false;
    c.recover(30.0f, 3.0f); // a rest on the ledge
    return c.start(c.mantleFeet());
}

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
        const bool chip = h.kind == ClimbHold::Kind::Foot;
        for (size_t j = 0; j < i; ++j) {
            if (holds[j].kind == ClimbHold::Kind::Edge || (!chip && holds[j].kind == ClimbHold::Kind::Foot)) continue;
            const float d = glm::length(glm::vec2(holds[j].position) - glm::vec2(h.position));
            EXPECT_GE(d, (chip ? w.desc().footSpacing : w.desc().spacing) * 0.99f);
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
    // On every mountain: no hold (on the route or not) in a ledge's stone,
    // where a hand on it would be inside the ledge.
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        const ClimbWall m = wall(seed);
        for (const kke::ClimbLedge& l : m.ledges())
            for (const ClimbHold& h : m.holds()) {
                if (h.kind == ClimbHold::Kind::Edge) continue;
                const glm::vec3 d = glm::abs(h.position - l.center) - l.halfExtents;
                EXPECT_FALSE(d.x < 0.0f && d.y < 0.05f && d.z < 0.0f) << "seed " << seed << ": a hold in a ledge at y " << h.position.y;
            }
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

// Hold jump until the charge is `charge`, aimed `aim`, then let go. What
// the jump itself cost.
float lunge(Rig& r, glm::vec2 aim, float charge) {
    Climber::Input in;
    in.aim = aim;
    in.jump = true;
    for (int i = 0; i < 600 && (r.c.charge() < charge || !r.c.charging()); ++i) r.c.update(in, kDt);
    in.jump = false;
    const float before = r.c.stamina();
    r.c.update(in, kDt);
    return before - r.c.stamina();
}

TEST(Climber, ALungeJumpsAndIsCaughtInTime) {
    Rig r;
    ASSERT_TRUE(r.mount());
    r.run({}, 0.3f);
    const float y = r.c.hips().y;
    const int feet = r.c.feetPlanted();
    const float cost = lunge(r, glm::vec2(0.0f, 1.0f), 1.0f);
    ASSERT_TRUE(r.c.flying());
    EXPECT_EQ(r.c.handHold(0), -1);
    EXPECT_EQ(r.c.handHold(1), -1);
    EXPECT_EQ(r.c.feetPlanted(), 0) << "the feet leave the rock too";
    // A full lunge costs the most (paid at the jump; double without feet on).
    EXPECT_NEAR(cost, r.c.settings().costDynoMax * (feet == 0 ? r.c.settings().handsOnly : 1.0f), 0.1f);
    // The trigger at air does nothing; once a hold is in reach it catches.
    int caughtBy = -1;
    float top = y;
    for (int i = 0; i < 120 && r.c.flying(); ++i) {
        Climber::Input in;
        top = std::max(top, r.c.hips().y);
        // Near the top of it (or on the way down), not straight back onto
        // the holds it left.
        const bool late = r.c.hips().y > y + r.c.settings().dynoMax * 0.85f || r.c.hips().y < top - 0.05f;
        for (int h = 0; h < 2 && caughtBy < 0 && late; ++h)
            if (r.c.aimTarget(h) >= 0) {
                in.reach[h] = true;
                caughtBy = h;
            }
        r.c.update(in, kDt);
    }
    ASSERT_GE(caughtBy, 0);
    EXPECT_FALSE(r.c.flying());
    EXPECT_EQ(r.c.state(), Climber::State::Climbing);
    EXPECT_GE(r.c.handHold(caughtBy), 0);
    EXPECT_GT(top, y + r.c.settings().dynoMax * 0.8f) << "a full lunge goes up most of dynoMax";
}

TEST(Climber, ALungeNobodyCatchesIsAFall) {
    Rig r;
    ASSERT_TRUE(r.mount());
    lunge(r, glm::vec2(0.0f, 1.0f), 0.6f);
    ASSERT_TRUE(r.c.flying());
    bool fell = false;
    for (int i = 0; i < 120 && !fell; ++i) {
        r.c.update({}, kDt);
        fell = r.c.fell();
    }
    EXPECT_TRUE(fell);
    EXPECT_EQ(r.c.state(), Climber::State::Fell);
}

TEST(Climber, ATapOfJumpIsNotALunge) {
    Rig r;
    ASSERT_TRUE(r.mount());
    r.run({}, 0.3f);
    const glm::vec3 hips = r.c.hips();
    Climber::Input in;
    in.jump = true;
    r.c.update(in, kDt);
    r.c.update(in, kDt);
    in.jump = false;
    r.c.update(in, kDt);
    EXPECT_FALSE(r.c.flying());
    EXPECT_GE(r.c.handHold(0), 0);
    EXPECT_GE(r.c.handHold(1), 0);
    EXPECT_LT(glm::length(r.c.hips() - hips), 0.02f) << "no hop on the spot";
}

TEST(Climber, FeetStepOntoFootholdsAndComeOffWhenOutOfReach) {
    Rig r;
    ASSERT_TRUE(r.mount());
    r.run({}, 0.3f);
    // Up off the ground, the feet step onto the footholds shown for them.
    for (int f = 0; f < 2; ++f) {
        Climber::Input in;
        in.step[f] = r.c.footTarget(f) >= 0;
        r.c.update(in, kDt);
        r.run({}, r.c.settings().stepTime + 0.05f);
    }
    EXPECT_GT(r.c.feetPlanted(), 0) << "there are footholds under the body";
    // Hands up the wall until a foot can't reach its hold any more.
    bool slipped = false;
    for (int move = 0; move < 8 && !slipped; ++move) {
        const int h = move % 2;
        Climber::Input in;
        in.aim = glm::vec2(0.0f, 1.0f);
        r.c.update(in, kDt);
        in.reach[h] = true;
        for (int i = 0; i < 40; ++i) {
            r.c.update(in, kDt);
            in.reach[h] = false;
            slipped = slipped || r.c.slipped() >= 0;
        }
    }
    ASSERT_TRUE(slipped);
    for (int f = 0; f < 2; ++f) {
        if (r.c.footHold(f) >= 0) continue;
        const int t = r.c.footTarget(f);
        if (t < 0) continue;
        Climber::Input in;
        in.step[f] = true;
        r.c.update(in, kDt);
        EXPECT_TRUE(r.c.footMoving(f));
        r.run({}, r.c.settings().stepTime + 0.05f);
        EXPECT_EQ(r.c.footHold(f), t);
        EXPECT_LT(glm::length(r.c.foot(f) - r.w.holds()[static_cast<size_t>(t)].position), 1e-3f);
    }
}

TEST(Climber, FeetOnRestTheArmsAndNoFeetCostDouble) {
    Rig r;
    ASSERT_TRUE(r.mount());
    r.run({}, 0.6f);
    // A climber whose legs reach nothing hangs on the arms alone.
    Rig noFeet;
    noFeet.c.settings().legReach = 0.05f;
    ASSERT_TRUE(noFeet.mount());
    noFeet.run({}, 0.6f);
    ASSERT_EQ(noFeet.c.feetPlanted(), 0);
    EXPECT_GT(noFeet.c.drainRate(), 0.0f);
    if (r.c.feetPlanted() == 2) {
        EXPECT_LT(r.c.drainRate(), 0.0f) << "both feet planted, both hands on: the arms recover";
    }
    // A reach costs double on the arms alone.
    Climber::Input in;
    in.aim = glm::vec2(0.0f, 1.0f);
    noFeet.c.update(in, kDt);
    const float before = noFeet.c.stamina();
    in.reach[0] = true;
    noFeet.c.update(in, kDt);
    ASSERT_TRUE(noFeet.c.handMoving(0));
    EXPECT_NEAR(before - noFeet.c.stamina(), noFeet.c.settings().costPrecise * noFeet.c.settings().handsOnly, 0.2f);
}

TEST(Climber, TwoHandsShareAHoldSideBySideAndStill) {
    Rig r;
    ASSERT_TRUE(r.mount());
    const int hold = r.c.handHold(Climber::kRight);
    Climber::Input in;
    in.pick[Climber::kLeft] = hold;
    r.c.update(in, kDt);
    in.reach[Climber::kLeft] = true;
    r.c.update(in, kDt);
    ASSERT_TRUE(r.c.handMoving(Climber::kLeft)) << "matching is allowed: pick " << r.c.aimTarget(Climber::kLeft) << " span " << r.c.canSpan(Climber::kLeft, hold)
                                                << " cross " << r.c.crossesOver(Climber::kLeft, hold);
    in.reach[Climber::kLeft] = false;
    r.run(in, 1.0f);
    ASSERT_EQ(r.c.handHold(Climber::kLeft), hold);
    const glm::vec3 l = r.c.grip(Climber::kLeft), rr = r.c.grip(Climber::kRight);
    EXPECT_NEAR(glm::length(rr - l), r.c.settings().handWidth, 0.01f) << "side by side, not one inside the other";
    EXPECT_LT(l.x, rr.x) << "the left hand on the left";
    // Settled, nothing shakes: the body and the hands stay put frame to frame.
    float worst = 0.0f;
    glm::vec3 last = r.c.hips();
    for (int i = 0; i < 120; ++i) {
        r.c.update({}, kDt);
        worst = std::max(worst, glm::length(r.c.hips() - last));
        last = r.c.hips();
    }
    EXPECT_LT(worst, 0.002f);
}

TEST(Climber, OneHandTiresFasterAndEmptyArmsLetGo) {
    Rig r;
    r.c.settings().legReach = 0.05f; // no feet on: the arms alone hold the body
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

TEST(Climber, AKnockCostsStaminaAndAHardOneFalls) {
    Rig r;
    ASSERT_TRUE(r.c.start(r.base()));
    r.run({}, 0.2f);
    const float before = r.c.stamina();
    r.c.knock(30.0f);
    EXPECT_NEAR(r.c.stamina(), before - 30.0f, 1e-3f);
    r.run({}, 0.1f);
    EXPECT_TRUE(r.c.climbing());
    r.c.knock(1000.0f);
    r.c.update({}, kDt);
    EXPECT_EQ(r.c.state(), Climber::State::Fell);
}

TEST(Climber, TheBotClimbsToTheSummitAndMantles) {
    int allLunges = 0, lungedOn = 0;
    for (uint32_t seed = 1; seed <= 40; ++seed) {
        ClimbWall w = wall(seed);
        Climber::Settings s;
        s.maxStamina = 1e6f; // this checks the route and the moves, not the rests
        Climber c(w, s);
        kke::ClimbBot bot(w.line());
        const glm::vec3 p = w.holds()[static_cast<size_t>(bot.route().front())].position;
        ASSERT_TRUE(c.start(glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f))) << "seed " << seed;
        float t = 0.0f;
        int lunges = 0;
        for (; t < 240.0f && c.state() != Climber::State::Fell; t += kDt) {
            c.update(bot.think(c, kDt), kDt);
            lunges += c.lunged() ? 1 : 0;
            if (!onAgain(c)) break;
        }
        allLunges += lunges;
        lungedOn += lunges > 0 ? 1 : 0;
        EXPECT_EQ(c.state(), Climber::State::Topped) << "seed " << seed << " stuck at y " << c.hips().y << " after " << t << " s";
        EXPECT_EQ(c.mantleLedge(), -1);
        EXPECT_NEAR(c.feet().y, w.summitY(), 0.05f);
        EXPECT_LT(c.feet().z, w.summitZ());
    }
    // Fresh, on most mountains it lunges past a hold now and then.
    EXPECT_GE(lungedOn, 30) << allLunges << " lunges on " << lungedOn << " of 40 mountains";
    EXPECT_GE(allLunges, 40);
}

TEST(Climber, TheArmsReachEveryHoldTheHandsAreOn) {
    // What a game's IK draws is only right if the hands can be where the
    // climber says: every hand on a hold within an arm of its shoulder,
    // all the way up (the wrist, as a game puts it on the hold).
    for (uint32_t seed : { 3u, 11u, 27u }) {
        ClimbWall w = wall(seed);
        Climber::Settings s;
        s.maxStamina = 1e6f;
        Climber c(w, s);
        kke::ClimbBot bot(w.line());
        const glm::vec3 p = w.holds()[static_cast<size_t>(bot.route().front())].position;
        ASSERT_TRUE(c.start(glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f)));
        float worst = 0.0f, settle = 0.0f;
        for (float t = 0.0f; t < 240.0f && c.state() != Climber::State::Fell; t += kDt) {
            c.update(bot.think(c, kDt), kDt);
            if (!onAgain(c)) break;
            if (c.state() != Climber::State::Climbing) continue;
            // A catch (a start, a lunge) pulls the body up to the hand: a
            // few frames to get there.
            settle = c.grabbed() || c.cutLoose() >= 0 || c.flying() ? 0.4f : std::max(0.0f, settle - kDt);
            if (settle > 0.0f) continue;
            for (int h = 0; h < 2; ++h) {
                const int hold = c.handHold(h);
                if (hold < 0) continue;
                const ClimbHold& hd = w.holds()[static_cast<size_t>(hold)];
                const float over = glm::length(c.wristAt(c.grip(h), hd.normal) - c.shoulder(h)) - s.armReach;
                worst = std::max(worst, over);
            }
        }
        EXPECT_EQ(c.state(), Climber::State::Topped) << "seed " << seed;
        EXPECT_LT(worst, s.cutLoose + 0.01f) << "seed " << seed << ": an arm stretched past its reach";
    }
}

TEST(Climber, ABodyHangsBetweenTheHandsOrNot) {
    Rig r;
    ASSERT_TRUE(r.mount());
    const int right = r.c.handHold(Climber::kRight);
    const glm::vec3 at = r.w.holds()[static_cast<size_t>(right)].position;
    int nearHold = -1, farHold = -1;
    for (size_t i = 0; i < r.w.holds().size(); ++i) {
        const ClimbHold& h = r.w.holds()[i];
        if (h.kind == ClimbHold::Kind::Edge || h.kind == ClimbHold::Kind::Foot || static_cast<int>(i) == right || r.c.crossesOver(Climber::kLeft, static_cast<int>(i)) ||
            h.position.x > at.x)
            continue; // the left hand's side
        const float d = glm::length(h.position - at);
        if (nearHold < 0 && d > 0.3f && d < 0.8f) nearHold = static_cast<int>(i);
        if (farHold < 0 && d > 1.9f && d < 2.4f) farHold = static_cast<int>(i);
    }
    ASSERT_GE(nearHold, 0);
    ASSERT_GE(farHold, 0);
    EXPECT_TRUE(r.c.canSpan(Climber::kLeft, nearHold));
    EXPECT_FALSE(r.c.canSpan(Climber::kLeft, farHold)) << "no body is two metres wide between its hands";
    // A bumper won't take a hand where the body can't follow.
    Climber::Input in;
    in.pick[Climber::kLeft] = farHold;
    in.reach[Climber::kLeft] = true;
    r.c.update(in, kDt);
    EXPECT_FALSE(r.c.handMoving(Climber::kLeft));
}

TEST(Climber, EachHandWorksItsOwnSide) {
    // A hold well over on the right, within the arms' length: the right
    // hand takes it, the left one doesn't reach across the body for it
    // (nor behind the back).
    Rig r;
    ASSERT_TRUE(r.mount());
    const int right = r.c.handHold(Climber::kRight);
    const glm::vec3 at = r.w.holds()[static_cast<size_t>(right)].position;
    int over = -1;
    for (size_t i = 0; i < r.w.holds().size(); ++i) {
        const glm::vec3 p = r.w.holds()[i].position;
        const float dx = p.x - at.x;
        if (static_cast<int>(i) != right && r.w.holds()[i].kind != ClimbHold::Kind::Edge && r.w.holds()[i].kind != ClimbHold::Kind::Foot && dx > 0.45f && dx < 1.2f && std::abs(p.y - at.y) < 0.8f) {
            over = static_cast<int>(i);
            break;
        }
    }
    ASSERT_GE(over, 0);
    EXPECT_FALSE(r.c.canSpan(Climber::kLeft, over)) << "the left arm would cross over the right one";
    Climber::Input in;
    in.pick[Climber::kLeft] = over;
    in.reach[Climber::kLeft] = true;
    r.c.update(in, kDt);
    EXPECT_FALSE(r.c.handMoving(Climber::kLeft));
    // Between the shoulders, either hand may go; past the other shoulder,
    // only that side's hand.
    const Climber::Settings& s = r.c.settings();
    const glm::vec3 hips = r.c.hips();
    EXPECT_TRUE(r.c.onItsSide(Climber::kLeft, hips + glm::vec3(s.shoulderHalf, 0.6f, 0.0f), hips));
    EXPECT_TRUE(r.c.onItsSide(Climber::kRight, hips + glm::vec3(-s.shoulderHalf, 0.6f, 0.0f), hips));
    EXPECT_FALSE(r.c.onItsSide(Climber::kLeft, hips + glm::vec3(s.shoulderHalf + s.crossReach + 0.1f, 0.6f, 0.0f), hips));
    EXPECT_TRUE(r.c.onItsSide(Climber::kRight, hips + glm::vec3(0.5f, 0.6f, 0.0f), hips));
}

TEST(Climber, StaminaMattersOnTheWayUp) {
    // Every mountain, with real stamina: the bot has to pace itself (shake out on jugs,
    // stand on ledges); it still gets up, and it gets tired doing it.
    for (uint32_t seed = 1; seed <= 40; ++seed) {
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
        EXPECT_EQ(c.state(), Climber::State::Topped) << "seed " << seed << " at y " << c.hips().y << " stamina " << c.staminaFraction();
        EXPECT_EQ(c.mantleLedge(), -1);
        EXPECT_LT(lowest, 0.75f) << "seed " << seed << ": the climb should tire you";
        EXPECT_GT(t, 15.0f) << "seed " << seed << ": 36 m should take a while";
    }
}

TEST(Climber, TheBodyStaysOutOfTheRockAndTheLedges) {
    // No knees, head or body through stone: all the way up, the hips, the
    // chest and the head clear the rock, and a body as tall as a ledge is
    // in front of it. Along the way a lip is taken where the hand is, not
    // only at its edge holds' points.
    bool alongLip = false;
    for (uint32_t seed : { 2u, 5u, 9u, 14u, 21u }) {
        ClimbWall w = wall(seed);
        Climber::Settings s;
        s.maxStamina = 1e6f;
        Climber c(w, s);
        kke::ClimbBot bot(w.line());
        const glm::vec3 p = w.holds()[static_cast<size_t>(bot.route().front())].position;
        ASSERT_TRUE(c.start(glm::vec3(p.x, 0.0f, w.surfaceZ(p.x, 1.0f) + 0.45f)));
        float settle = 0.0f;
        for (float t = 0.0f; t < 240.0f && c.state() != Climber::State::Fell; t += kDt) {
            c.update(bot.think(c, kDt), kDt);
            if (!onAgain(c)) break;
            if (c.state() != Climber::State::Climbing) continue;
            settle = c.grabbed() ? 0.4f : std::max(0.0f, settle - kDt);
            const glm::vec3 hips = c.hips();
            EXPECT_GE(hips.z, w.surfaceZ(hips.x, hips.y) + s.bodyIn - 0.02f) << "seed " << seed;
            EXPECT_GE(hips.z, w.surfaceZ(hips.x, hips.y + s.headUp - 0.1f) + 0.13f - 0.02f) << "seed " << seed << ": the head in the rock";
            if (settle <= 0.0f)
                for (const kke::ClimbLedge& l : w.ledges()) {
                    if (std::abs(hips.x - l.center.x) > l.halfExtents.x + s.shoulderHalf) continue;
                    const float low = hips.y - s.hipsHeight + 0.05f, high = hips.y + s.headUp;
                    if (high < l.center.y - l.halfExtents.y || low > l.top() - 0.02f) continue;
                    EXPECT_GE(hips.z, l.center.z + l.halfExtents.z + s.bodyDepth) << "seed " << seed << ": the body inside a ledge at y " << hips.y;
                }
            for (int h = 0; h < 2; ++h) {
                const int hold = c.handHold(h);
                if (hold < 0) continue;
                const ClimbHold& hd = w.holds()[static_cast<size_t>(hold)];
                if (hd.kind != ClimbHold::Kind::Edge) continue;
                const float off = std::abs(c.grip(h).x - hd.position.x);
                EXPECT_LE(off, hd.size + 0.03f + s.handWidth * 0.5f + 1e-3f);
                alongLip = alongLip || off > 0.05f;
            }
        }
        EXPECT_EQ(c.state(), Climber::State::Topped) << "seed " << seed;
        EXPECT_EQ(c.mantleLedge(), -1) << "seed " << seed;
    }
    EXPECT_TRUE(alongLip);
}

} // namespace
