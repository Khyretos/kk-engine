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
        if (i > 3 && std::abs(firstSide) > 0.05f) {
            EXPECT_GT(side * firstSide, 0.0f) << "flipped sides at frame " << i;
        }
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

kke::SceneFile loadTrailScene(const char* name) {
    namespace fs = std::filesystem;
    return kke::SceneFile::load((fs::path(KKE_SOURCE_DIR) / "scenes" / (std::string(name) + ".scene.json")).string());
}

TrailRun runTrail(const kke::SceneFile& sc, const char* name, float stopZ) {
    namespace fs = std::filesystem;
    TrailRun out;
    kke::AssetCatalog cat = kke::AssetCatalog::scan((fs::path(KKE_SOURCE_DIR) / "assets/synty").string());
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

TrailRun runTrail(const char* name, float stopZ) { return runTrail(loadTrailScene(name), name, stopZ); }

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
// A level saved again (what the sandbox editor does: load, edit, save)
// plays the same: same fence, same single vault.
TEST(SceneTrails, TownBlockAfterASaveRoundTrip) {
    if (!haveSyntyPacks()) GTEST_SKIP() << "Synty packs not in assets/synty/";
    const kke::SceneFile resaved = kke::SceneFile::parse(loadTrailScene("town_block").toJson(), "resaved");
    TrailRun r = runTrail(resaved, "town_block (resaved)", 0.0f);
    EXPECT_EQ(r.vaults, 1);
    EXPECT_LT(r.end.z, 0.0f);
}

// Falling onto a ramp: the feet slide sideways on landing while the
// velocity points straight down. Once, that direction was normalized
// from zero and the character's position became NaN (showcase ramp).
TEST(Locomotion, LandingOnARampStaysFinite) {
    Course c;
    RigidWorld::BodyDesc d;
    d.motion = RigidWorld::Motion::Static;
    d.halfExtents = { 2.2f, 0.12f, 1.8f };
    d.position = { -3.2f, 0.95f, -7.0f };
    d.rotation = glm::angleAxis(glm::radians(-24.0f), glm::vec3(0, 0, 1));
    c.world.add(d);
    RigidWorld::CharacterDesc cd;
    cd.position = { -3.0f, 1.5f, -7.0f };
    c.player = c.world.addCharacter(cd);
    Locomotion loco(c.world, c.player);
    bool landed = false;
    glm::vec3 settled(0.0f);
    for (int i = 0; i < 240; ++i) {
        loco.update(Locomotion::Input{}, kDt);
        c.world.step(kDt);
        landed |= loco.landed();
        const glm::vec3 f = c.feet();
        ASSERT_TRUE(std::isfinite(f.x) && std::isfinite(f.y) && std::isfinite(f.z)) << "frame " << i;
        if (i == 60) settled = f;
    }
    EXPECT_TRUE(landed);
    // Then it stands still on the (walkable) ramp instead of creeping down.
    EXPECT_NEAR(c.feet().x, settled.x, 0.01f);
    EXPECT_NEAR(c.feet().y, settled.y, 0.01f);
}

// The demo runs physics at a fixed 60 Hz (fixedUpdate) and Locomotion once
// per rendered frame (update). At 144 fps most frames have no physics step,
// so the feet hadn't moved and the measured speed read 0 (idle), then 2.4x
// on the frame that did step: the animation flashed between idle and walk.
// A jittery ~60 fps (0, 1 or 2 steps a frame) did the same.
namespace {

struct FrameResult {
    float minSpeed = 1e9f, maxSpeed = 0.0f, distance = 0.0f;
};

// Walks forward for 3 s of rendered frames of `frameDt(i)` seconds, with
// physics on a 60 Hz accumulator exactly like Application::run.
template <class FrameDt>
FrameResult walkWithFixedPhysics(Locomotion::Input in, FrameDt frameDt) {
    Course c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    FrameResult r;
    float accumulator = 0.0f, t = 0.0f;
    glm::vec3 start{};
    for (int i = 0; t < 3.0f; ++i) {
        const float dt = frameDt(i);
        t += dt;
        accumulator += dt;
        while (accumulator >= kDt) {
            c.world.step(kDt);
            accumulator -= kDt;
        }
        loco.update(in, dt);
        if (t < 1.0f) { start = c.feet(); continue; } // up to speed first
        r.minSpeed = std::min(r.minSpeed, loco.groundSpeed());
        r.maxSpeed = std::max(r.maxSpeed, loco.groundSpeed());
    }
    r.distance = glm::length(glm::vec2(c.feet().x - start.x, c.feet().z - start.z));
    return r;
}

} // namespace

TEST(Locomotion, MeasuredSpeedIsSteadyWhenFramesOutpacePhysics) {
    Locomotion::Input walk = forward();
    walk.slow = true;
    const float v = Locomotion::Settings{}.walkSpeed;
    const FrameResult r = walkWithFixedPhysics(walk, [](int) { return 1.0f / 144.0f; });
    EXPECT_GT(r.minSpeed, v * 0.9f) << "read as standing still between physics steps";
    EXPECT_LT(r.maxSpeed, v * 1.1f);
    EXPECT_NEAR(r.distance, v * 2.0f, v * 2.0f * 0.1f) << "commanded speed was held back";
}

TEST(Locomotion, MeasuredSpeedIsSteadyWithJitteryFrames) {
    const float v = Locomotion::Settings{}.runSpeed;
    // Around 60 fps, but frames land on either side of the physics tick.
    const FrameResult r = walkWithFixedPhysics(forward(), [](int i) { return (i % 3 == 0 ? 0.6f : 1.2f) / 60.0f; });
    EXPECT_GT(r.minSpeed, v * 0.9f);
    EXPECT_LT(r.maxSpeed, v * 1.1f);
    EXPECT_NEAR(r.distance, v * 2.0f, v * 2.0f * 0.1f);
}

// Ledge hang (*Ledge actions*): a 3 m wall, 6 m wide. Jumping at it while
// steering in (not holding "go up") catches the top and hangs.
namespace {
struct HangCourse : Course {
    HangCourse() { box({ 0, 1.5f, -2.0f }, { 3.0f, 1.5f, 0.5f }); } // face at z = -1.5, top at 3.0
};
void jumpToHang(HangCourse& c, Locomotion& loco) {
    Locomotion::Input in = forward();
    in.goUp = true;
    c.run(loco, in, 1.2f);
}
Locomotion::Input sideways(float x) {
    Locomotion::Input in;
    in.move = glm::vec3(x, 0, 0);
    return in;
}
} // namespace

TEST(Locomotion, JumpAtAHighWallHangsFromTheTop) {
    HangCourse c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang) << "feet " << c.feet().y << " grabbed height " << loco.lastObstacle().height << " progress " << loco.traversalProgress();
    EXPECT_NEAR(c.feet().y, 3.0f - loco.settings().hangReach, 0.05f);
    EXPECT_NEAR(c.feet().z, -1.5f + loco.settings().radius + 0.05f, 0.05f);
    EXPECT_NEAR(loco.hangEdge().y, 3.0f, 0.02f);
    // It stays there with no input.
    c.run(loco, Locomotion::Input{}, 2.0f);
    EXPECT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_NEAR(c.feet().y, 3.0f - loco.settings().hangReach, 0.05f);
}

// Along the edge, then around the wall's outside corner onto its side
// face, still holding the same input.
TEST(Locomotion, ShimmyAlongTheEdgeAndAroundTheCorner) {
    HangCourse c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    c.run(loco, sideways(1.0f), 1.0f); // facing -Z, +X is the character's right
    EXPECT_NEAR(c.feet().x, loco.settings().shimmySpeed * 1.0f, 0.15f);
    EXPECT_GT(loco.shimmySpeed(), 0.5f);
    c.run(loco, sideways(1.0f), 4.0f); // past the end of the 6 m wall
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    const float r = loco.settings().radius;
    EXPECT_NEAR(c.feet().x, 3.0f + r + 0.05f, 0.05f); // on the side face (x = 3)
    EXPECT_LT(c.feet().z, -1.5f);
    EXPECT_NEAR(loco.facing().x, -1.0f, 0.05f);
    EXPECT_NEAR(c.feet().y, 3.0f - loco.settings().hangReach, 0.05f);
    c.run(loco, sideways(1.0f), 3.0f); // keeps going along the side face to its far end
    EXPECT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_GT(c.feet().x, 3.0f);
}

// Where the top steps up (a taller block continues the wall) the ledge
// ends: the character stops instead of shimmying into it.
TEST(Locomotion, ShimmyStopsWhereTheTopStepsUp) {
    HangCourse c;
    c.box({ 4.5f, 2.0f, -2.0f }, { 1.5f, 2.0f, 0.5f }); // 4 m tall, x 3..6
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    c.run(loco, sideways(1.0f), 5.0f);
    EXPECT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_GT(c.feet().x, 2.5f);
    EXPECT_LT(c.feet().x, 3.05f);
    EXPECT_NEAR(loco.facing().z, -1.0f, 0.05f);
    c.run(loco, sideways(-1.0f), 1.0f); // and back
    EXPECT_LT(c.feet().x, 2.2f);
}

// An L of two 3 m walls: shimmying into the inside corner turns onto the
// wall ahead.
TEST(Locomotion, ShimmyIntoAnInsideCorner) {
    HangCourse c;
    c.box({ 2.5f, 1.5f, 0.0f }, { 0.5f, 1.5f, 1.5f }); // face at x = 2, z -1.5..1.5
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    c.run(loco, sideways(1.0f), 3.0f);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    const float r = loco.settings().radius;
    EXPECT_NEAR(c.feet().x, 2.0f - r - 0.05f, 0.05f);
    EXPECT_NEAR(loco.facing().x, 1.0f, 0.05f);
    EXPECT_GT(c.feet().z, -1.5f + r); // along the new wall, away from the corner
}

// "Go up" while pushing away from the wall jumps back off it.
TEST(Locomotion, JumpBackOffAHang) {
    HangCourse c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    Locomotion::Input away;
    away.move = glm::vec3(0, 0, 1);
    away.goUp = true;
    c.run(loco, away, 0.05f);
    EXPECT_EQ(loco.state(), Locomotion::State::Air);
    EXPECT_TRUE(loco.facing().z > 0.9f);
    away.goUp = false;
    c.run(loco, away, 2.0f);
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_GT(c.feet().z, 0.3f); // well away from the wall
}

TEST(Locomotion, ClimbUpFromAHang) {
    HangCourse c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    Locomotion::Input up;
    up.goUp = true;
    c.run(loco, up, 0.1f);
    EXPECT_EQ(loco.state(), Locomotion::State::Climb);
    c.run(loco, Locomotion::Input{}, 1.5f);
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_NEAR(c.feet().y, 3.0f, 0.05f);
    EXPECT_LT(c.feet().z, -1.5f);
}

TEST(Locomotion, CrouchLetsGoAndDoesNotRegrab) {
    HangCourse c;
    c.spawn({ 0, 0.01f, 0 });
    Locomotion loco(c.world, c.player);
    jumpToHang(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    Locomotion::Input drop = forward(); // still steering into the wall
    drop.crouch = true;
    c.run(loco, drop, 0.05f);
    EXPECT_EQ(loco.state(), Locomotion::State::Air);
    c.run(loco, forward(), 1.5f);
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_NEAR(c.feet().y, 0.0f, 0.05f);
}
// A 0.6 m thick, 3 m wall (the showcase lane's): too thin to stand on,
// so there's no climb, but its top is an edge to hang from, at 15 fps.
TEST(Locomotion, HangsFromAThinWallItCannotStandOn) {
    Course c;
    c.box({ 16.0f, 1.5f, 12.0f }, { 0.3f, 1.5f, 4.0f });
    c.spawn({ 17.2f, 0.01f, 12.0f });
    Locomotion loco(c.world, c.player);
    Locomotion::Input in;
    in.move = glm::vec3(-1, 0, 0);
    const float dt = 1.0f / 15.0f;
    for (int i = 0; i < 30; ++i) {
        in.goUp = (i == 4);
        loco.update(in, dt);
        c.world.step(dt);
    }
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_NEAR(c.feet().y, 3.0f - loco.settings().hangReach, 0.05f);
    EXPECT_NEAR(c.feet().x, 16.3f + loco.settings().radius + 0.05f, 0.05f);
}

// Ledge leaps: hanging near the end of one wall, "go up" toward a second,
// taller wall across a 1 m gap leaps over and hangs from its top.
TEST(Locomotion, LeapsSidewaysAcrossAGapToAHigherEdge) {
    Course c;
    c.box({ 0.0f, 1.5f, -2.0f }, { 1.0f, 1.5f, 0.5f });  // x -1..1, top 3.0
    c.box({ 2.8f, 1.7f, -2.0f }, { 0.8f, 1.7f, 0.5f });  // x 2..3.6, top 3.4
    c.spawn({ 0.6f, 0.01f, 0.0f });
    Locomotion loco(c.world, c.player);
    Locomotion::Input jump = forward();
    jump.goUp = true;
    c.run(loco, jump, 1.2f);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    Locomotion::Input leap = sideways(1.0f);
    leap.goUp = true;
    c.run(loco, leap, 0.05f);
    ASSERT_EQ(loco.state(), Locomotion::State::Leap);
    c.run(loco, Locomotion::Input{}, 0.8f);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_NEAR(loco.hangEdge().y, 3.4f, 0.02f);
    EXPECT_GT(c.feet().x, 2.0f + loco.settings().radius);
    EXPECT_NEAR(c.feet().y, 3.4f - loco.settings().hangReach, 0.05f);
    // Nothing further that way: "go up" sideways climbs instead of leaping.
    c.run(loco, leap, 0.05f);
    EXPECT_NE(loco.state(), Locomotion::State::Leap);
}

// A thin wall (nothing to stand on) under a beam: "go up" leaps up to the
// beam's top.
TEST(Locomotion, LeapsUpToAnEdgeAbove) {
    Course c;
    c.box({ 0.0f, 1.2f, -1.65f }, { 2.0f, 1.2f, 0.15f });   // face z = -1.5, top 2.4
    c.box({ 0.0f, 3.35f, -1.65f }, { 2.0f, 0.25f, 0.15f }); // beam 3.1..3.6
    c.spawn({ 0.0f, 0.01f, 0.0f });
    Locomotion loco(c.world, c.player);
    Locomotion::Input jump = forward();
    jump.goUp = true;
    c.run(loco, jump, 1.2f);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_NEAR(loco.hangEdge().y, 2.4f, 0.02f);
    Locomotion::Input up = forward();
    up.goUp = true;
    c.run(loco, up, 0.05f);
    ASSERT_EQ(loco.state(), Locomotion::State::Leap);
    c.run(loco, Locomotion::Input{}, 0.8f);
    ASSERT_EQ(loco.state(), Locomotion::State::Hang);
    EXPECT_NEAR(loco.hangEdge().y, 3.6f, 0.02f);
    EXPECT_NEAR(c.feet().y, 3.6f - loco.settings().hangReach, 0.05f);
}

namespace {
// A 20 m wall along -Z with its face at x = 0.8, and the runner beside it.
struct WallRunCourse : Course {
    WallRunCourse() { box({ 1.0f, 2.0f, -10.0f }, { 0.2f, 2.0f, 10.0f }); }
};
void sprintAndJump(WallRunCourse& c, Locomotion& loco) {
    c.run(loco, forward(true), 1.0f);
    Locomotion::Input jump = forward(true);
    jump.goUp = true;
    c.run(loco, jump, 0.3f);
}
} // namespace

TEST(Locomotion, JumpingAlongAWallRunsOnIt) {
    WallRunCourse c;
    c.spawn({ 0.25f, 0.01f, 1.0f });
    Locomotion loco(c.world, c.player);
    sprintAndJump(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::WallRun);
    EXPECT_EQ(loco.wallRunSide(), 1.0f); // heading -Z, the wall at +X is on the right
    const glm::vec3 start = c.feet();
    c.run(loco, forward(true), 0.4f);
    EXPECT_EQ(loco.state(), Locomotion::State::WallRun);
    EXPECT_LT(c.feet().z, start.z - 1.5f);                       // running along it
    EXPECT_NEAR(c.feet().x, 0.8f - loco.settings().radius - 0.05f, 0.05f); // beside it
    EXPECT_GT(c.feet().y, 0.5f);                                  // up off the ground
    // It ends: gravity wins, the runner lands and keeps running.
    c.run(loco, forward(true), 2.0f);
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_NEAR(c.feet().y, 0.0f, 0.05f);
}

TEST(Locomotion, WallJumpKicksOffTheWall) {
    WallRunCourse c;
    c.spawn({ 0.25f, 0.01f, 1.0f });
    Locomotion loco(c.world, c.player);
    sprintAndJump(c, loco);
    ASSERT_EQ(loco.state(), Locomotion::State::WallRun);
    c.run(loco, forward(true), 0.2f);
    Locomotion::Input kick = forward(true);
    kick.goUp = true;
    c.run(loco, kick, 0.05f);
    EXPECT_EQ(loco.state(), Locomotion::State::Air);
    EXPECT_TRUE(loco.jumped() || c.world.characterVelocity(c.player).y > 2.0f);
    EXPECT_LT(c.world.characterVelocity(c.player).x, -2.0f); // away from the wall
    c.run(loco, forward(true), 2.0f);
    EXPECT_EQ(loco.state(), Locomotion::State::Ground);
    EXPECT_LT(c.feet().x, -0.5f);
}

// Walking (too slow) and jumping next to a wall is only a jump.
TEST(Locomotion, SlowJumpNextToAWallIsNoWallRun) {
    WallRunCourse c;
    c.spawn({ 0.25f, 0.01f, 1.0f });
    Locomotion loco(c.world, c.player);
    c.run(loco, forward(), 1.0f);
    Locomotion::Input jump = forward();
    jump.goUp = true;
    for (int i = 0; i < 60; ++i) {
        loco.update(jump, kDt);
        c.world.step(kDt);
        jump.goUp = false;
        ASSERT_NE(loco.state(), Locomotion::State::WallRun);
    }
}
