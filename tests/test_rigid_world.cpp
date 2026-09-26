#include "kke/RigidWorld.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>

namespace {
kke::RigidWorld::BodyId ground(kke::RigidWorld& w) {
    kke::RigidWorld::BodyDesc g;
    g.shape = kke::RigidWorld::Shape::Box;
    g.motion = kke::RigidWorld::Motion::Static;
    g.halfExtents = glm::vec3(50.0f, 0.5f, 50.0f);
    g.position = glm::vec3(0.0f, -0.5f, 0.0f);
    return w.add(g);
}
kke::RigidWorld::Settings single() {
    kke::RigidWorld::Settings s;
    s.threads = 0; // deterministic, and the 1-core floor
    return s;
}
} // namespace

TEST(RigidWorld, BoxFallsAndRestsOnTheGround) {
    kke::RigidWorld w(single());
    ground(w);
    kke::RigidWorld::BodyDesc d;
    d.halfExtents = glm::vec3(0.5f);
    d.position = glm::vec3(0.0f, 5.0f, 0.0f);
    auto box = w.add(d);
    ASSERT_NE(box, kke::RigidWorld::kNoBody);
    for (int i = 0; i < 240; ++i) w.step(1.0f / 60.0f);
    EXPECT_NEAR(w.position(box).y, 0.5f, 0.05f);
    EXPECT_LT(glm::length(w.velocity(box)), 0.05f);
    auto contacts = w.takeContacts();
    EXPECT_FALSE(contacts.empty()); // the landing was reported
}

TEST(RigidWorld, RaycastHitsTheGround) {
    kke::RigidWorld w(single());
    auto g = ground(w);
    auto hit = w.raycast(glm::vec3(1.0f, 10.0f, 2.0f), glm::vec3(0.0f, -1.0f, 0.0f), 100.0f);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.body, g);
    EXPECT_NEAR(hit.point.y, 0.0f, 1e-3f);
    EXPECT_NEAR(hit.normal.y, 1.0f, 1e-3f);
    EXPECT_FALSE(w.raycast(glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 100.0f).hit);
}

TEST(RigidWorld, StaticTriangleMeshCollides) {
    kke::RigidWorld w(single());
    kke::RigidWorld::BodyDesc m;
    m.shape = kke::RigidWorld::Shape::Mesh;
    m.motion = kke::RigidWorld::Motion::Static;
    m.points = { { -5, 0, -5 }, { 5, 0, -5 }, { 5, 0, 5 }, { -5, 0, 5 } };
    m.indices = { 0, 2, 1, 0, 3, 2 };
    w.add(m);
    kke::RigidWorld::BodyDesc s;
    s.shape = kke::RigidWorld::Shape::Sphere;
    s.radius = 0.25f;
    s.position = glm::vec3(0.0f, 3.0f, 0.0f);
    auto ball = w.add(s);
    for (int i = 0; i < 240; ++i) w.step(1.0f / 60.0f);
    EXPECT_NEAR(w.position(ball).y, 0.25f, 0.05f);
}

TEST(RigidWorld, CharacterWalksJumpsAndClimbsAStep) {
    kke::RigidWorld w(single());
    ground(w);
    // A 0.25 m step at x = 3.
    kke::RigidWorld::BodyDesc step;
    step.motion = kke::RigidWorld::Motion::Static;
    step.halfExtents = glm::vec3(2.0f, 0.125f, 2.0f);
    step.position = glm::vec3(5.0f, 0.125f, 0.0f);
    w.add(step);
    kke::RigidWorld::CharacterDesc cd;
    cd.position = glm::vec3(0.0f, 0.05f, 0.0f);
    auto ch = w.addCharacter(cd);
    for (int i = 0; i < 30; ++i) w.step(1.0f / 60.0f);
    EXPECT_TRUE(w.characterOnGround(ch));
    // Walk +x at 3 m/s for 2 s: up onto the step.
    kke::RigidWorld::CharacterInput in;
    in.move = glm::vec3(3.0f, 0.0f, 0.0f);
    for (int i = 0; i < 120; ++i) { w.setCharacterInput(ch, in); w.step(1.0f / 60.0f); }
    glm::vec3 p = w.characterPosition(ch);
    EXPECT_GT(p.x, 4.0f);
    EXPECT_NEAR(p.y, 0.25f, 0.06f); // standing on the step
    // Jump.
    in.move = glm::vec3(0.0f);
    in.jump = true;
    w.setCharacterInput(ch, in);
    w.step(1.0f / 60.0f);
    float peak = 0.0f;
    in.jump = false;
    for (int i = 0; i < 60; ++i) { w.setCharacterInput(ch, in); w.step(1.0f / 60.0f); peak = std::max(peak, w.characterPosition(ch).y); }
    EXPECT_GT(peak, 1.0f);
    EXPECT_TRUE(w.characterOnGround(ch));
}

TEST(RigidWorld, CrouchingFitsUnderALowCeilingAndStandingUpThereIsRefused) {
    kke::RigidWorld w(single());
    ground(w);
    // A beam from 1.3 m up, over x = 2..4: a 1.8 m capsule can't pass.
    kke::RigidWorld::BodyDesc beam;
    beam.motion = kke::RigidWorld::Motion::Static;
    beam.halfExtents = glm::vec3(1.0f, 0.2f, 2.0f);
    beam.position = glm::vec3(3.0f, 1.5f, 0.0f);
    w.add(beam);
    auto walk = [&](kke::RigidWorld::CharacterId ch) {
        kke::RigidWorld::CharacterInput in;
        in.move = glm::vec3(3.0f, 0.0f, 0.0f);
        for (int i = 0; i < 60; ++i) { w.setCharacterInput(ch, in); w.step(1.0f / 60.0f); }
        in.move = glm::vec3(0.0f);
        w.setCharacterInput(ch, in);
        return w.characterPosition(ch).x;
    };
    kke::RigidWorld::CharacterDesc cd;
    cd.position = glm::vec3(0.0f, 0.05f, 0.0f);
    auto standing = w.addCharacter(cd);
    EXPECT_LT(walk(standing), 2.0f); // blocked by the beam
    w.removeCharacter(standing);

    auto ch = w.addCharacter(cd);
    ASSERT_TRUE(w.setCharacterHeight(ch, 1.0f));
    EXPECT_FLOAT_EQ(w.characterHeight(ch), 1.0f);
    const float x = walk(ch);
    EXPECT_GT(x, 2.5f);
    EXPECT_LT(x, 3.5f); // under the beam now
    EXPECT_FALSE(w.setCharacterHeight(ch, 1.8f)); // no room to stand
    EXPECT_FLOAT_EQ(w.characterHeight(ch), 1.0f);
    // Walk out, then standing works.
    walk(ch);
    EXPECT_TRUE(w.setCharacterHeight(ch, 1.8f));
}

TEST(RigidWorld, CharacterPushesADynamicBox) {
    kke::RigidWorld w(single());
    ground(w);
    kke::RigidWorld::BodyDesc d;
    d.halfExtents = glm::vec3(0.3f);
    d.density = 100.0f; // a light crate
    d.position = glm::vec3(1.2f, 0.3f, 0.0f);
    auto crate = w.add(d);
    kke::RigidWorld::CharacterDesc cd;
    cd.position = glm::vec3(0.0f, 0.02f, 0.0f);
    auto ch = w.addCharacter(cd);
    kke::RigidWorld::CharacterInput in;
    in.move = glm::vec3(2.0f, 0.0f, 0.0f);
    for (int i = 0; i < 120; ++i) { w.setCharacterInput(ch, in); w.step(1.0f / 60.0f); }
    EXPECT_GT(w.position(crate).x, 1.6f);
}

TEST(RigidWorld, ManyBodiesSleepWhenSettled) {
    kke::RigidWorld w(single());
    ground(w);
    for (int i = 0; i < 100; ++i) {
        kke::RigidWorld::BodyDesc d;
        d.halfExtents = glm::vec3(0.25f);
        d.position = glm::vec3((i % 10) * 1.2f - 6.0f, 1.0f + 0.1f * (i % 3), (i / 10) * 1.2f - 6.0f);
        w.add(d);
    }
    for (int i = 0; i < 600; ++i) w.step(1.0f / 60.0f);
    EXPECT_EQ(w.bodyCount(), 101u);
    EXPECT_LT(w.activeBodyCount(), 10u); // settled piles sleep
}

// The scaling claim in SCALING.md: rigid bodies cost microseconds each.
// 1,000 boxes raining down, one thread: the busiest step stays well
// inside a 60 Hz frame. (Generous bound: CI machines vary.)
TEST(RigidWorld, AThousandFallingBoxesFitAFrameOnOneThread) {
    kke::RigidWorld w(single());
    ground(w);
    for (int i = 0; i < 1000; ++i) {
        kke::RigidWorld::BodyDesc d;
        d.halfExtents = glm::vec3(0.2f);
        d.position = glm::vec3((i % 32) * 0.6f - 9.6f, 2.0f + (i / 32) * 0.5f, ((i * 7) % 13) * 0.5f - 3.0f);
        w.add(d);
    }
    double worst = 0.0, total = 0.0;
    for (int i = 0; i < 180; ++i) {
        w.step(1.0f / 60.0f);
        worst = std::max(worst, w.lastStepMs());
        total += w.lastStepMs();
    }
    std::printf("1000 boxes, 1 thread: avg %.2f ms, worst %.2f ms per step\n", total / 180.0, worst);
    EXPECT_LT(total / 180.0, 16.0);
}
