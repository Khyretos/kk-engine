// Ragdolls on Jolt (RigidWorld::addRagdoll, issue #31): limits, limb
// self-collision, settling, cleanup.
#include "kke/ProceduralAnim.h"
#include "kke/Ragdoll.h"
#include "kke/RigidWorld.h"
#include "RagdollTestRigs.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {

kke::RigidWorld::Settings single() {
    kke::RigidWorld::Settings s;
    s.threads = 0;
    return s;
}

void ground(kke::RigidWorld& w) {
    kke::RigidWorld::BodyDesc g;
    g.motion = kke::RigidWorld::Motion::Static;
    g.halfExtents = glm::vec3(50.0f, 0.5f, 50.0f);
    g.position = glm::vec3(0.0f, -0.5f, 0.0f);
    ASSERT_NE(w.add(g), kke::RigidWorld::kNoBody);
}

// A standing humanoid (arms down), Synty bone names, feet at y = 0.
kke::ModelData standing() {
    kke::ModelData m;
    auto add = [&](const char* name, int parent, glm::vec3 offset) {
        kke::ModelBone b;
        b.name = name;
        b.parent = parent;
        b.localRest = glm::translate(glm::mat4(1.0f), offset);
        m.bones.push_back(b);
        return static_cast<int>(m.bones.size()) - 1;
    };
    int root = add("Root", -1, { 0, 0, 0 });
    int pelvis = add("Pelvis", root, { 0, 0.97f, 0 });
    int s1 = add("spine_01", pelvis, { 0, 0.1f, 0 });
    int s2 = add("spine_02", s1, { 0, 0.15f, 0 });
    int s3 = add("spine_03", s2, { 0, 0.15f, 0 });
    int neck = add("neck_01", s3, { 0, 0.15f, 0 });
    add("head", neck, { 0, 0.1f, 0 });
    for (int side : { 1, -1 }) {
        const bool left = side > 0;
        int ua = add(left ? "UpperArm_L" : "UpperArm_R", s3, { side * 0.2f, 0.05f, 0 });
        int la = add(left ? "lowerarm_l" : "lowerarm_r", ua, { side * 0.05f, -0.28f, 0 });
        add(left ? "Hand_L" : "Hand_R", la, { 0, -0.25f, 0.02f });
        int th = add(left ? "Thigh_L" : "Thigh_R", pelvis, { side * 0.1f, -0.05f, 0 });
        int ca = add(left ? "calf_l" : "calf_r", th, { 0, -0.45f, 0 });
        add(left ? "Foot_L" : "Foot_R", ca, { 0, -0.42f, 0 });
    }
    return m;
}

kke::RagdollDesc humanoid(const glm::vec3& at = glm::vec3(0.0f)) {
    kke::ModelData m = standing();
    auto world = kke::computeRestPose(m);
    for (auto& w : world) w = glm::translate(glm::mat4(1.0f), at) * w;
    return kke::buildHumanoidRagdoll(m, world);
}

int jointTo(const kke::RagdollDesc& d, const char* body) {
    for (size_t i = 0; i < d.joints.size(); ++i)
        if (d.joints[i].bodyB == d.findBody(body)) return static_cast<int>(i);
    return -1;
}

// World position of a joint's anchor as seen from body `b`.
glm::vec3 anchorOn(const kke::RagdollDesc& d, const std::vector<glm::mat4>& now, int joint, int b) {
    const glm::mat4& t0 = d.bodies[b].transform;
    const glm::vec3 local = glm::transpose(glm::mat3(t0)) * (d.joints[joint].anchor - glm::vec3(t0[3]));
    return glm::vec3(now[b] * glm::vec4(local, 1.0f));
}

} // namespace

TEST(JoltRagdoll, FallsSettlesAndStaysInOnePiece) {
    kke::RigidWorld w(single());
    ground(w);
    const kke::RagdollDesc d = humanoid(glm::vec3(0, 0.05f, 0));
    ASSERT_EQ(d.bodies.size(), 11u);
    const size_t before = w.bodyCount();
    auto id = w.addRagdoll(d, glm::vec3(0.0f));
    ASSERT_NE(id, 0u);
    EXPECT_EQ(w.bodyCount(), before + 11);
    EXPECT_EQ(w.ragdollCount(), 1u);
    // Shove the torso so it topples.
    const auto bodies = w.ragdollBodies(id);
    w.addImpulse(bodies[d.findBody("torso")], glm::vec3(0, 0, 60.0f), glm::vec3(d.bodies[d.findBody("torso")].transform[3]));
    for (int i = 0; i < 300; ++i) w.step(1.0f / 60.0f);
    std::vector<glm::mat4> now;
    ASSERT_TRUE(w.ragdollTransforms(id, now));
    for (size_t b = 0; b < now.size(); ++b) {
        EXPECT_GT(now[b][3].y, 0.0f) << d.bodies[b].name; // on the ground, not in it
        EXPECT_LT(now[b][3].y, 0.8f) << d.bodies[b].name; // fell over (maybe onto its knees)
        EXPECT_LT(glm::length(w.velocity(bodies[b])), 0.2f) << d.bodies[b].name;
    }
    EXPECT_LT(now[static_cast<size_t>(d.findBody("head"))][3].y, 0.5f);
    // Joints held: both bodies still agree where each anchor is.
    for (size_t j = 0; j < d.joints.size(); ++j) {
        const glm::vec3 a = anchorOn(d, now, static_cast<int>(j), d.joints[j].bodyA);
        const glm::vec3 b = anchorOn(d, now, static_cast<int>(j), d.joints[j].bodyB);
        EXPECT_LT(glm::distance(a, b), 0.03f) << "joint " << j;
    }
    w.removeRagdoll(id);
    EXPECT_EQ(w.bodyCount(), before);
    EXPECT_EQ(w.ragdollCount(), 0u);
    EXPECT_FALSE(w.ragdollTransforms(id, now));
    w.removeRagdoll(id); // twice is fine
}

TEST(JoltRagdoll, KneesBendOneWayOnly) {
    kke::RigidWorld w(single());
    const kke::RagdollDesc d = humanoid(glm::vec3(0, 5, 0));
    auto id = w.addRagdoll(d, glm::vec3(0.0f));
    ASSERT_NE(id, 0u);
    const int knee = jointTo(d, "calf_l");
    ASSERT_GE(knee, 0);
    ASSERT_TRUE(d.joints[knee].hinge);
    const auto bodies = w.ragdollBodies(id);
    const kke::RigidWorld::BodyId calf = bodies[d.findBody("calf_l")];
    // Kick the foot forward, hard, every step (+Z is in front: that would
    // hyperextend). A kick can overshoot the stop for a step; it doesn't give.
    for (int i = 0; i < 30; ++i) {
        w.addImpulse(calf, glm::vec3(0, 0, 3.0f), w.position(calf) + glm::vec3(0, -0.2f, 0));
        w.step(1.0f / 60.0f);
        EXPECT_GT(w.ragdollHingeAngle(id, knee), -12.0f) << "step " << i;
    }
    EXPECT_GT(w.ragdollHingeAngle(id, knee), -6.0f);
    w.removeRagdoll(id);
    // Backward: it bends, up to the limit.
    id = w.addRagdoll(d, glm::vec3(0.0f));
    const kke::RigidWorld::BodyId calf2 = w.ragdollBodies(id)[d.findBody("calf_l")];
    float most = 0.0f;
    for (int i = 0; i < 60; ++i) {
        w.addImpulse(calf2, glm::vec3(0, 0, -4.0f), w.position(calf2) + glm::vec3(0, -0.2f, 0));
        w.step(1.0f / 60.0f);
        most = std::max(most, w.ragdollHingeAngle(id, knee));
    }
    EXPECT_GT(most, 45.0f);
    EXPECT_LT(most, 155.0f);
}

TEST(JoltRagdoll, LimbsCollideWithEachOtherButNotAcrossAJoint) {
    kke::RigidWorld w(single());
    ground(w);
    // Two loose boxes of one ragdoll, one above the other: they stack.
    kke::RagdollDesc d;
    kke::RagdollBody low, high;
    low.name = "low";
    low.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.1f, 0));
    low.halfExtents = glm::vec3(0.3f, 0.1f, 0.3f);
    high = low;
    high.name = "high";
    high.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.0f, 0));
    d.bodies = { low, high };
    auto id = w.addRagdoll(d, glm::vec3(0.0f));
    ASSERT_NE(id, 0u);
    for (int i = 0; i < 120; ++i) w.step(1.0f / 60.0f);
    std::vector<glm::mat4> now;
    ASSERT_TRUE(w.ragdollTransforms(id, now));
    EXPECT_NEAR(now[1][3].y, 0.3f, 0.05f); // resting on the other one
    w.removeRagdoll(id);

    // Overlapping and joined (an arm against the torso): no explosion.
    high.transform = glm::translate(glm::mat4(1.0f), glm::vec3(0.05f, 0.15f, 0));
    d.bodies = { low, high };
    kke::RagdollJoint j;
    j.bodyA = 0;
    j.bodyB = 1;
    j.anchor = glm::vec3(0.0f, 0.12f, 0.0f);
    d.joints = { j };
    id = w.addRagdoll(d, glm::vec3(0.0f));
    ASSERT_NE(id, 0u);
    float fastest = 0.0f;
    for (int i = 0; i < 60; ++i) {
        w.step(1.0f / 60.0f);
        for (auto b : w.ragdollBodies(id)) fastest = std::max(fastest, glm::length(w.velocity(b)));
    }
    EXPECT_LT(fastest, 1.0f);
}

TEST(JoltRagdoll, BadDescriptionsAreRefused) {
    kke::RigidWorld w(single());
    EXPECT_EQ(w.addRagdoll(kke::RagdollDesc{}, glm::vec3(0.0f)), 0u);
    kke::RagdollDesc d;
    d.bodies.resize(1);
    kke::RagdollJoint j;
    j.bodyA = 0;
    j.bodyB = 3; // no such body
    d.joints = { j };
    const size_t before = w.bodyCount();
    EXPECT_EQ(w.addRagdoll(d, glm::vec3(0.0f)), 0u);
    EXPECT_EQ(w.bodyCount(), before);
    EXPECT_FLOAT_EQ(w.ragdollHingeAngle(99, 0), 0.0f);
    EXPECT_TRUE(w.ragdollBodies(99).empty());
}

TEST(JoltRagdoll, ThrownRagdollKeepsItsVelocity) {
    kke::RigidWorld w(single());
    const kke::RagdollDesc d = humanoid(glm::vec3(0, 10, 0));
    auto id = w.addRagdoll(d, glm::vec3(3.0f, 0.0f, 0.0f));
    ASSERT_NE(id, 0u);
    w.step(1.0f / 60.0f);
    for (kke::RigidWorld::BodyId b : w.ragdollBodies(id)) EXPECT_NEAR(w.velocity(b).x, 3.0f, 0.3f);
}

namespace {

// Forward/back angle of the left thigh in the pelvis's frame (0 = straight
// down as built, positive = forward).
float thighSwing(const kke::RagdollDesc& d, const std::vector<glm::mat4>& now) {
    const int pelvis = d.findBody("pelvis"), thigh = d.findBody("thigh_l");
    const glm::mat3 p0(d.bodies[pelvis].transform), p(now[pelvis]);
    const glm::vec3 local = glm::transpose(p) * glm::normalize(glm::vec3(now[thigh][1]));
    const glm::vec3 down = glm::transpose(p0) * glm::vec3(0, -1, 0), fwd = glm::transpose(p0) * glm::vec3(0, 0, 1);
    return glm::degrees(std::atan2(glm::dot(local, fwd), glm::dot(local, down)));
}

float kickThigh(const kke::RagdollDesc& d, float impulse, bool limited = true) {
    kke::RigidWorld w(single());
    kke::RagdollDesc desc = d;
    if (!limited) desc.findJoint("hip_l")->limited = false;
    auto id = w.addRagdoll(desc, glm::vec3(0.0f));
    EXPECT_NE(id, 0u);
    // Hold the pelvis still, so only the hip moves.
    w.setMotion(w.ragdollBodies(id)[desc.findBody("pelvis")], kke::RigidWorld::Motion::Kinematic);
    const kke::RigidWorld::BodyId thigh = w.ragdollBodies(id)[desc.findBody("thigh_l")];
    float most = 0.0f;
    std::vector<glm::mat4> now;
    for (int i = 0; i < 60; ++i) {
        w.addImpulse(thigh, glm::vec3(0, 0, impulse), w.position(thigh) + glm::vec3(0, -0.2f, 0));
        w.step(1.0f / 60.0f);
        w.ragdollTransforms(id, now);
        const float a = thighSwing(desc, now);
        most = impulse > 0.0f ? std::max(most, a) : std::min(most, a);
    }
    return most;
}

} // namespace

// A hip flexes ~120 degrees forward but extends only ~20 back.
TEST(JoltRagdoll, HipsSwingFarForwardButLittleBack) {
    const kke::RagdollDesc d = humanoid(glm::vec3(0, 5, 0));
    const float forward = kickThigh(d, 3.0f);
    const float back = kickThigh(d, -3.0f);
    EXPECT_GT(forward, 100.0f);
    EXPECT_LT(forward, 135.0f);
    EXPECT_LT(back, -10.0f); // it does move back
    EXPECT_GT(back, -30.0f); // but not far
    // An override with no limits swings well past that.
    EXPECT_LT(kickThigh(d, -3.0f, false), -60.0f);
}

TEST(JoltRagdoll, QuadrupedFallsSettlesAndItsKneesStayInRange) {
    kke::RigidWorld w(single());
    ground(w);
    kke::ModelData m = kke_test::quadrupedSkeleton();
    auto world = kke::computeRestPose(m);
    // Horse.fbx is ~6 units tall: scale to a 1.7 m horse, 1 m up.
    const glm::mat4 place = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1.0f, 0)) * glm::scale(glm::mat4(1.0f), glm::vec3(0.28f));
    for (auto& b : world) b = place * b;
    const kke::RagdollDesc d = kke::buildQuadrupedRagdoll(m, world, 500.0f);
    ASSERT_EQ(d.bodies.size(), 13u);
    auto id = w.addRagdoll(d, glm::vec3(0.0f));
    ASSERT_NE(id, 0u);
    const auto bodies = w.ragdollBodies(id);
    // Knock it over sideways.
    w.addImpulse(bodies[d.findBody("chest")], glm::vec3(700.0f, 0, 0), glm::vec3(d.bodies[d.findBody("chest")].transform[3]) + glm::vec3(0, 0.3f, 0));
    for (int i = 0; i < 360; ++i) {
        w.step(1.0f / 60.0f);
        for (size_t j = 0; j < d.joints.size(); ++j) {
            if (!d.joints[j].hinge) continue;
            const float a = w.ragdollHingeAngle(id, static_cast<int>(j));
            EXPECT_GT(a, d.joints[j].hingeMinDegrees - 8.0f) << d.joints[j].name << " step " << i;
            EXPECT_LT(a, d.joints[j].hingeMaxDegrees + 8.0f) << d.joints[j].name << " step " << i;
        }
    }
    std::vector<glm::mat4> now;
    ASSERT_TRUE(w.ragdollTransforms(id, now));
    for (size_t b = 0; b < now.size(); ++b) {
        EXPECT_GT(now[b][3].y, 0.0f) << d.bodies[b].name;
        EXPECT_LT(glm::length(w.velocity(bodies[b])), 0.3f) << d.bodies[b].name;
    }
    for (size_t j = 0; j < d.joints.size(); ++j) {
        const glm::vec3 a = anchorOn(d, now, static_cast<int>(j), d.joints[j].bodyA);
        const glm::vec3 b = anchorOn(d, now, static_cast<int>(j), d.joints[j].bodyB);
        EXPECT_LT(glm::distance(a, b), 0.05f) << d.joints[j].name;
    }
}

TEST(RigidWorld, MassOverridesDensity) {
    kke::RigidWorld w(single());
    kke::RigidWorld::BodyDesc d;
    d.halfExtents = glm::vec3(0.5f);
    d.mass = 2.0f; // a 1 m cube of water would be 1000 kg
    auto b = w.add(d);
    ASSERT_NE(b, kke::RigidWorld::kNoBody);
    w.addImpulse(b, glm::vec3(4.0f, 0, 0), w.position(b));
    EXPECT_NEAR(w.velocity(b).x, 2.0f, 1e-3f);
}

// Active ragdolls (kke/ProceduralAnim.h): joint motors toward a pose.
TEST(JoltRagdoll, MotorsHoldAJointAtItsTarget) {
    kke::RigidWorld w(single());
    const kke::RagdollDesc d = humanoid(glm::vec3(0, 20, 0)); // falling freely: only the motors bend it
    auto id = w.addRagdoll(d, glm::vec3(0.0f));
    ASSERT_NE(id, 0u);
    const int knee = jointTo(d, "calf_l");
    ASSERT_GE(knee, 0);
    const kke::RagdollJoint& j = d.joints[knee];
    kke::RagdollDrive drive;
    for (const auto& b : d.bodies) drive.targets.push_back(b.transform);
    drive.jointStrength.assign(d.joints.size(), 1.0f);
    // The calf bent 60 degrees about the knee.
    const glm::mat4 bend = glm::translate(glm::mat4(1.0f), j.anchor) * glm::mat4_cast(glm::angleAxis(glm::radians(60.0f), glm::normalize(j.hingeAxis))) *
                           glm::translate(glm::mat4(1.0f), -j.anchor);
    drive.targets[j.bodyB] = bend * drive.targets[j.bodyB];
    for (int i = 0; i < 90; ++i) {
        ASSERT_TRUE(w.driveRagdoll(id, drive));
        w.step(1.0f / 60.0f);
    }
    EXPECT_NEAR(w.ragdollHingeAngle(id, knee), 60.0f, 8.0f);
    // Strength 0: the motor lets go (and a mismatched drive is refused).
    drive.jointStrength.assign(d.joints.size(), 0.0f);
    EXPECT_TRUE(w.driveRagdoll(id, drive));
    drive.jointStrength.pop_back();
    EXPECT_FALSE(w.driveRagdoll(id, drive));
    EXPECT_FALSE(w.driveRagdoll(12345, drive));
}

TEST(JoltRagdoll, ActiveRagdollStaysStandingAndLimpOneFalls) {
    for (bool active : { true, false }) {
        kke::RigidWorld w(single());
        ground(w);
        const kke::RagdollDesc d = humanoid(glm::vec3(0, 0.02f, 0));
        auto id = w.addRagdoll(d, glm::vec3(0.0f));
        ASSERT_NE(id, 0u);
        kke::ActiveRagdoll a(d);
        std::vector<glm::mat4> start;
        for (const auto& b : d.bodies) start.push_back(b.transform);
        a.setTargets(start);
        // A light shove, so it goes Active and has something to recover from.
        const int torso = d.findBody("torso");
        a.hit(torso, glm::vec3(0, 0, 1.0f));
        w.addVelocity(w.ragdollBodies(id)[torso], glm::vec3(0, 0, 1.0f));
        std::vector<glm::mat4> now;
        for (int i = 0; i < 180; ++i) {
            ASSERT_TRUE(w.ragdollTransforms(id, now));
            a.update(1.0f / 60.0f, now);
            if (active && a.physical()) w.driveRagdoll(id, a.drive());
            w.step(1.0f / 60.0f);
        }
        ASSERT_TRUE(w.ragdollTransforms(id, now));
        const float head = now[static_cast<size_t>(d.findBody("head"))][3].y;
        if (active) {
            EXPECT_GT(head, 1.4f) << "the active ragdoll should still be standing";
            EXPECT_EQ(a.state(), kke::ActiveRagdoll::State::Animated); // recovered and handed back
        } else {
            EXPECT_LT(head, 0.8f) << "without motors it should have fallen";
        }
    }
}
