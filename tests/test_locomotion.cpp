#include "kke/Locomotion.h"
#include "kke/SceneLoader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {

using kke::Locomotion;
using kke::RigidWorld;

constexpr float kDt = 1.0f / 60.0f;

struct Course {
    RigidWorld world;
    RigidWorld::CharacterId player = 0;
    Course() : world(settings()) {
        box({ 0, -0.5f, 0 }, { 50, 0.5f, 50 }); // floor
    }
    static RigidWorld::Settings settings() {
        RigidWorld::Settings s;
        s.threads = 0;
        return s;
    }
    void box(glm::vec3 center, glm::vec3 half) {
        RigidWorld::BodyDesc d;
        d.motion = RigidWorld::Motion::Static;
        d.halfExtents = half;
        d.position = center;
        world.add(d);
    }
    void spawn(glm::vec3 feet) {
        RigidWorld::CharacterDesc cd;
        cd.position = feet;
        player = world.addCharacter(cd);
        for (int i = 0; i < 10; ++i) world.step(kDt); // settle on the floor
    }
    // Runs `seconds` of frames with the same input (goUp only on the first).
    void run(Locomotion& loco, Locomotion::Input in, float seconds) {
        const int frames = static_cast<int>(std::lround(seconds / kDt));
        for (int i = 0; i < frames; ++i) {
            loco.update(in, kDt);
            world.step(kDt);
            in.goUp = false;
        }
    }
    glm::vec3 feet() const { return world.characterPosition(player); }
};

Locomotion::Input forward(bool fast = false) {
    Locomotion::Input in;
    in.move = glm::vec3(0, 0, -1);
    in.fast = fast;
    return in;
}

} // namespace

TEST(Locomotion, LeapParabolaHitsBothEndsAndPeaksAtTheOvershoot) {
    for (float yl : { 0.0f, 0.8f, -1.0f }) {
        const float xl = 3.0f, h = std::max(0.6f, 0.4f - yl); // peak above the start
        glm::vec2 ab = Locomotion::leapParabola(xl, yl, h);
        EXPECT_LT(ab.x, 0.0f);
        EXPECT_NEAR(ab.x * xl * xl + ab.y * xl, yl, 1e-3f);
        const float px = -ab.y / (2.0f * ab.x);
        EXPECT_GT(px, 0.0f);
        EXPECT_LT(px, xl);
        EXPECT_NEAR(ab.x * px * px + ab.y * px, yl + h, 1e-3f);
    }
}

TEST(Locomotion, AngleBetweenWraps) {
    EXPECT_NEAR(Locomotion::angleBetween(170.0f, -170.0f), 20.0f, 1e-4f);
    EXPECT_NEAR(Locomotion::angleBetween(-170.0f, 170.0f), -20.0f, 1e-4f);
    EXPECT_NEAR(Locomotion::angleBetween(0.0f, 180.0f), 180.0f, 1e-4f);
}

TEST(Locomotion, AwarenessClassifiesVaultClimbAndWall) {
    Course c;
    c.box({ 0, 0.5f, -1.0f }, { 2.0f, 0.5f, 0.15f });   // 1 m fence, 0.3 m thick, face at z = -0.85
    c.box({ 6, 0.75f, -2.0f }, { 1.0f, 0.75f, 1.5f });  // 1.5 m block, face at z = -0.5
    c.box({ -6, 1.5f, -1.0f }, { 1.0f, 1.5f, 0.3f });   // 3 m wall
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    const auto& s = loco.settings();
    Locomotion::Obstacle fence = loco.probe({ 0, 0, -1 }, s.walkSensor);
    EXPECT_EQ(fence.kind, Locomotion::Obstacle::Kind::Vault);
    EXPECT_NEAR(fence.height, 1.0f, 0.05f);
    EXPECT_NEAR(fence.depth, 0.3f, 0.12f);
    EXPECT_NEAR(fence.normal.z, 1.0f, 1e-3f);
    EXPECT_LT(fence.target.z, -1.2f); // lands behind it

    c.world.teleportCharacter(c.player, { 6, 0.01f, 0 });
    Locomotion::Obstacle block = loco.probe({ 0, 0, -1 }, s.walkSensor);
    EXPECT_EQ(block.kind, Locomotion::Obstacle::Kind::Climb);
    EXPECT_NEAR(block.target.y, 1.5f, 0.05f);

    c.world.teleportCharacter(c.player, { -6, 0.01f, 0 });
    EXPECT_EQ(loco.probe({ 0, 0, -1 }, s.sprintSensor).kind, Locomotion::Obstacle::Kind::None);
    // Nothing in the other direction.
    EXPECT_EQ(loco.probe({ 0, 0, 1 }, s.sprintSensor).kind, Locomotion::Obstacle::Kind::None);
}

TEST(Locomotion, RunningVaultLandsOnTheOtherSideAndKeepsMomentum) {
    Course c;
    c.box({ 0, 0.5f, -3.0f }, { 3.0f, 0.5f, 0.15f }); // fence
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    // Run up to it; press "go up" once the sensor sees it (as a player would).
    for (int i = 0; i < 120 && loco.probe({ 0, 0, -1 }, loco.settings().walkSensor).kind != Locomotion::Obstacle::Kind::Vault; ++i) {
        loco.update(forward(), kDt);
        c.world.step(kDt);
    }
    ASSERT_EQ(loco.state(), Locomotion::State::Ground);
    ASSERT_GT(loco.groundSpeed(), 3.0f);
    Locomotion::Input go = forward();
    go.goUp = true;
    loco.update(go, kDt);
    c.world.step(kDt);
    ASSERT_EQ(loco.state(), Locomotion::State::Vault);
    float maxY = 0.0f;
    for (int i = 0; i < 120 && loco.state() == Locomotion::State::Vault; ++i) {
        loco.update(forward(), kDt);
        c.world.step(kDt);
        maxY = std::max(maxY, c.feet().y);
    }
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_LT(c.feet().z, -3.4f);
    EXPECT_GT(maxY, 0.85f); // went over, not through
    EXPECT_NEAR(c.feet().y, 0.0f, 0.05f);
    c.run(loco, forward(), 0.2f);
    EXPECT_GT(loco.groundSpeed(), 2.0f); // still running
}

TEST(Locomotion, ClimbsOntoAChestHighBlock) {
    Course c;
    c.box({ 0, 0.75f, -2.5f }, { 2.0f, 0.75f, 1.5f }); // 1.5 m block, face at z = -1
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    Locomotion::Input go = forward();
    go.move *= 0.3f; // walk up slowly
    c.run(loco, go, 1.2f);
    go.goUp = true;
    c.run(loco, go, 0.05f);
    ASSERT_EQ(loco.state(), Locomotion::State::Climb);
    c.run(loco, Locomotion::Input{}, 1.5f);
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_NEAR(c.feet().y, 1.5f, 0.06f);
    EXPECT_LT(c.feet().z, -1.2f);
}

TEST(Locomotion, TooTallWallIsJustAJump) {
    Course c;
    c.box({ 0, 1.5f, -1.0f }, { 2.0f, 1.5f, 0.3f });
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    Locomotion::Input go;
    go.goUp = true;
    loco.update(go, kDt);
    EXPECT_TRUE(loco.jumped());
    EXPECT_EQ(loco.state(), Locomotion::State::Air);
}

TEST(Locomotion, TurnsAtALimitedRateAndSlowsInSharpTurns) {
    Course c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    Locomotion::Input right;
    right.move = glm::vec3(1, 0, 0);
    c.run(loco, right, 1.0f);
    ASSERT_GT(loco.groundSpeed(), 3.0f);
    // Reverse: the velocity must not flip in one frame.
    Locomotion::Input left;
    left.move = glm::vec3(-1, 0, 0);
    c.run(loco, left, 2 * kDt);
    glm::vec3 v = c.world.characterVelocity(c.player);
    EXPECT_GT(v.x, 0.0f);
    // Mid turn, it's slower than a straight run.
    c.run(loco, left, 0.12f);
    EXPECT_LT(loco.groundSpeed(), loco.settings().runSpeed * 0.8f);
    // And it gets there.
    c.run(loco, left, 1.0f);
    v = c.world.characterVelocity(c.player);
    EXPECT_LT(v.x, -3.0f);
    EXPECT_NEAR(loco.facing().x, -1.0f, 0.02f);
}

TEST(Locomotion, ReversalKeepsItsTurningSide) {
    Course c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    c.run(loco, forward(), 1.0f);
    // Exactly backwards, with the input wobbling across 180 degrees.
    float firstSide = 0.0f;
    for (int i = 0; i < 12; ++i) {
        Locomotion::Input back;
        back.move = glm::vec3((i % 2 ? 0.02f : -0.02f), 0, 1);
        loco.update(back, kDt);
        c.world.step(kDt);
        const float side = loco.facing().x;
        if (i == 3) firstSide = side;
        if (i > 3 && std::abs(firstSide) > 0.05f) EXPECT_GT(side * firstSide, 0.0f) << "flipped sides at frame " << i;
    }
}

TEST(Locomotion, AirControlIsASmallCorrection) {
    Course c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    Locomotion::Input up;
    up.goUp = true;
    c.run(loco, up, kDt);
    ASSERT_EQ(loco.state(), Locomotion::State::Air);
    c.run(loco, forward(), 0.5f);
    const glm::vec3 v = c.world.characterVelocity(c.player);
    EXPECT_LE(glm::length(glm::vec2(v.x, v.z)), loco.settings().airSpeedMin + 0.05f);
    EXPECT_GT(-v.z, 0.5f); // but it did steer
}

TEST(Locomotion, SmallDropStaysOnTheGroundBigDropFalls) {
    Course c;
    c.box({ 0, 0.1f, 0 }, { 1.0f, 0.1f, 1.0f });      // 0.2 m kerb
    c.box({ 10, 1.0f, 0 }, { 1.0f, 1.0f, 1.0f });     // 2 m ledge
    c.spawn({ 0, 0.21f, 0 });
    Locomotion loco(c.world, c.player);
    bool wasInAir = false;
    for (int i = 0; i < 60; ++i) {
        loco.update(forward(), kDt);
        c.world.step(kDt);
        wasInAir |= loco.state() == Locomotion::State::Air;
    }
    EXPECT_FALSE(wasInAir);
    EXPECT_LT(c.feet().z, -1.5f);

    c.world.teleportCharacter(c.player, { 10, 2.01f, 0 });
    for (int i = 0; i < 5; ++i) c.world.step(kDt);
    for (int i = 0; i < 60; ++i) {
        loco.update(forward(), kDt);
        c.world.step(kDt);
        wasInAir |= loco.state() == Locomotion::State::Air;
    }
    EXPECT_TRUE(wasInAir);
}

TEST(Locomotion, BufferedJumpFiresOnLanding) {
    Course c;
    c.spawn({ 0, 1.0f, 0 }); // lands after a short drop
    Locomotion loco(c.world, c.player);
    // Falling: wait until just above the floor, then press.
    for (int i = 0; i < 60 && c.feet().y > 0.25f; ++i) {
        loco.update(Locomotion::Input{}, kDt);
        c.world.step(kDt);
    }
    Locomotion::Input up;
    up.goUp = true;
    bool jumped = false;
    loco.update(up, kDt);
    c.world.step(kDt);
    jumped |= loco.jumped();
    for (int i = 0; i < 20 && !jumped; ++i) {
        loco.update(Locomotion::Input{}, kDt);
        c.world.step(kDt);
        jumped |= loco.jumped();
    }
    EXPECT_TRUE(jumped);
}

// kke_demo's parkour lane end to end, at 60 fps and at a slow 15 fps:
// vault the fence and the low wall, climb the block, drop off, sprint and
// climb the 2.1 m ledge. "Go up" is pressed whenever the sensors see
// something, as the demo's autopilot does.
TEST(Locomotion, ParkourLaneEndToEnd) {
    Course c;
    c.box({ 0, 0.005f, 17.0f }, { 2.6f, 0.005f, 11.5f });
    c.box({ 0, 0.5f, 24.0f }, { 2.5f, 0.5f, 0.15f });
    c.box({ 0, 0.3f, 20.5f }, { 2.5f, 0.3f, 0.25f });
    c.box({ 0, 0.75f, 16.0f }, { 2.5f, 0.75f, 1.2f });
    c.box({ 0, 1.05f, 9.5f }, { 2.5f, 1.05f, 1.5f });
    c.spawn({ 0, 0.05f, 28.0f });
    Locomotion loco(c.world, c.player);
    for (float dt : { 1.0f / 60.0f, 1.0f / 15.0f }) {
        loco.teleport({ 0, 0.05f, 28.0f });
        int vaults = 0, climbs = 0;
        Locomotion::State last = loco.state();
        for (int i = 0; i < int(20 / dt) && c.feet().z > 8.5f; ++i) {
            Locomotion::Input in = forward();
            in.fast = c.feet().z < 14.8f;
            const auto& ls = loco.settings();
            in.goUp = loco.state() == Locomotion::State::Ground &&
                      loco.probe(in.move, in.fast ? ls.sprintSensor : ls.walkSensor).kind != Locomotion::Obstacle::Kind::None;
            loco.update(in, dt);
            c.world.step(dt);
            if (loco.state() != last) {
                vaults += loco.state() == Locomotion::State::Vault;
                climbs += loco.state() == Locomotion::State::Climb;
                last = loco.state();
            }
        }
        EXPECT_EQ(vaults, 2) << "dt " << dt;
        EXPECT_EQ(climbs, 2) << "dt " << dt;
        EXPECT_LE(c.feet().z, 8.5f) << "dt " << dt;
        EXPECT_NEAR(c.feet().y, 2.1f, 0.1f) << "dt " << dt; // on top of the ledge
    }
}

// The shipped Synty scenes, run end to end by a sprinting autopilot that
// presses "go up" whenever the sprint sensor sees something. Needs the
// packs in assets/synty/ (never committed), so CI skips it.
namespace {
struct TrailRun {
    int vaults = 0, climbs = 0;
    glm::vec3 end{0.0f};
};

TrailRun runTrail(const char* name, float stopZ) {
    namespace fs = std::filesystem;
    TrailRun out;
    kke::AssetCatalog cat = kke::AssetCatalog::scan((fs::path(KKE_SOURCE_DIR) / "assets/synty").string());
    kke::SceneFile sc = kke::SceneFile::load((fs::path(KKE_SOURCE_DIR) / "scenes" / (std::string(name) + ".scene.json")).string());
    RigidWorld::Settings st;
    st.threads = 0;
    RigidWorld world(st);
    kke::LoadedScene ls = kke::loadSceneCollision(sc, cat, world);
    EXPECT_TRUE(ls.missing.empty()) << name << ": " << ls.missing.size() << " assets not found";
    RigidWorld::CharacterDesc cd;
    cd.position = sc.spawn;
    auto id = world.addCharacter(cd);
    Locomotion loco(world, id);
    const float dt = 1.0f / 60.0f;
    Locomotion::State last = loco.state();
    for (int i = 0; i < 60 * 20; ++i) {
        Locomotion::Input in = forward(true);
        in.goUp = loco.state() == Locomotion::State::Ground &&
                  loco.probe(in.move, loco.settings().sprintSensor).kind != Locomotion::Obstacle::Kind::None;
        loco.update(in, dt);
        world.step(dt);
        if (loco.state() != last) {
            last = loco.state();
            if (last == Locomotion::State::Vault) ++out.vaults;
            if (last == Locomotion::State::Climb) ++out.climbs;
        }
        out.end = world.characterPosition(id);
        if (out.end.z < stopZ) break;
    }
    return out;
}

bool haveSyntyPacks() {
    return std::filesystem::exists(std::filesystem::path(KKE_SOURCE_DIR) / "assets/synty/POLYGON_Nature_Source_Files") &&
           std::filesystem::exists(std::filesystem::path(KKE_SOURCE_DIR) / "assets/synty/PolygonTown_Source_Files");
}
} // namespace

TEST(SceneTrails, ForestTrail) {
    if (!haveSyntyPacks()) GTEST_SKIP() << "Synty packs not in assets/synty/";
    // Pillar, stone wall, stump and fence are vaults; the rock shelf is a
    // climb; the log is small enough to step over.
    TrailRun r = runTrail("forest_trail", -14.0f);
    EXPECT_EQ(r.vaults, 4);
    EXPECT_EQ(r.climbs, 1);
    EXPECT_LT(r.end.z, -14.0f);
}

TEST(SceneTrails, TownBlockFence) {
    if (!haveSyntyPacks()) GTEST_SKIP() << "Synty packs not in assets/synty/";
    // Straight at the seam between two picket-fence panels.
    TrailRun r = runTrail("town_block", 0.0f);
    EXPECT_EQ(r.vaults, 1);
    EXPECT_LT(r.end.z, 0.0f);
}
