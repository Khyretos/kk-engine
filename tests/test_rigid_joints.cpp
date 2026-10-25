#include "kke/RigidWorld.h"

#include <gtest/gtest.h>

#include <cmath>

// Joints (ENG-F37): each kind does what a game expects of it, measured
// on a single-threaded world.
namespace {
using W = kke::RigidWorld;

W::Settings single() {
    W::Settings s;
    s.threads = 0;
    return s;
}
W::BodyId box(W& w, glm::vec3 pos, glm::vec3 half = glm::vec3(0.25f)) {
    W::BodyDesc d;
    d.halfExtents = half;
    d.position = pos;
    return w.add(d);
}
void run(W& w, float seconds) {
    for (int i = 0; i < int(seconds * 60.0f); ++i) w.step(1.0f / 60.0f);
}
} // namespace

TEST(RigidJoints, BallHoldsABoxUpToTheWorld) {
    W w(single());
    const W::BodyId b = box(w, glm::vec3(1.0f, 5.0f, 0.0f));
    W::JointDesc j;
    j.type = W::JointType::Ball;
    j.a = b;
    j.anchor = glm::vec3(0.0f, 5.0f, 0.0f); // a pendulum 1 m long
    const W::JointId id = w.addJoint(j);
    ASSERT_NE(id, 0u);
    run(w, 3.0f);
    EXPECT_NEAR(glm::length(w.position(b) - j.anchor), 1.0f, 0.05f);
    EXPECT_LT(w.position(b).y, 5.0f); // it swung down
    w.removeJoint(id);
    EXPECT_FALSE(w.hasJoint(id));
    run(w, 1.0f);
    EXPECT_LT(w.position(b).y, 0.0f); // nothing holds it now (no ground)
}

TEST(RigidJoints, HingeTurnsOnlyAboutItsAxisAndKeepsItsLimits) {
    W w(single());
    // A door: 1 m wide, hinged on its left edge about Y, gravity can't
    // turn it, so a motor does.
    const W::BodyId door = box(w, glm::vec3(0.5f, 1.0f, 0.0f), glm::vec3(0.5f, 1.0f, 0.05f));
    W::JointDesc j;
    j.type = W::JointType::Hinge;
    j.a = door;
    j.anchor = glm::vec3(0.0f, 1.0f, 0.0f);
    j.axis = glm::vec3(0, 1, 0);
    j.limited = true;
    j.min = -10.0f;
    j.max = 90.0f;
    const W::JointId id = w.addJoint(j);
    ASSERT_NE(id, 0u);
    EXPECT_NEAR(w.jointValue(id), 0.0f, 0.5f);
    ASSERT_TRUE(w.setJointMotor(id, W::JointMotor::Velocity, 120.0f, 200.0f));
    run(w, 2.0f);
    EXPECT_NEAR(w.jointValue(id), 90.0f, 3.0f); // stopped by the limit
    EXPECT_NEAR(w.position(door).y, 1.0f, 0.05f); // didn't sag
    ASSERT_TRUE(w.setJointMotor(id, W::JointMotor::Position, 30.0f));
    run(w, 2.0f);
    EXPECT_NEAR(w.jointValue(id), 30.0f, 3.0f);
}

TEST(RigidJoints, SliderMovesAlongItsAxisWithinLimits) {
    W w(single());
    const W::BodyId b = box(w, glm::vec3(0.0f, 3.0f, 0.0f));
    W::JointDesc j;
    j.type = W::JointType::Slider;
    j.a = b;
    j.anchor = w.position(b);
    j.axis = glm::vec3(0, 1, 0); // a lift
    j.limited = true;
    j.min = -1.0f;
    j.max = 2.0f;
    const W::JointId id = w.addJoint(j);
    ASSERT_NE(id, 0u);
    run(w, 2.0f); // falls to the bottom stop
    EXPECT_NEAR(w.jointValue(id), -1.0f, 0.05f);
    EXPECT_NEAR(w.position(b).y, 2.0f, 0.05f);
    EXPECT_NEAR(w.position(b).x, 0.0f, 0.01f);
    ASSERT_TRUE(w.setJointMotor(id, W::JointMotor::Position, 1.5f));
    run(w, 3.0f);
    EXPECT_NEAR(w.jointValue(id), 1.5f, 0.05f);
}

TEST(RigidJoints, SpringBouncesAndRopeOnlyPulls) {
    W w(single());
    // A spring 1 m long holding a box under a point: gravity stretches it
    // a little; a rope 2 m long lets its box fall 2 m and no further.
    const W::BodyId s = box(w, glm::vec3(0.0f, 4.0f, 0.0f));
    W::JointDesc sp;
    sp.type = W::JointType::Spring;
    sp.a = s;
    sp.anchor = w.position(s);
    sp.anchorB = glm::vec3(0.0f, 5.0f, 0.0f);
    sp.frequency = 2.0f;
    sp.damping = 0.5f;
    const W::JointId springId = w.addJoint(sp);
    ASSERT_NE(springId, 0u);
    EXPECT_NEAR(w.jointValue(springId), 1.0f, 0.01f);

    const W::BodyId r = box(w, glm::vec3(3.0f, 4.5f, 0.0f));
    W::JointDesc rope;
    rope.type = W::JointType::Spring;
    rope.a = r;
    rope.anchor = w.position(r);
    rope.anchorB = glm::vec3(3.0f, 5.0f, 0.0f);
    rope.limited = true;
    rope.min = 0.0f;
    rope.max = 2.0f;
    const W::JointId ropeId = w.addJoint(rope);
    ASSERT_NE(ropeId, 0u);
    run(w, 4.0f);
    // m g / k with k = m (2 pi f)^2: g / (2 pi 2)^2 = 6 cm of stretch.
    EXPECT_GT(w.jointValue(springId), 1.02f);
    EXPECT_LT(w.jointValue(springId), 1.15f);
    EXPECT_NEAR(w.jointValue(ropeId), 2.0f, 0.05f);
    EXPECT_NEAR(w.position(r).y, 3.0f, 0.05f);
}

TEST(RigidJoints, FixedWeldsTwoBodies) {
    W w(single());
    const W::BodyId a = box(w, glm::vec3(0.0f, 5.0f, 0.0f));
    const W::BodyId b = box(w, glm::vec3(0.6f, 5.0f, 0.0f));
    W::JointDesc j;
    j.type = W::JointType::Fixed;
    j.a = a;
    j.b = b;
    ASSERT_NE(w.addJoint(j), 0u);
    w.addImpulse(a, glm::vec3(0, 0, 5), w.position(a) + glm::vec3(0, 0.2f, 0));
    run(w, 1.0f);
    EXPECT_NEAR(glm::length(w.position(b) - w.position(a)), 0.6f, 0.02f);
}

TEST(RigidJoints, RefusesNonsenseAndGoesWithItsBody) {
    W w(single());
    const W::BodyId a = box(w, glm::vec3(0.0f, 5.0f, 0.0f));
    W::JointDesc j;
    j.type = W::JointType::Hinge;
    EXPECT_EQ(w.addJoint(j), 0u);         // no bodies
    j.a = a;
    j.b = a;
    EXPECT_EQ(w.addJoint(j), 0u);         // the same body twice
    j.b = W::kNoBody;
    j.axis = glm::vec3(0.0f);
    EXPECT_EQ(w.addJoint(j), 0u);         // no axis
    j.a = 12345;
    j.axis = glm::vec3(0, 1, 0);
    EXPECT_EQ(w.addJoint(j), 0u);         // unknown body
    j.a = a;
    const W::JointId id = w.addJoint(j);
    ASSERT_NE(id, 0u);
    EXPECT_FALSE(w.setJointMotor(999, W::JointMotor::Velocity, 1.0f));
    EXPECT_EQ(w.jointCount(), 1u);
    w.remove(a);
    EXPECT_FALSE(w.hasJoint(id));
    EXPECT_EQ(w.jointCount(), 0u);
    run(w, 0.1f); // and stepping after is fine
}
