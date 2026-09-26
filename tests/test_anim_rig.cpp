#include "kke/AnimRig.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>

using kke::ModelData;
using kke::Pose;

namespace {

kke::ModelBone bone(const char* name, int parent, const glm::mat4& local) {
    kke::ModelBone b;
    b.name = name;
    b.parent = parent;
    b.localRest = local;
    return b;
}

glm::mat4 at(float x, float y, float z) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)); }

// A pair of legs hanging from a pelvis 0.9 m up: thigh 0.45, calf 0.45,
// feet on the floor (y = 0), 0.1 m either side.
ModelData legs() {
    ModelData m;
    m.bones.push_back(bone("Root", -1, glm::mat4(1.0f)));
    m.bones.push_back(bone("Pelvis", 0, at(0, 0.9f, 0)));
    for (float side : { -0.1f, 0.1f }) {
        const bool left = side < 0;
        const int thigh = static_cast<int>(m.bones.size());
        m.bones.push_back(bone(left ? "Thigh_L" : "Thigh_R", 1, at(side, 0, 0)));
        m.bones.push_back(bone(left ? "calf_l" : "calf_r", thigh, at(0, -0.45f, 0)));
        m.bones.push_back(bone(left ? "Foot_L" : "Foot_R", thigh + 1, at(0, -0.45f, 0)));
    }
    return m;
}

Pose restPose(const ModelData& m) { return kke::AnimationSet(m).restPose(); }

glm::vec3 pos(const ModelData& m, const Pose& p, int b) { return glm::vec3(kke::poseToModel(m, p)[b][3]); }

} // namespace

TEST(AnimRig, TwoBoneReachesTheTarget) {
    ModelData m = legs();
    Pose p = restPose(m);
    const kke::TwoBoneChain leg = kke::findChain(m, "thigh_l", "calf_l", "foot_l");
    ASSERT_TRUE(leg.valid());
    // Foot up 0.3 and forward 0.2; the knee should go forward.
    const glm::vec3 target(-0.1f, 0.3f, 0.2f);
    kke::solveTwoBone(m, p, leg, target, glm::vec3(-0.1f, 0.5f, 1.0f));
    EXPECT_LT(glm::length(pos(m, p, leg.end) - target), 0.005f);
    EXPECT_GT(pos(m, p, leg.lower).z, 0.1f);
    // Bone lengths don't change.
    EXPECT_NEAR(glm::length(pos(m, p, leg.lower) - pos(m, p, leg.upper)), 0.45f, 1e-3f);
    EXPECT_NEAR(glm::length(pos(m, p, leg.end) - pos(m, p, leg.lower)), 0.45f, 1e-3f);
}

TEST(AnimRig, UnreachableTargetStraightensTowardIt) {
    ModelData m = legs();
    Pose p = restPose(m);
    const kke::TwoBoneChain leg = kke::findChain(m, "thigh_l", "calf_l", "foot_l");
    kke::solveTwoBone(m, p, leg, glm::vec3(-0.1f, 0.9f, -3.0f), glm::vec3(-0.1f, 0.5f, 1.0f));
    const glm::vec3 hip = pos(m, p, leg.upper), foot = pos(m, p, leg.end);
    EXPECT_NEAR(glm::length(foot - hip), 0.9f, 0.01f);
    EXPECT_LT(foot.z, -0.85f);
}

TEST(AnimRig, HalfWeightGoesHalfway) {
    ModelData m = legs();
    Pose p = restPose(m);
    const kke::TwoBoneChain leg = kke::findChain(m, "thigh_l", "calf_l", "foot_l");
    kke::solveTwoBone(m, p, leg, glm::vec3(-0.1f, 0.2f, 0.0f), glm::vec3(-0.1f, 0.5f, 1.0f), 0.5f);
    EXPECT_NEAR(pos(m, p, leg.end).y, 0.1f, 0.005f);
}

TEST(AnimRig, FeetFindUnevenGround) {
    ModelData m = legs();
    const kke::TwoBoneChain l = kke::findChain(m, "thigh_l", "calf_l", "foot_l");
    const kke::TwoBoneChain r = kke::findChain(m, "thigh_r", "calf_r", "foot_r");
    kke::FootPlacer placer(m, l, r, 1);
    // Left foot over a 0.2 m step, right foot over a 0.15 m dip.
    auto ground = [](const glm::vec3& from, glm::vec3& hit) {
        hit = glm::vec3(from.x, from.x < 0.0f ? 0.2f : -0.15f, from.z);
        return true;
    };
    Pose p;
    for (int i = 0; i < 120; ++i) { // settles over a couple of seconds' frames
        p = restPose(m);
        placer.apply(m, p, ground, 1.0f / 60.0f);
    }
    EXPECT_NEAR(pos(m, p, l.end).y, 0.2f, 0.01f);
    EXPECT_NEAR(pos(m, p, r.end).y, -0.15f, 0.01f);
    EXPECT_NEAR(placer.pelvisOffset(), -0.15f, 0.01f); // hips drop for the low foot
    // Off (in the air): back to the animated pose.
    for (int i = 0; i < 120; ++i) {
        p = restPose(m);
        placer.apply(m, p, ground, 1.0f / 60.0f, 0.0f);
    }
    EXPECT_NEAR(pos(m, p, l.end).y, 0.0f, 0.01f);
}

TEST(AnimRig, CanonicalNamesPairUalWithSynty) {
    EXPECT_EQ(kke::canonicalBoneName("UpperArm_L"), kke::canonicalBoneName("upperarm_l"));
    EXPECT_EQ(kke::canonicalBoneName("indexFinger_02_r"), kke::canonicalBoneName("index_02_r"));
    EXPECT_EQ(kke::canonicalBoneName("finger_01_l"), kke::canonicalBoneName("middle_01_l"));
    EXPECT_EQ(kke::canonicalBoneName("mixamorig:Hips"), "pelvis");
    EXPECT_EQ(kke::canonicalBoneName("ball_leaf_l"), kke::canonicalBoneName("ball_l"));
}

// Target bones point along different axes than the source's, and the
// target is shorter: the retargeted swing must still move the leg
// forward, and the pelvis travel shrinks with the leg.
TEST(AnimRig, RetargetCopiesMotionNotAxes) {
    ModelData src = legs();
    ModelData tgt;
    tgt.bones.push_back(bone("root", -1, glm::mat4(1.0f)));
    tgt.bones.push_back(bone("pelvis", 0, at(0, 0.6f, 0)));
    tgt.bones.push_back(bone("thigh_l", 1, at(-0.08f, 0, 0) * glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0, 0, 1))));
    tgt.bones.push_back(bone("calf_l", 2, at(0.3f, 0, 0)));
    tgt.bones.push_back(bone("foot_l", 3, at(0.3f, 0, 0)));
    tgt.bones.push_back(bone("thigh_r", 1, at(0.08f, 0, 0))); // left at -X: faces -Z, like the source

    // Source clip: left thigh swings 45 degrees forward, pelvis moves up 0.09.
    kke::ModelAnimation clip;
    clip.name = "Kick";
    clip.duration = 0.0f;
    std::vector<glm::mat4> frame;
    for (const kke::ModelBone& b : src.bones) frame.push_back(b.localRest);
    frame[1] = at(0, 0.99f, 0);
    frame[2] = frame[2] * glm::rotate(glm::mat4(1.0f), glm::radians(-45.0f), glm::vec3(1, 0, 0));
    clip.frames.push_back(frame);
    src.animations.push_back(clip);

    kke::BoneMatch match = kke::matchBones(src, tgt);
    EXPECT_EQ(match.matched, 6);
    std::vector<kke::ModelAnimation> out = kke::retargetAnimations(src, tgt, match);
    ASSERT_EQ(out.size(), 1u);
    tgt.animations = out;
    kke::AnimationSet set(tgt);
    Pose p;
    set.sample(0, 0.0f, false, p);
    const glm::vec3 hip = pos(tgt, p, 2), foot = pos(tgt, p, 4);
    const glm::vec3 dir = glm::normalize(foot - hip);
    // 45 degrees forward and down, like the source.
    EXPECT_NEAR(dir.y, -std::sqrt(0.5f), 0.02f);
    EXPECT_NEAR(dir.z, std::sqrt(0.5f), 0.02f);
    // Pelvis: 0.09 up on a 0.9 m pelvis = 0.06 up on a 0.6 m one.
    EXPECT_NEAR(pos(tgt, p, 1).y, 0.66f, 0.005f);
}

TEST(AnimRig, ModelForwardFromTheLegs) {
    ModelData m = legs(); // left leg at -X
    EXPECT_NEAR(kke::modelForward(m).z, -1.0f, 1e-4f);
    for (kke::ModelBone& b : m.bones) b.localRest[3].x = -b.localRest[3].x; // mirror: left at +X
    EXPECT_NEAR(kke::modelForward(m).z, 1.0f, 1e-4f);
}

TEST(AnimRig, RootMotionPlaysInPlaceAndReportsTravel) {
    ModelData m;
    m.bones.push_back(bone("root", -1, glm::mat4(1.0f)));
    kke::ModelAnimation walk;
    walk.name = "Walk";
    walk.duration = 1.0f;
    walk.sampleRate = 30.0f;
    for (int f = 0; f <= 30; ++f) walk.frames.push_back({ at(0, 0, 1.5f * f / 30.0f) }); // 1.5 m per cycle
    m.animations.push_back(walk);
    kke::AnimationSet set(m);
    set.extractRootMotion(m, 0);
    Pose p;
    set.sample(0, 0.5f, true, p);
    EXPECT_NEAR(p[0].t.z, 0.0f, 1e-4f); // in place
    EXPECT_NEAR(set.rootTravel(0, 0.2f, 0.6f, true).z, 0.6f, 1e-3f);
    EXPECT_NEAR(set.rootTravel(0, 0.8f, 1.2f, true).z, 0.6f, 1e-3f); // across the loop

    kke::Animator anim(set);
    anim.play(anim.addClipState("walk", 0, true), 0.0f);
    float z = 0.0f;
    for (int i = 0; i < 60; ++i) {
        anim.update(1.0f / 60.0f);
        z += anim.rootMotion().z;
    }
    EXPECT_NEAR(z, 1.5f, 0.01f);
}

// With the real packs: UAL clips on a POLYGON Town character.
TEST(AnimRig, UalOnSyntyCharacterIfInstalled) {
    namespace fs = std::filesystem;
    const fs::path ual = fs::path(KKE_SOURCE_DIR) / "assets/animations/UAL1_Standard.fbx";
    const fs::path chr = fs::path(KKE_SOURCE_DIR) / "assets/synty/PolygonTown_Source_Files/Source_Files/Characters/SK_Character_Father_01.fbx";
    if (!fs::exists(ual) || !fs::exists(chr)) GTEST_SKIP() << "UAL or POLYGON Town not installed";
    ModelData src = kke::loadModel(ual.string());
    kke::ModelLoadOptions noAnims;
    noAnims.loadAnimations = false;
    ModelData tgt = kke::loadModel(chr.string(), noAnims);
    kke::BoneMatch match = kke::matchBones(src, tgt);
    // Everything but Synty's face bones pairs up.
    EXPECT_GE(match.matched, static_cast<int>(tgt.bones.size()) - 4);
    tgt.animations = kke::retargetAnimations(src, tgt, match);
    ASSERT_EQ(tgt.animations.size(), src.animations.size());
    kke::AnimationSet set(tgt);
    const int walk = set.find("Walk_Loop");
    ASSERT_GE(walk, 0);
    const kke::TwoBoneChain l = kke::findChain(tgt, "thigh_l", "calf_l", "foot_l");
    ASSERT_TRUE(l.valid());
    // Feet stay near the floor through the walk cycle.
    Pose p;
    float lowest = 1e9f, highest = -1e9f;
    for (float t = 0.0f; t < set.duration(walk); t += 0.05f) {
        set.sample(walk, t, true, p);
        const float y = pos(tgt, p, l.end).y;
        EXPECT_FALSE(std::isnan(y));
        lowest = std::min(lowest, y);
        highest = std::max(highest, y);
    }
    EXPECT_LT(std::abs(lowest), 0.2f);
    EXPECT_GT(highest - lowest, 0.05f); // the foot lifts
}
