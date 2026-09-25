#include "kke/Ragdoll.h"

#include "kke/AssetCatalog.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

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

TEST(Ragdoll, BuildsElevenBodiesTenJointsTwoHinges) {
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
    EXPECT_EQ(hinges, 2);
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
    for (size_t i = 0; i < d.joints.size(); ++i) if (d.joints[i].hinge) { knee = static_cast<int>(i); break; }
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
