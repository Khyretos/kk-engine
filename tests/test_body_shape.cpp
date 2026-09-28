#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/BodyShape.h"
#include "kke/Equipment.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

// Body awareness and equipment on the UAL mannequin (CC0, shipped in
// assets/animations): the body shape fitted to its mesh, arms that stay
// out of it, palms, fingers that close on a handle, items in slots.

namespace {

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

// A standing pose: the idle clip's first frame (the rest pose is a T-pose).
Pose idlePose(const kke::AnimationSet& set) {
    Pose p = set.restPose();
    for (size_t i = 0; i < ual().animations.size(); ++i)
        if (const std::string& n = ual().animations[i].name; n == "Idle_Loop" || n.ends_with("|Idle_Loop")) {
            set.sample(static_cast<int>(i), 0.0f, true, p);
            break;
        }
    return p;
}

struct Arms {
    kke::HumanArm arm[2];
    Arms() {
        const kke::TwoBoneChain l = kke::findChain(ual(), "upperarm_l", "lowerarm_l", "hand_l");
        const kke::TwoBoneChain r = kke::findChain(ual(), "upperarm_r", "lowerarm_r", "hand_r");
        arm[0] = kke::makeHumanArm(ual(), l, r);
        arm[1] = kke::makeHumanArm(ual(), r, l);
    }
};

glm::vec3 pos(const std::vector<glm::mat4>& w, int b) { return glm::vec3(w[static_cast<size_t>(b)][3]); }

} // namespace

TEST(BodyShape, CapsuleOverlap) {
    const kke::Capsule a{ { 0, 0, 0 }, { 1, 0, 0 }, 0.1f }, b{ { 0.5f, 0.15f, -1 }, { 0.5f, 0.15f, 1 }, 0.1f };
    glm::vec3 push;
    EXPECT_NEAR(kke::capsuleOverlap(a, b, push), 0.05f, 1e-5f);
    EXPECT_NEAR(push.y, -1.0f, 1e-5f); // a is below b
    const kke::Capsule far{ { 0, 1, 0 }, { 1, 1, 0 }, 0.1f };
    EXPECT_LT(kke::capsuleOverlap(a, far, push), 0.0f);
}

TEST(BodyShape, FitsTheMannequinsMesh) {
    NEED_UAL();
    const kke::BodyShape body = kke::BodyShape::fit(ual());
    ASSERT_TRUE(body.valid());
    for (kke::BodyPart p : { kke::BodyPart::Pelvis, kke::BodyPart::Belly, kke::BodyPart::Chest, kke::BodyPart::UpperChest, kke::BodyPart::Head,
                             kke::BodyPart::ThighL, kke::BodyPart::ThighR, kke::BodyPart::UpperArmL, kke::BodyPart::ForearmR, kke::BodyPart::HandR }) {
        const kke::BodyShape::Part* part = body.part(p);
        ASSERT_NE(part, nullptr) << kke::bodyPartName(p);
        EXPECT_GT(part->radius, 0.02f) << kke::bodyPartName(p);
        EXPECT_LT(part->radius, 0.2f) << kke::bodyPartName(p);
    }
    // A torso no wider than the shoulders, and the torso's capsules touch
    // one another (no gap an elbow could slip through).
    const Pose rest = kke::AnimationSet(ual()).restPose();
    const std::vector<glm::mat4> w = kke::poseToModel(ual(), rest);
    const kke::Capsule chest = body.posed(*body.part(kke::BodyPart::Chest), w);
    const float shoulders = glm::length(pos(w, ual().findBone("upperarm_l")) - pos(w, ual().findBone("upperarm_r")));
    EXPECT_LT(glm::length(chest.b - chest.a) + 2.0f * chest.radius, shoulders * 1.2f);
    const kke::BodyPart torso[4] = { kke::BodyPart::Pelvis, kke::BodyPart::Belly, kke::BodyPart::Chest, kke::BodyPart::UpperChest };
    for (int i = 0; i < 3; ++i) {
        glm::vec3 d;
        EXPECT_GT(kke::capsuleOverlap(body.posed(*body.part(torso[i]), w), body.posed(*body.part(torso[i + 1]), w), d), 0.0f)
            << kke::bodyPartName(torso[i]) << " / " << kke::bodyPartName(torso[i + 1]);
    }
}

TEST(BodyShape, AnimatedArmsAreNotInsideTheBody) {
    NEED_UAL();
    // The fit mustn't be fat: arms hanging in the idle clip already clear it.
    const kke::BodyShape body = kke::BodyShape::fit(ual());
    const kke::AnimationSet set(ual());
    const Arms arms;
    const std::vector<glm::mat4> w = kke::poseToModel(ual(), idlePose(set));
    for (int s = 0; s < 2; ++s) EXPECT_LT(kke::armPenetration(w, arms.arm[s], body), 0.01f) << "arm " << s;
}

TEST(BodyShape, ArmGoesRoundTheBodyNotThroughIt) {
    NEED_UAL();
    const kke::BodyShape body = kke::BodyShape::fit(ual());
    const kke::AnimationSet set(ual());
    const Arms arms;
    const Pose start = idlePose(set);
    const std::vector<glm::mat4> w = kke::poseToModel(ual(), start);
    const glm::vec3 fwd = kke::modelForward(ual()), up(0, 1, 0);
    const glm::vec3 left = glm::normalize(glm::cross(up, fwd));
    const glm::vec3 pelvis = pos(w, ual().findBone("pelvis"));
    const glm::vec3 chest = pos(w, ual().findBone("spine_02"));
    // Hands aimed across the body: the other hip, the other flank, the
    // other shoulder, round the back. Plain IK takes the straight way
    // through the body for some of them.
    const glm::vec3 goals[] = {
        pelvis + left * 0.18f + up * 0.15f, pelvis + left * 0.2f + fwd * 0.05f, chest + left * 0.2f + up * 0.15f + fwd * 0.05f,
        chest + left * 0.1f - fwd * 0.2f, pelvis + fwd * 0.1f, chest + left * 0.25f,
    };
    float worstPlain = 0.0f;
    for (int side = 0; side < 2; ++side) {
        const float mirror = side == 1 ? 1.0f : -1.0f; // the right arm reaches to the left, and the left arm to the right
        for (glm::vec3 g : goals) {
            g.x = pelvis.x + (g.x - pelvis.x) * mirror;
            kke::ArmGoal goal;
            goal.hand = g;
            Pose plain = start;
            kke::solveHumanArm(ual(), plain, arms.arm[side], goal);
            worstPlain = std::max(worstPlain, kke::armPenetration(kke::poseToModel(ual(), plain), arms.arm[side], body));
            Pose aware = start;
            const kke::BodyAvoidResult r = kke::solveHumanArm(ual(), aware, arms.arm[side], goal, body);
            EXPECT_LT(r.penetration, 0.005f) << "arm " << side << " to " << g.x << " " << g.y << " " << g.z;
            EXPECT_LT(kke::armPenetration(kke::poseToModel(ual(), aware), arms.arm[side], body), 0.005f);
            // ... and the hand stays by the body, not wandering off.
            float gap = 1e9f;
            for (const kke::BodyShape::Part& part : body.parts()) {
                if (kke::isArmPart(part.part, true) || kke::isArmPart(part.part, false)) continue;
                glm::vec3 d;
                gap = std::min(gap, -kke::capsuleOverlap({ r.arm.hand, r.arm.hand, 0.0f }, body.posed(part, w), d));
            }
            EXPECT_LT(gap, 0.3f) << "arm " << side << " to " << g.x << " " << g.y << " " << g.z;
        }
    }
    EXPECT_GT(worstPlain, 0.03f); // the problem this solves
}

TEST(BodyShape, HandAimedIntoTheBodyStopsAtItsSurface) {
    NEED_UAL();
    const kke::BodyShape body = kke::BodyShape::fit(ual());
    const kke::AnimationSet set(ual());
    const Arms arms;
    Pose p = idlePose(set);
    const std::vector<glm::mat4> w = kke::poseToModel(ual(), p);
    kke::ArmGoal goal;
    goal.hand = pos(w, ual().findBone("spine_02")); // the middle of the chest
    const kke::BodyAvoidResult r = kke::solveHumanArm(ual(), p, arms.arm[0], goal, body);
    EXPECT_TRUE(r.moved);
    EXPECT_LT(r.penetration, 0.005f);
}

TEST(BodyShape, HeldThingsStayOutOfTheBody) {
    NEED_UAL();
    const kke::BodyShape body = kke::BodyShape::fit(ual());
    const kke::AnimationSet set(ual());
    const Arms arms;
    Pose p = idlePose(set);
    const std::vector<glm::mat4> w = kke::poseToModel(ual(), p);
    // A stick held in the right hand pointing across the belly.
    const int hand = arms.arm[1].chain.end;
    const glm::mat4 handW = w[static_cast<size_t>(hand)];
    const glm::vec3 fwd = kke::modelForward(ual());
    const glm::vec3 left = glm::normalize(glm::cross(glm::vec3(0, 1, 0), fwd));
    const glm::mat4 inv = glm::inverse(handW);
    kke::BodyAvoid avoid;
    const glm::vec3 grip = pos(w, hand);
    avoid.held.push_back({ glm::vec3(inv * glm::vec4(grip, 1.0f)), glm::vec3(inv * glm::vec4(grip + left * 0.7f, 1.0f)), 0.02f });
    kke::ArmGoal goal;
    goal.hand = grip + fwd * 0.05f;
    glm::mat3 rot(handW);
    for (int i = 0; i < 3; ++i) rot[i] = glm::normalize(rot[i]);
    goal.handRotation = glm::quat_cast(rot); // keeps pointing across
    Pose plain = p;
    kke::solveHumanArm(ual(), plain, arms.arm[1], goal);
    EXPECT_GT(kke::armPenetration(kke::poseToModel(ual(), plain), arms.arm[1], body, avoid), 0.02f);
    const kke::BodyAvoidResult r = kke::solveHumanArm(ual(), p, arms.arm[1], goal, body, avoid);
    EXPECT_TRUE(r.moved);
    EXPECT_LT(r.penetration, 0.005f);
}

// ---------------------------------------------------------------------
// Hands and equipment

TEST(Equipment, PalmIsInTheHandFacingWhereTheFingersCurl) {
    NEED_UAL();
    const kke::AnimationSet set(ual());
    const std::vector<glm::mat4> w = kke::poseToModel(ual(), idlePose(set));
    for (int s = 0; s < 2; ++s) {
        const kke::HandRig h = kke::makeHandRig(ual(), s == 0);
        ASSERT_TRUE(h.valid());
        const glm::mat4 palm = w[static_cast<size_t>(h.hand)] * h.palm;
        const glm::vec3 wrist = pos(w, h.hand), knuckle = pos(w, h.finger[1][0]);
        const glm::vec3 c = glm::vec3(palm[3]);
        // Between the wrist and the knuckles, near the hand.
        const float t = glm::dot(c - wrist, knuckle - wrist) / glm::dot(knuckle - wrist, knuckle - wrist);
        EXPECT_GT(t, 0.4f);
        EXPECT_LT(t, 0.9f);
        EXPECT_LT(glm::length(c - wrist), h.knuckles * 1.1f);
        // The relaxed idle hand curls its fingers toward the palm's side.
        const glm::vec3 n = glm::normalize(glm::vec3(palm[2]));
        const glm::vec3 tip = pos(w, h.finger[1][2]);
        EXPECT_GT(glm::dot(tip - knuckle, n), 0.0f) << (s == 0 ? "left" : "right");
    }
}

TEST(Equipment, FingersCloseOnAHandleWithoutGoingThroughIt) {
    NEED_UAL();
    const kke::AnimationSet set(ual());
    for (int s = 0; s < 2; ++s) {
        Pose p = set.restPose();
        const kke::HandRig h = kke::makeHandRig(ual(), s == 0);
        const std::vector<glm::mat4> w = kke::poseToModel(ual(), p);
        const glm::mat4 palm = w[static_cast<size_t>(h.hand)] * h.palm;
        const float radius = 0.016f;
        const glm::vec3 axis = glm::normalize(glm::vec3(palm[1])), centre = glm::vec3(palm[3]) + glm::normalize(glm::vec3(palm[2])) * radius;
        kke::GripSurface surface;
        surface.capsules.push_back({ centre - axis * 0.08f, centre + axis * 0.08f, radius });
        kke::wrapFingers(ual(), p, h, surface);
        const std::vector<glm::mat4> after = kke::poseToModel(ual(), p);
        for (int f = 0; f < 4; ++f) {
            // Closed: the middle joint well bent, and no segment inside the handle.
            const glm::vec3 a = pos(after, h.finger[f][0]), b = pos(after, h.finger[f][1]), c = pos(after, h.finger[f][2]);
            const float bend = glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(b - a), glm::normalize(c - b)), -1.0f, 1.0f)));
            // The index lies nearest where the handle leaves the hand: it wraps least.
            EXPECT_GT(bend, f == 0 ? 15.0f : 35.0f) << "finger " << f;
            for (const auto& [p0, p1] : { std::pair{ a, b }, std::pair{ b, c } }) {
                glm::vec3 d;
                EXPECT_LT(kke::capsuleOverlap({ p0, p1, h.fingerRadius }, surface.capsules[0], d), 0.003f) << "finger " << f;
            }
        }
    }
}

TEST(Equipment, ItemGoesWhereTheHandHoldsIt) {
    NEED_UAL();
    kke::Equipment eq(ual());
    ASSERT_TRUE(eq.valid());
    kke::Equippable stick;
    stick.name = "stick";
    stick.grips.push_back({ "main", glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.1f, 0.0f)), 0.015f, 0.05f });
    stick.shape.push_back({ { 0, 0, 0 }, { 0, 0.8f, 0 }, 0.02f });
    EXPECT_FALSE(eq.equip(kke::EquipSlot::Back, stick)); // hands only
    ASSERT_TRUE(eq.equip(kke::EquipSlot::RightHand, stick));
    // Where the hand must be for the stick to be at `want`; put the hand
    // there and the stick comes out at `want`.
    const glm::mat4 want = glm::translate(glm::mat4(1.0f), glm::vec3(0.3f, 1.1f, 0.4f)) * glm::rotate(glm::mat4(1.0f), 0.7f, glm::vec3(1, 0, 0));
    const glm::mat4 hand = eq.handFor(1, stick, 0, want);
    std::vector<glm::mat4> w = kke::poseToModel(ual(), kke::AnimationSet(ual()).restPose());
    w[static_cast<size_t>(eq.hand(1).hand)] = hand;
    const glm::mat4 got = eq.itemTransform(kke::EquipSlot::RightHand, w);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 3; ++r) EXPECT_NEAR(got[c][r], want[c][r], 1e-4f);
    // The handle's axis is a handle's radius out from the palm's skin.
    const glm::mat4 palm = hand * eq.hand(1).palm;
    const glm::vec3 handle = glm::vec3(got * glm::vec4(0.0f, 0.1f, 0.0f, 1.0f));
    EXPECT_NEAR(glm::length(handle - glm::vec3(palm[3])), 0.015f, 1e-4f);
    EXPECT_EQ(eq.heldShape(1).size(), 1u);
    // Worn places exist on the body.
    EXPECT_TRUE(eq.socket(kke::EquipSlot::Back).valid());
    EXPECT_TRUE(eq.socket(kke::EquipSlot::HipLeft).valid());
}
