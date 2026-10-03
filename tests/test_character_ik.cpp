#include "kke/CharacterIk.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>

// kke::CharacterIk on the UAL mannequin (CC0, shipped in assets/animations):
// hands on an edge overhead, a foot planted on a wall, the lean into
// speeding up, contacts fading back to the clip.

namespace {

using kke::CharacterIk;
using kke::ModelData;
using kke::Pose;

const ModelData& ual() {
    static const ModelData m = [] {
        namespace fs = std::filesystem;
        const fs::path p = fs::path(KKE_SOURCE_DIR) / "assets/animations/UAL1_Standard.fbx";
        return fs::exists(p) ? kke::loadModel(p.string()) : ModelData{};
    }();
    return m;
}

#define NEED_UAL()                                                            \
    if (ual().bones.empty() || ual().meshes.empty()) GTEST_SKIP() << "assets/animations/UAL1_Standard.fbx missing"

Pose idlePose(const kke::AnimationSet& set) {
    Pose p = set.restPose();
    for (size_t i = 0; i < ual().animations.size(); ++i)
        if (const std::string& n = ual().animations[i].name; n == "Idle_Loop" || n.ends_with("|Idle_Loop")) {
            set.sample(static_cast<int>(i), 0.0f, true, p);
            break;
        }
    return p;
}

int bone(const char* name) {
    for (size_t b = 0; b < ual().bones.size(); ++b)
        if (kke::canonicalBoneName(ual().bones[b].name) == name) return static_cast<int>(b);
    return -1;
}

glm::vec3 at(const Pose& p, int b) { return glm::vec3(kke::poseToModel(ual(), p)[static_cast<size_t>(b)][3]); }

// Flat floor at y = 0.
bool floor0(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
    hit = glm::vec3(from.x, 0.0f, from.z);
    normal = glm::vec3(0, 1, 0);
    return true;
}

} // namespace

TEST(CharacterIk, HandsReachAnEdgeOverheadWithTheShoulderUp) {
    NEED_UAL();
    kke::AnimationSet set(ual());
    CharacterIk ik(ual(), &ual());
    ASSERT_TRUE(ik.valid());
    ASSERT_GE(ik.arm(CharacterIk::Left).clavicle, 0);
    const glm::vec3 fwd = kke::modelForward(ual());
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0, 1, 0), fwd));
    const int handL = bone("hand_l"), handR = bone("hand_r"), upperL = bone("upperarm_l");
    const float shoulderBefore = at(idlePose(set), upperL).y;
    // An edge 1.85 m up (within reach of the idle pose's shoulders), 0.3 m
    // in front: hands shoulder-width apart on it.
    const glm::vec3 edge = fwd * 0.3f + glm::vec3(0, 1.85f, 0);
    Pose p;
    for (int i = 0; i < 90; ++i) { // the contacts fade in over the frames
        p = idlePose(set);
        ik.hand(CharacterIk::Left, edge + left * 0.2f);
        ik.hand(CharacterIk::Right, edge - left * 0.2f);
        ik.apply(ual(), p, glm::mat4(1.0f), floor0, glm::vec3(0.0f), 1.0f / 60.0f);
    }
    EXPECT_GT(ik.handWeight(CharacterIk::Left), 0.99f);
    EXPECT_LT(glm::length(at(p, handL) - (edge + left * 0.2f)), 0.03f);
    EXPECT_LT(glm::length(at(p, handR) - (edge - left * 0.2f)), 0.03f);
    EXPECT_GT(at(p, upperL).y, shoulderBefore + 0.02f); // the girdle lifted with the arm
    for (int s = 0; s < 2; ++s)
        EXPECT_LT(kke::armPenetration(kke::poseToModel(ual(), p), ik.arm(static_cast<CharacterIk::Side>(s)), ik.bodyShape()), 0.02f);

    // Let go: back to the clip.
    for (int i = 0; i < 90; ++i) {
        p = idlePose(set);
        ik.apply(ual(), p, glm::mat4(1.0f), floor0, glm::vec3(0.0f), 1.0f / 60.0f);
    }
    EXPECT_EQ(ik.handWeight(CharacterIk::Left), 0.0f);
    EXPECT_LT(glm::length(at(p, handL) - at(idlePose(set), handL)), 0.01f);
}

// Hanging from a ledge: the ball of the foot against the wall in front.
TEST(CharacterIk, FootPlantsOnAWall) {
    NEED_UAL();
    kke::AnimationSet set(ual());
    CharacterIk ik(ual(), &ual());
    const glm::vec3 fwd = kke::modelForward(ual());
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0, 1, 0), fwd));
    const int footL = bone("foot_l");
    const glm::vec3 wall = fwd * 0.35f + left * 0.1f + glm::vec3(0, 0.45f, 0); // on the wall's face
    Pose p;
    for (int i = 0; i < 90; ++i) {
        p = idlePose(set);
        ik.feetOnGround(false);
        ik.foot(CharacterIk::Left, wall, -fwd);
        ik.apply(ual(), p, glm::mat4(1.0f), floor0, glm::vec3(0.0f), 1.0f / 60.0f);
    }
    const glm::vec3 ankle = at(p, footL);
    // Off the wall by the ankle's height above the sole, at the contact.
    EXPECT_LT(glm::dot(ankle - wall, fwd), -0.02f);
    EXPECT_GT(glm::dot(ankle - wall, fwd), -0.2f);
    EXPECT_NEAR(ankle.y, wall.y, 0.08f);
}

// Speeding up forward: the chest leans forward, a little; standing still
// it comes back upright.
TEST(CharacterIk, LeansIntoSpeedingUp) {
    NEED_UAL();
    kke::AnimationSet set(ual());
    CharacterIk ik(ual(), &ual());
    const glm::vec3 fwd = kke::modelForward(ual());
    const int chest = bone("spine_03"), pelvis = bone("pelvis");
    const glm::vec3 upright = at(idlePose(set), chest) - at(idlePose(set), pelvis);
    Pose p;
    float speed = 0.0f;
    for (int i = 0; i < 20; ++i) {
        speed += 8.0f / 60.0f; // 8 m/s^2
        p = idlePose(set);
        ik.apply(ual(), p, glm::mat4(1.0f), floor0, fwd * speed, 1.0f / 60.0f);
    }
    const glm::vec3 leaning = at(p, chest) - at(p, pelvis);
    EXPECT_GT(glm::dot(leaning - upright, fwd), 0.01f);
    EXPECT_LE(glm::length(ik.leanDegrees()), ik.settings().maxLean + 1e-3f);
    for (int i = 0; i < 120; ++i) {
        p = idlePose(set);
        ik.apply(ual(), p, glm::mat4(1.0f), floor0, fwd * speed, 1.0f / 60.0f);
    }
    EXPECT_LT(glm::length(ik.leanDegrees()), 0.2f);
}
