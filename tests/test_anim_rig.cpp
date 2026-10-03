#include "kke/AnimRig.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

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

// A chest 1.4 m up with both arms straight out (a T-pose), facing +Z (the
// left arm at +X): upper arm 0.3, forearm 0.25.
ModelData arms() {
    ModelData m;
    m.bones.push_back(bone("Root", -1, glm::mat4(1.0f)));
    m.bones.push_back(bone("Spine_03", 0, at(0, 1.4f, 0)));
    for (float side : { 1.0f, -1.0f }) {
        const bool left = side > 0;
        const int upper = static_cast<int>(m.bones.size());
        m.bones.push_back(bone(left ? "upperarm_l" : "upperarm_r", 1, at(0.2f * side, 0, 0)));
        m.bones.push_back(bone(left ? "lowerarm_l" : "lowerarm_r", upper, at(0.3f * side, 0, 0)));
        m.bones.push_back(bone(left ? "hand_l" : "hand_r", upper + 1, at(0.25f * side, 0, 0)));
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

// On a 20 degree slope the feet turn with it (model space: the rotation
// from the animated foot to the placed one takes up onto the normal);
// a 50 degree one is clamped to maxTilt.
TEST(AnimRig, FeetTiltWithTheSlope) {
    ModelData m = legs();
    const kke::TwoBoneChain l = kke::findChain(m, "thigh_l", "calf_l", "foot_l");
    const kke::TwoBoneChain r = kke::findChain(m, "thigh_r", "calf_r", "foot_r");
    for (float slope : { 20.0f, 50.0f }) {
        kke::FootPlacer placer(m, l, r, 1);
        const glm::vec3 n = glm::angleAxis(glm::radians(slope), glm::vec3(1, 0, 0)) * glm::vec3(0, 1, 0);
        auto ground = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            hit = glm::vec3(from.x, 0.0f, from.z);
            normal = n;
            return true;
        };
        Pose p;
        for (int i = 0; i < 120; ++i) {
            p = restPose(m);
            placer.apply(m, p, kke::FootPlacer::SurfaceQuery(ground), 1.0f / 60.0f);
        }
        const std::vector<glm::mat4> before = kke::poseToModel(m, restPose(m));
        const std::vector<glm::mat4> after = kke::poseToModel(m, p);
        const glm::quat delta = glm::quat_cast(glm::mat3(after[l.end])) * glm::inverse(glm::quat_cast(glm::mat3(before[l.end])));
        const glm::vec3 footUp = delta * glm::vec3(0, 1, 0);
        const float expected = std::min(slope, 30.0f);
        EXPECT_NEAR(glm::degrees(std::acos(glm::clamp(footUp.y, -1.0f, 1.0f))), expected, 1.0f) << "slope " << slope;
        EXPECT_GT(footUp.z, 0.0f); // tilted the same way as the ground
        EXPECT_NEAR(pos(m, p, l.end).y, 0.0f, 0.01f); // still standing on it
    }
}

// Feet with a ball bone, ankles 0.08 m up: a foot whose heel is over the
// floor and whose toes are over a 0.2 m step stands on the step, at once
// (a planted foot never sinks in while the smoothing catches up).
TEST(AnimRig, FootHalfOverAStepStandsOnIt) {
    ModelData m;
    m.bones.push_back(bone("Root", -1, glm::mat4(1.0f)));
    m.bones.push_back(bone("Pelvis", 0, at(0, 0.98f, 0)));
    for (float side : { -0.1f, 0.1f }) {
        const bool left = side < 0;
        const int thigh = static_cast<int>(m.bones.size());
        m.bones.push_back(bone(left ? "Thigh_L" : "Thigh_R", 1, at(side, 0, 0)));
        m.bones.push_back(bone(left ? "calf_l" : "calf_r", thigh, at(0, -0.45f, 0)));
        m.bones.push_back(bone(left ? "Foot_L" : "Foot_R", thigh + 1, at(0, -0.45f, 0)));
        m.bones.push_back(bone(left ? "ball_l" : "ball_r", thigh + 2, at(0, -0.06f, 0.14f)));
    }
    const kke::TwoBoneChain l = kke::findChain(m, "thigh_l", "calf_l", "foot_l");
    const kke::TwoBoneChain r = kke::findChain(m, "thigh_r", "calf_r", "foot_r");
    kke::FootPlacer placer(m, l, r, 1);
    // The step starts 0.1 m in front of the ankles, under the left foot only.
    auto ground = [](const glm::vec3& from, glm::vec3& hit) {
        hit = glm::vec3(from.x, from.x < 0.0f && from.z > 0.1f ? 0.2f : 0.0f, from.z);
        return true;
    };
    Pose p = restPose(m);
    placer.apply(m, p, ground, 1.0f / 60.0f); // one frame
    const int ballL = 5;
    EXPECT_GE(pos(m, p, ballL).y, 0.2f - 0.005f);              // the toes are on the step, not in it
    EXPECT_NEAR(pos(m, p, r.end).y, 0.08f, 0.01f);             // the other foot stays on the floor
}

// The shoulder girdle: overhead, the clavicle lifts (the shoulder rises);
// reaching low in front, it barely moves.
ModelData armsWithClavicles() {
    ModelData m;
    m.bones.push_back(bone("Root", -1, glm::mat4(1.0f)));
    m.bones.push_back(bone("Spine_03", 0, at(0, 1.4f, 0)));
    for (float side : { 1.0f, -1.0f }) {
        const bool left = side > 0;
        const int clav = static_cast<int>(m.bones.size());
        m.bones.push_back(bone(left ? "clavicle_l" : "clavicle_r", 1, at(0.04f * side, 0, 0)));
        m.bones.push_back(bone(left ? "upperarm_l" : "upperarm_r", clav, at(0.16f * side, 0, 0)));
        m.bones.push_back(bone(left ? "lowerarm_l" : "lowerarm_r", clav + 1, at(0.3f * side, 0, 0)));
        m.bones.push_back(bone(left ? "hand_l" : "hand_r", clav + 2, at(0.25f * side, 0, 0)));
    }
    return m;
}

TEST(AnimRig, HumanArmLiftsTheShoulderGirdleOverhead) {
    const ModelData m = armsWithClavicles();
    const kke::TwoBoneChain r = kke::findChain(m, "upperarm_r", "lowerarm_r", "hand_r");
    const kke::TwoBoneChain l = kke::findChain(m, "upperarm_l", "lowerarm_l", "hand_l");
    const kke::HumanArm arm = kke::makeHumanArm(m, r, l);
    ASSERT_GE(arm.clavicle, 0);
    const float restShoulderY = pos(m, restPose(m), r.upper).y;

    Pose up = restPose(m);
    kke::ArmGoal overhead;
    overhead.hand = glm::vec3(-0.25f, 1.4f + 0.58f, 0.05f);
    kke::solveHumanArm(m, up, arm, overhead);
    EXPECT_GT(pos(m, up, r.upper).y, restShoulderY + 0.04f); // shrugged
    EXPECT_LT(glm::length(pos(m, up, r.end) - overhead.hand), 0.01f);

    Pose low = restPose(m);
    kke::ArmGoal front;
    front.hand = glm::vec3(-0.2f, 1.1f, 0.3f);
    kke::solveHumanArm(m, low, arm, front);
    EXPECT_LT(std::abs(pos(m, low, r.upper).y - restShoulderY), 0.015f);

    // Off: the shoulder stays where it is.
    kke::ArmLimits still;
    still.shoulderShrug = still.shoulderReach = 0.0f;
    Pose fixed = restPose(m);
    kke::solveHumanArm(m, fixed, arm, overhead, still);
    EXPECT_NEAR(pos(m, fixed, r.upper).y, restShoulderY, 1e-4f);
}

// Straight up, the hand can't go far across behind the head (the range
// closes in overhead).
TEST(AnimRig, HumanArmRangeClosesInOverhead) {
    const ModelData m = arms();
    const kke::TwoBoneChain r = kke::findChain(m, "upperarm_r", "lowerarm_r", "hand_r");
    const kke::TwoBoneChain l = kke::findChain(m, "upperarm_l", "lowerarm_l", "hand_l");
    const kke::HumanArm arm = kke::makeHumanArm(m, r, l);
    Pose p = restPose(m);
    kke::ArmGoal goal;
    goal.hand = glm::vec3(0.1f, 1.4f + 0.54f, -0.05f); // up, across to the left and behind the head
    const kke::ArmResult res = kke::solveHumanArm(m, p, arm, goal);
    EXPECT_TRUE(res.limited);
    const glm::vec3 d = pos(m, p, r.end) - pos(m, p, r.upper);
    // Measured from straight ahead toward the other side (+X here).
    const float across = glm::degrees(std::atan2(d.x, d.z));
    EXPECT_GT(across, 0.0f);
    EXPECT_LT(across, 45.0f); // at shoulder height it could go to 70
}

// Clips that walk away from their start (root motion baked in) play in
// place; the sway stays; a turn in place isn't touched.
TEST(AnimRig, ClipsThatTravelPlayInPlace) {
    ModelData m;
    m.bones.push_back(bone("Root", -1, glm::mat4(1.0f)));
    m.bones.push_back(bone("Hips", 0, at(0, 0.5f, 0)));
    m.bones.push_back(bone("Head", 1, at(0, 0, 0.4f)));
    kke::ModelAnimation walk;
    walk.name = "Walk";
    walk.duration = 1.0f;
    walk.sampleRate = 30.0f;
    kke::ModelAnimation turn = walk;
    turn.name = "Turn";
    for (int f = 0; f <= 30; ++f) {
        const float t = static_cast<float>(f) / 30.0f;
        const float sway = 0.03f * std::sin(t * 6.2831853f);
        walk.frames.push_back({ glm::mat4(1.0f), at(sway, 0.5f, 1.2f * t), at(0, 0, 0.4f) });
        turn.frames.push_back({ glm::mat4(1.0f), at(0, 0.5f, 0) * glm::rotate(glm::mat4(1.0f), 1.5f * t, glm::vec3(0, 1, 0)), at(0, 0, 0.4f) });
    }
    m.animations = { walk, turn };
    EXPECT_EQ(kke::makeClipsInPlace(m), 1);
    const auto& w = m.animations[0].frames;
    EXPECT_NEAR(w.back()[1][3].z, w.front()[1][3].z, 1e-4f);  // no travel
    EXPECT_NEAR(w[7][1][3].x, 0.03f * std::sin(7.0f / 30.0f * 6.2831853f), 1e-4f); // sway kept
    EXPECT_NEAR(w[15][1][3].y, 0.5f, 1e-4f);
    EXPECT_NEAR(m.animations[1].frames.back()[1][3].x, 0.0f, 1e-6f); // the turn untouched
}

// With the real pack (KKE_ASSETS_DIR or assets/synty): POLYGON Dogs' walk
// has its travel baked into the skeleton; loaded, no bone walks away.
TEST(AnimRig, SyntyDogWalkPlaysInPlaceIfInstalled) {
    namespace fs = std::filesystem;
    std::vector<fs::path> roots = { fs::path(KKE_SOURCE_DIR) / "assets/synty" };
    if (const char* dir = std::getenv("KKE_ASSETS_DIR")) roots.insert(roots.begin(), dir);
    fs::path clip;
    for (const fs::path& r : roots)
        if (fs::exists(r / "POLYGON_Dogs/FBX/Animations/Locomotion/_POLYGON_Dog_Locomotion_Walking.fbx"))
            clip = r / "POLYGON_Dogs/FBX/Animations/Locomotion/_POLYGON_Dog_Locomotion_Walking.fbx";
    if (clip.empty()) GTEST_SKIP() << "POLYGON Dogs not installed";
    kke::ModelLoadOptions o;
    o.allowNoMeshes = true;
    auto maxTravel = [](const ModelData& m) {
        const kke::ModelAnimation& a = m.animations.front();
        auto worldOf = [&](const std::vector<glm::mat4>& locals) {
            std::vector<glm::mat4> w(m.bones.size());
            for (size_t b = 0; b < m.bones.size(); ++b) w[b] = m.bones[b].parent >= 0 ? w[m.bones[b].parent] * locals[b] : locals[b];
            return w;
        };
        const auto first = worldOf(a.frames.front()), last = worldOf(a.frames.back());
        float most = 0.0f;
        for (size_t b = 0; b < m.bones.size(); ++b) {
            glm::vec3 d = glm::vec3(last[b][3]) - glm::vec3(first[b][3]);
            d.y = 0.0f;
            most = std::max(most, glm::length(d));
        }
        return most;
    };
    o.clipsInPlace = false;
    const ModelData raw = kke::loadModel(clip.string(), o);
    ASSERT_FALSE(raw.animations.empty());
    o.clipsInPlace = true;
    const ModelData inPlace = kke::loadModel(clip.string(), o);
    std::printf("dog walk: travels %.3f m as authored, %.3f m in place\n", maxTravel(raw), maxTravel(inPlace));
    EXPECT_LT(maxTravel(inPlace), 0.03f);
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

TEST(AnimRig, AppendClipsByBoneNameReordersAndFillsRest) {
    ModelData rig = legs();
    // The same skeleton listed in another order, one bone missing.
    ModelData src;
    src.bones.push_back(bone("Root", -1, glm::mat4(1.0f)));
    src.bones.push_back(bone("Pelvis", 0, at(0, 0.9f, 0)));
    src.bones.push_back(bone("Thigh_R", 1, at(0.1f, 0, 0)));
    src.bones.push_back(bone("Thigh_L", 1, at(-0.1f, 0, 0)));
    kke::ModelAnimation clip;
    clip.name = "Wiggle";
    clip.duration = 1.0f;
    clip.frames.assign(2, std::vector<glm::mat4>(src.bones.size(), glm::mat4(1.0f)));
    clip.frames[1][3] = at(-0.1f, 0.2f, 0); // Thigh_L lifts
    src.animations.push_back(clip);

    EXPECT_EQ(kke::appendClipsByBoneName(rig, src), 1u);
    ASSERT_EQ(rig.animations.size(), 1u);
    const kke::ModelAnimation& a = rig.animations[0];
    EXPECT_EQ(a.name, "Wiggle");
    ASSERT_EQ(a.frames[1].size(), rig.bones.size());
    const int thighL = rig.findBone("Thigh_L"), calfL = rig.findBone("calf_l");
    EXPECT_FLOAT_EQ(a.frames[1][static_cast<size_t>(thighL)][3].y, 0.2f);
    // Not in the source: stays at rest.
    EXPECT_FLOAT_EQ(a.frames[1][static_cast<size_t>(calfL)][3].y, -0.45f);
}

TEST(AnimRig, HumanArmReachesInFrontWithTheElbowDown) {
    const ModelData m = arms();
    Pose p = restPose(m);
    const kke::TwoBoneChain r = kke::findChain(m, "upperarm_r", "lowerarm_r", "hand_r");
    const kke::TwoBoneChain l = kke::findChain(m, "upperarm_l", "lowerarm_l", "hand_l");
    const kke::HumanArm arm = kke::makeHumanArm(m, r, l);
    ASSERT_TRUE(arm.valid());
    EXPECT_FALSE(arm.left);
    kke::ArmGoal goal;
    goal.hand = glm::vec3(-0.15f, 1.3f, 0.4f); // in front of the right shoulder, a little low
    const kke::ArmResult res = kke::solveHumanArm(m, p, arm, goal);
    EXPECT_FALSE(res.limited);
    EXPECT_LT(glm::length(pos(m, p, r.end) - goal.hand), 0.005f);
    EXPECT_NEAR(glm::length(pos(m, p, r.lower) - pos(m, p, r.upper)), 0.3f, 1e-3f);
    EXPECT_NEAR(glm::length(pos(m, p, r.end) - pos(m, p, r.lower)), 0.25f, 1e-3f);
    EXPECT_LT(pos(m, p, r.lower).y, 1.38f); // the elbow hangs below the shoulder
}

TEST(AnimRig, HumanArmElbowOnlyBendsForward) {
    const ModelData m = arms();
    const kke::TwoBoneChain r = kke::findChain(m, "upperarm_r", "lowerarm_r", "hand_r");
    const kke::TwoBoneChain l = kke::findChain(m, "upperarm_l", "lowerarm_l", "hand_l");
    const kke::HumanArm arm = kke::makeHumanArm(m, r, l);
    // Targets all round the shoulder, near and far, with elbow hints on every side.
    int n = 0;
    for (float yaw = -180.0f; yaw < 180.0f; yaw += 30.0f)
        for (float pitch = -80.0f; pitch <= 80.0f; pitch += 40.0f)
            for (float reach : { 0.25f, 0.45f, 0.6f })
                for (const glm::vec3 hint : { glm::vec3(0, 3, 0), glm::vec3(0, -3, 0), glm::vec3(3, 1.4f, 0), glm::vec3(0, 1.4f, 3) }) {
                    Pose p = restPose(m);
                    const glm::vec3 shoulder = pos(m, p, r.upper);
                    const float y = glm::radians(yaw), x = glm::radians(pitch);
                    kke::ArmGoal goal;
                    goal.hand = shoulder + reach * glm::vec3(std::cos(x) * std::sin(y), std::sin(x), std::cos(x) * std::cos(y));
                    goal.elbowToward = hint;
                    kke::solveHumanArm(m, p, arm, goal);
                    const std::vector<glm::mat4> w = kke::poseToModel(m, p);
                    const glm::vec3 s = glm::vec3(w[r.upper][3]), e = glm::vec3(w[r.lower][3]), h = glm::vec3(w[r.end][3]);
                    // The hinge as the upper arm carries it: the forearm turns about it the positive (forward) way.
                    const glm::vec3 hinge = glm::normalize(glm::mat3(w[r.upper]) * arm.upperHinge);
                    const float bend = glm::dot(glm::cross(glm::normalize(e - s), glm::normalize(h - e)), hinge);
                    EXPECT_GE(bend, -1e-3f) << "yaw " << yaw << " pitch " << pitch << " reach " << reach;
                    // The elbow is a hinge: the forearm stays in the plane the hinge allows.
                    EXPECT_NEAR(glm::dot(glm::normalize(h - e), hinge), 0.0f, 1e-3f);
                    ++n;
                }
    EXPECT_EQ(n, 12 * 5 * 3 * 4);
}

TEST(AnimRig, HumanArmCannotReachThroughItsBack) {
    const ModelData m = arms();
    Pose p = restPose(m);
    const kke::TwoBoneChain r = kke::findChain(m, "upperarm_r", "lowerarm_r", "hand_r");
    const kke::TwoBoneChain l = kke::findChain(m, "upperarm_l", "lowerarm_l", "hand_l");
    const kke::HumanArm arm = kke::makeHumanArm(m, r, l);
    // Behind the back on the other side: out of the shoulder's range.
    kke::ArmGoal goal;
    goal.hand = glm::vec3(0.3f, 1.4f, -0.4f);
    const kke::ArmResult res = kke::solveHumanArm(m, p, arm, goal);
    EXPECT_TRUE(res.limited);
    const glm::vec3 d = res.hand - pos(m, p, r.upper);
    // Right arm: out is -X. The hand stays within 135 degrees of straight ahead, on its own side.
    EXPECT_LT(std::atan2(-d.x, d.z), glm::radians(136.0f));
    EXPECT_GT(std::atan2(-d.x, d.z), glm::radians(-116.0f));
}

TEST(AnimRig, HumanArmForearmTakesTheTwistWithinItsRange) {
    const ModelData m = arms();
    const kke::TwoBoneChain r = kke::findChain(m, "upperarm_r", "lowerarm_r", "hand_r");
    const kke::HumanArm arm = kke::makeHumanArm(m, r);
    kke::ArmGoal goal;
    goal.hand = glm::vec3(-0.75f, 1.4f, 0.0f); // straight out
    Pose base = restPose(m);
    const glm::quat neutral = kke::solveHumanArm(m, base, arm, goal).handRotation;
    for (float turn : { 60.0f, 179.0f }) {
        Pose p = restPose(m);
        // Turned about the forearm (straight out along -X).
        goal.handRotation = glm::angleAxis(glm::radians(turn), glm::vec3(-1, 0, 0)) * neutral;
        const kke::ArmResult res = kke::solveHumanArm(m, p, arm, goal);
        const glm::quat diff = res.handRotation * glm::inverse(neutral);
        const float got = glm::degrees(2.0f * std::acos(glm::clamp(std::abs(diff.w), 0.0f, 1.0f)));
        if (turn < 90.0f) {
            EXPECT_FALSE(res.limited);
            EXPECT_NEAR(got, turn, 0.5f);
        } else {
            EXPECT_TRUE(res.limited);
            EXPECT_NEAR(got, 90.0f + 15.0f, 0.5f); // pronation plus the wrist's own twist
        }
    }
}
