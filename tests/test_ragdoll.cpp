#include "kke/Ragdoll.h"

#include "kke/AssetCatalog.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <filesystem>

namespace {

// A minimal humanoid in a T-pose, bone names as in Synty rigs. Each bone
// is a pure translation from its parent, so world transforms are easy to
// reason about.
kke::ModelData tPoseSkeleton() {
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
    int pelvis = add("Pelvis", root, { 0, 1.0f, 0 });
    int s1 = add("spine_01", pelvis, { 0, 0.1f, 0 });
    int s2 = add("spine_02", s1, { 0, 0.15f, 0 });
    int s3 = add("spine_03", s2, { 0, 0.15f, 0 });
    int neck = add("neck_01", s3, { 0, 0.15f, 0 });
    add("head", neck, { 0, 0.1f, 0 });
    for (int side : { 1, -1 }) {
        bool left = side > 0;
        int cl = add(left ? "clavicle_l" : "clavicle_r", s3, { side * 0.05f, 0.1f, 0 });
        int ua = add(left ? "UpperArm_L" : "UpperArm_R", cl, { side * 0.12f, 0, 0 });
        int la = add(left ? "lowerarm_l" : "lowerarm_r", ua, { side * 0.28f, 0, 0 });
        int hand = add(left ? "Hand_L" : "Hand_R", la, { side * 0.25f, 0, 0 });
        add(left ? "finger_01_l" : "finger_01_r", hand, { side * 0.06f, 0, 0 });
        int th = add(left ? "Thigh_L" : "Thigh_R", pelvis, { side * 0.1f, -0.05f, 0 });
        int ca = add(left ? "calf_l" : "calf_r", th, { 0, -0.45f, 0 });
        add(left ? "Foot_L" : "Foot_R", ca, { 0, -0.42f, 0 });
    }
    return m;
}

std::vector<glm::mat4> worldOf(const kke::ModelData& m, const glm::mat4& instance = glm::mat4(1.0f)) {
    auto rest = kke::computeRestPose(m);
    for (auto& w : rest) w = instance * w;
    return rest;
}

float maxError(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    float e = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        for (int c = 0; c < 4; ++c) e = std::max(e, glm::length(a[i][c] - b[i][c]));
    return e;
}

} // namespace

TEST(Ragdoll, BuildsElevenBodiesTenJointsFourHinges) {
    kke::ModelData m = tPoseSkeleton();
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, worldOf(m), 70.0f);
    EXPECT_EQ(d.bodies.size(), 11u);
    EXPECT_EQ(d.joints.size(), 10u);
    int hinges = 0;
    for (const auto& j : d.joints) {
        hinges += j.hinge;
        EXPECT_GE(j.bodyA, 0);
        EXPECT_GE(j.bodyB, 0);
        EXPECT_NE(j.bodyA, j.bodyB);
    }
    EXPECT_EQ(hinges, 4); // knees and elbows
    float mass = 0.0f;
    for (const auto& b : d.bodies) {
        mass += b.mass;
        EXPECT_GT(b.halfExtents.x, 0.0f);
        EXPECT_GT(b.halfExtents.y, 0.0f);
        // rotation part is orthonormal
        glm::mat3 r(b.transform);
        EXPECT_NEAR(glm::determinant(r), 1.0f, 1e-4f);
    }
    EXPECT_GT(mass, 60.0f);
    EXPECT_LE(mass, 70.0f);
}

TEST(Ragdoll, JointAnchorsSitAtTheBones) {
    kke::ModelData m = tPoseSkeleton();
    auto world = worldOf(m);
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, world);
    int knee = -1;
    for (size_t i = 0; i < d.joints.size(); ++i)
        if (d.joints[i].hinge && d.joints[i].bodyB == d.findBody("calf_l")) { knee = static_cast<int>(i); break; }
    ASSERT_GE(knee, 0);
    glm::vec3 calf = glm::vec3(world[m.findBone("calf_l")][3]);
    glm::vec3 calfR = glm::vec3(world[m.findBone("calf_r")][3]);
    float d0 = std::min(glm::distance(d.joints[knee].anchor, calf), glm::distance(d.joints[knee].anchor, calfR));
    EXPECT_NEAR(d0, 0.0f, 1e-5f);
    // knee hinge axis is the character's left-right axis
    EXPECT_NEAR(std::abs(d.joints[knee].hingeAxis.x), 1.0f, 1e-4f);
}

TEST(Ragdoll, BindThenPoseAtBindTransformsIsIdentity) {
    kke::ModelData m = tPoseSkeleton();
    glm::mat4 instance = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(3, 0, -2)), 0.7f, glm::vec3(0, 1, 0));
    auto world = worldOf(m, instance);
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, world);
    kke::RagdollSkinBinding bind = kke::bindSkeletonToRagdoll(m, world, d);
    std::vector<glm::mat4> bodies;
    for (const auto& b : d.bodies) bodies.push_back(b.transform);
    auto model = kke::poseFromRagdoll(m, bind, bodies, glm::inverse(instance));
    EXPECT_LT(maxError(model, worldOf(m)), 1e-4f); // back in model space, unchanged
}

TEST(Ragdoll, MovingABodyMovesOnlyItsBonesAndChildren) {
    kke::ModelData m = tPoseSkeleton();
    auto world = worldOf(m);
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, world);
    kke::RagdollSkinBinding bind = kke::bindSkeletonToRagdoll(m, world, d);
    std::vector<glm::mat4> bodies;
    for (const auto& b : d.bodies) bodies.push_back(b.transform);
    int la = d.findBody("lowerarm_l");
    ASSERT_GE(la, 0);
    bodies[la] = glm::translate(glm::mat4(1.0f), glm::vec3(0, -0.5f, 0)) * bodies[la];
    auto posed = kke::poseFromRagdoll(m, bind, bodies, glm::mat4(1.0f));
    auto delta = [&](const char* bone) { int b = m.findBone(bone); return glm::vec3(posed[b][3]) - glm::vec3(world[b][3]); };
    EXPECT_NEAR(glm::length(delta("lowerarm_l") - glm::vec3(0, -0.5f, 0)), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::length(delta("Hand_L") - glm::vec3(0, -0.5f, 0)), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::length(delta("finger_01_l") - glm::vec3(0, -0.5f, 0)), 0.0f, 1e-5f); // unmapped: follows parent
    EXPECT_NEAR(glm::length(delta("UpperArm_L")), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::length(delta("Hand_R")), 0.0f, 1e-5f);
}

TEST(Ragdoll, MissingBoneReportsWhich) {
    kke::ModelData m = tPoseSkeleton();
    m.bones[m.findBone("calf_r")].name = "shin_right";
    std::string missing;
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, worldOf(m), 70.0f, &missing);
    EXPECT_TRUE(d.bodies.empty());
    EXPECT_EQ(missing, "calf_r");
}

TEST(Ragdoll, SyntyCharacterIfInstalled) {
    auto catalog = kke::AssetCatalog::scan((std::filesystem::path(KKE_SOURCE_DIR) / "assets/synty").string());
    const kke::CatalogAsset* dummy = catalog.find("SK_Character_Dummy_Male_01");
    if (!dummy) GTEST_SKIP() << "Synty Prototype pack not installed in assets/synty";
    std::filesystem::path file = dummy->path;
    kke::ModelData m = kke::loadModel(file.string());
    auto world = kke::computeRestPose(m);
    std::string missing;
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, world, 70.0f, &missing);
    ASSERT_FALSE(d.bodies.empty()) << "missing bone " << missing;
    kke::RagdollSkinBinding bind = kke::bindSkeletonToRagdoll(m, world, d);
    std::vector<glm::mat4> bodies;
    for (const auto& b : d.bodies) bodies.push_back(b.transform);
    EXPECT_LT(maxError(kke::poseFromRagdoll(m, bind, bodies, glm::mat4(1.0f)), world), 1e-3f);
    for (const auto& b : d.bodies) EXPECT_GT(b.transform[3][1], 0.0f) << b.name; // everything above the floor
}

TEST(Ragdoll, JointLimitsFollowTheBuildPose) {
    kke::ModelData m = tPoseSkeleton();
    kke::RagdollDesc d = kke::buildHumanoidRagdoll(m, worldOf(m));
    const auto jointTo = [&](const char* body) -> const kke::RagdollJoint& {
        for (const auto& j : d.joints)
            if (j.bodyB == d.findBody(body)) return j;
        return d.joints.front();
    };
    // Straight legs and arms: bend up to the maximum, a hair past straight.
    const kke::RagdollJoint& knee = jointTo("calf_l");
    ASSERT_TRUE(knee.hinge);
    EXPECT_NEAR(knee.hingeMinDegrees, -3.0f, 1e-3f);
    EXPECT_NEAR(knee.hingeMaxDegrees, 150.0f, 1e-3f);
    // Positive knee rotation (right hand rule) swings the foot backward (-Z
    // is behind this skeleton: forward = lateral x up = +X x +Y = +Z).
    const glm::vec3 down(0, -1, 0);
    const glm::vec3 bent = glm::angleAxis(glm::radians(30.0f), knee.hingeAxis) * down;
    EXPECT_LT(bent.z, -0.4f);
    const kke::RagdollJoint& elbow = jointTo("lowerarm_l");
    ASSERT_TRUE(elbow.hinge);
    const glm::vec3 forearm = glm::angleAxis(glm::radians(30.0f), elbow.hingeAxis) * glm::vec3(1, 0, 0);
    EXPECT_GT(forearm.z, 0.4f); // the hand comes forward
    // A knee already bent 40 degrees keeps the same range around straight.
    auto world = worldOf(m);
    for (const char* bone : { "calf_l" }) {
        const int c = m.findBone(bone);
        const glm::vec3 knee0(world[c][3]);
        const int foot = m.findBone("Foot_L");
        const glm::vec3 shin = glm::vec3(world[foot][3]) - knee0;
        world[foot][3] = glm::vec4(knee0 + glm::angleAxis(glm::radians(40.0f), glm::vec3(1, 0, 0)) * shin, 1.0f);
    }
    kke::RagdollDesc bentDesc = kke::buildHumanoidRagdoll(m, world);
    for (const auto& j : bentDesc.joints) {
        if (j.bodyB != bentDesc.findBody("calf_l")) continue;
        EXPECT_NEAR(j.hingeMinDegrees, -43.0f, 0.5f);
        EXPECT_NEAR(j.hingeMaxDegrees, 110.0f, 0.5f);
    }
    // Ball joints get a cone and a twist.
    const kke::RagdollJoint& hip = jointTo("thigh_l");
    EXPECT_FALSE(hip.hinge);
    EXPECT_GT(hip.swingDegrees, 0.0f);
    EXPECT_GT(hip.twistDegrees, 0.0f);
    EXPECT_NEAR(hip.swingAxis.y, -1.0f, 1e-4f);
}

TEST(Ragdoll, BlendPosesGoesFromOneToTheOther) {
    std::vector<glm::mat4> a{ glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) };
    std::vector<glm::mat4> b{ glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(2, 1, 0)), glm::radians(90.0f), glm::vec3(0, 1, 0)) };
    EXPECT_LT(maxError(kke::blendPoses(a, b, 0.0f), a), 1e-5f);
    EXPECT_LT(maxError(kke::blendPoses(a, b, 1.0f), b), 1e-5f);
    EXPECT_LT(maxError(kke::blendPoses(a, b, 7.0f), b), 1e-5f); // clamped
    auto half = kke::blendPoses(a, b, 0.5f);
    EXPECT_NEAR(half[0][3].x, 1.0f, 1e-5f);
    const glm::vec3 x = glm::normalize(glm::vec3(half[0][0]));
    EXPECT_NEAR(std::acos(glm::clamp(x.x, -1.0f, 1.0f)), glm::radians(45.0f), 1e-3f); // half the turn
    // Scale comes from the target; extra bones come from the target too.
    b[0] = glm::scale(b[0], glm::vec3(2.0f));
    b.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(5, 5, 5)));
    auto scaled = kke::blendPoses(a, b, 0.5f);
    ASSERT_EQ(scaled.size(), 2u);
    EXPECT_NEAR(glm::length(glm::vec3(scaled[0][0])), 2.0f, 1e-4f);
    EXPECT_NEAR(scaled[1][3].y, 5.0f, 1e-6f);
}

TEST(Ragdoll, BlendWeightEasesInAndOut) {
    EXPECT_FLOAT_EQ(kke::blendWeight(0.0f, 0.5f), 0.0f);
    EXPECT_FLOAT_EQ(kke::blendWeight(0.25f, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(kke::blendWeight(1.0f, 0.5f), 1.0f);
    EXPECT_FLOAT_EQ(kke::blendWeight(0.1f, 0.0f), 1.0f);
    EXPECT_LT(kke::blendWeight(0.05f, 0.5f), 0.05f); // slow start
}
