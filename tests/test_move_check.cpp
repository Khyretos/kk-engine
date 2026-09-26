#include "kke/net/WorldMoveCheck.h"

#include <gtest/gtest.h>

using kke::RigidWorld;
using kke::net::NetPlayerState;
using kke::net::WorldMoveCheck;
using Verdict = WorldMoveCheck::Verdict;

namespace {

RigidWorld::Settings single() {
    RigidWorld::Settings s;
    s.threads = 0;
    return s;
}

RigidWorld::BodyId box(RigidWorld& w, glm::vec3 min, glm::vec3 max, RigidWorld::Motion motion = RigidWorld::Motion::Static) {
    RigidWorld::BodyDesc d;
    d.motion = motion;
    d.halfExtents = (max - min) * 0.5f;
    d.position = (min + max) * 0.5f;
    return w.add(d);
}

// A level: ground; a 2 m wall across x = 5; a 1 m wall across x = 10
// (vaultable); a 2 m block from x = 20 to 25 (climbable).
struct Level {
    RigidWorld world{ single() };
    Level() {
        box(world, { -50, -1, -50 }, { 50, 0, 50 });
        box(world, { 4.9f, 0, -10 }, { 5.1f, 2, 10 });
        box(world, { 9.9f, 0, -10 }, { 10.1f, 1, 10 });
        box(world, { 20, 0, -10 }, { 25, 2, 10 });
    }
};

NetPlayerState at(float x, float y, float z = 0.0f) {
    NetPlayerState s;
    s.position = glm::vec3(x, y, z);
    return s;
}

// Walks `check` through `path` at 30 states a second; the worst verdict.
Verdict walk(WorldMoveCheck& check, const std::vector<glm::vec3>& path, uint8_t player = 1) {
    for (size_t i = 1; i < path.size(); ++i) {
        const Verdict v = check.check(player, at(path[i - 1].x, path[i - 1].y, path[i - 1].z), at(path[i].x, path[i].y, path[i].z), 1.0 / 30.0);
        if (v != Verdict::Ok) return v;
    }
    return Verdict::Ok;
}

} // namespace

TEST(WorldMoveCheck, WalkingJumpingAndFallingPass) {
    Level l;
    WorldMoveCheck c(l.world);
    std::vector<glm::vec3> path;
    for (int i = 0; i <= 60; ++i) path.push_back({ -10.0f + 0.2f * static_cast<float>(i), 0.0f, 0.0f }); // walk to x = 2
    EXPECT_EQ(walk(c, path), Verdict::Ok);
    // A jump: up 1.3 m and down again over 0.9 s.
    path.clear();
    for (int i = 0; i <= 27; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        path.push_back({ 0.0f, std::max(0.0f, 5.0f * t - 4.9f * t * t), -0.2f * static_cast<float>(i) });
    }
    EXPECT_EQ(walk(c, path), Verdict::Ok);
    // Off a 30 m drop: a long time in the air, but falling.
    path.clear();
    for (int i = 0; i <= 75; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        path.push_back({ -20.0f, std::max(0.0f, 30.0f - 4.9f * t * t), 0.0f });
    }
    EXPECT_EQ(walk(c, path), Verdict::Ok);
    EXPECT_EQ(c.throughWalls + c.flying, 0u);
}

TEST(WorldMoveCheck, ThroughAWallIsRefused) {
    Level l;
    WorldMoveCheck c(l.world);
    EXPECT_EQ(c.check(1, at(4.7f, 0), at(5.3f, 0), 1.0 / 30.0), Verdict::ThroughWall);
    EXPECT_EQ(c.throughWalls, 1u);
    // Brushing along it is fine.
    EXPECT_EQ(c.check(1, at(4.6f, 0, 0), at(4.6f, 0, 0.3f), 1.0 / 30.0), Verdict::Ok);
}

TEST(WorldMoveCheck, VaultsAndLedgeClimbsPass) {
    Level l;
    WorldMoveCheck c(l.world);
    // Over the 1 m wall at x = 10.
    EXPECT_EQ(walk(c, { { 9.4f, 0, 0 }, { 9.6f, 0.5f, 0 }, { 9.8f, 1.1f, 0 }, { 10.0f, 1.2f, 0 }, { 10.2f, 1.1f, 0 }, { 10.4f, 0.5f, 0 }, { 10.6f, 0, 0 } }),
              Verdict::Ok);
    // Up the face of the 2 m block at x = 20, then onto it.
    EXPECT_EQ(walk(c, { { 19.6f, 0, 0 }, { 19.6f, 0.7f, 0 }, { 19.6f, 1.4f, 0 }, { 19.6f, 2.0f, 0 }, { 19.9f, 2.0f, 0 }, { 20.3f, 2.0f, 0 } }),
              Verdict::Ok);
    // Hanging off its edge for five seconds: something to hold.
    std::vector<glm::vec3> hang(150, glm::vec3(19.65f, 0.9f, 0.0f));
    EXPECT_EQ(walk(c, hang), Verdict::Ok);
}

TEST(WorldMoveCheck, RisingOrHoveringInTheOpenIsFlying) {
    Level l;
    WorldMoveCheck c(l.world);
    std::vector<glm::vec3> up;
    for (int i = 0; i <= 60; ++i) up.push_back({ -30.0f, 0.1f * static_cast<float>(i), 0.0f }); // 3 m/s straight up, 6 m
    EXPECT_EQ(walk(c, up, 1), Verdict::Flying);

    WorldMoveCheck c2(l.world);
    std::vector<glm::vec3> hover;
    for (int i = 0; i <= 10; ++i) hover.push_back({ -30.0f, 0.12f * static_cast<float>(i), 0.0f }); // a jump's worth up
    for (int i = 0; i <= 120; ++i) hover.push_back({ -30.0f + 0.1f * static_cast<float>(i), 1.2f, 0.0f }); // then 4 s level
    EXPECT_EQ(walk(c2, hover, 1), Verdict::Flying);
    EXPECT_EQ(c2.flying, 1u);
}

TEST(WorldMoveCheck, CratesHoldYouUpPlayersDont) {
    Level l;
    const RigidWorld::BodyId crate = box(l.world, { -40.5f, 0, -0.5f }, { -39.5f, 1, 0.5f }, RigidWorld::Motion::Dynamic);
    const RigidWorld::BodyId capsule = box(l.world, { -35.3f, 2.0f, -0.3f }, { -34.7f, 3.8f, 0.3f }, RigidWorld::Motion::Kinematic);
    WorldMoveCheck c(l.world, [capsule](RigidWorld::BodyId b) { return b == capsule; });
    ASSERT_NE(crate, RigidWorld::kNoBody);
    // Standing on the crate for 4 s.
    std::vector<glm::vec3> onCrate(120, glm::vec3(-40.0f, 1.0f, 0.0f));
    EXPECT_EQ(walk(c, onCrate), Verdict::Ok);
    // "Standing" 2 m up inside your own stand-in capsule: still flying.
    std::vector<glm::vec3> inCapsule;
    for (int i = 0; i <= 20; ++i) inCapsule.push_back({ -35.0f, 0.1f * static_cast<float>(i), 0.0f });
    for (int i = 0; i <= 120; ++i) inCapsule.push_back({ -35.0f, 2.0f, 0.0f });
    EXPECT_EQ(walk(c, inCapsule, 2), Verdict::Flying);
}

TEST(WorldMoveCheck, AfterARefusalTheHonestFallIsAllowed) {
    Level l;
    WorldMoveCheck c(l.world);
    std::vector<glm::vec3> up;
    for (int i = 0; i <= 40; ++i) up.push_back({ -30.0f, 0.1f * static_cast<float>(i), 0.0f });
    ASSERT_EQ(walk(c, up), Verdict::Flying);
    // Sent back to ~3 m up: from there it drops like a stone to the ground.
    std::vector<glm::vec3> fall;
    for (int i = 0; i <= 24; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        fall.push_back({ -30.0f, std::max(0.0f, 3.0f - 4.9f * t * t), 0.0f });
    }
    EXPECT_EQ(walk(c, fall), Verdict::Ok);
}
