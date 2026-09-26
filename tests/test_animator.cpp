#include "kke/Animator.h"

#include <gtest/gtest.h>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace {

// One bone, clips that rotate it about Y at a constant angle, or sweep
// from 0 to 90 degrees over their length.
kke::ModelData model() {
    kke::ModelData m;
    kke::ModelBone b;
    b.name = "root";
    m.bones.push_back(b);
    auto constant = [](const char* name, float degrees, float duration) {
        kke::ModelAnimation a;
        a.name = name;
        a.duration = duration;
        a.sampleRate = 30.0f;
        int n = static_cast<int>(duration * 30.0f) + 1;
        for (int f = 0; f < n; ++f) a.frames.push_back({ glm::rotate(glm::mat4(1.0f), glm::radians(degrees), glm::vec3(0, 1, 0)) });
        return a;
    };
    m.animations.push_back(constant("Idle_Loop", 0.0f, 1.0f));
    m.animations.push_back(constant("Walk_Loop", 60.0f, 1.0f));
    kke::ModelAnimation sweep;
    sweep.name = "Sweep";
    sweep.duration = 1.0f;
    for (int f = 0; f <= 30; ++f) sweep.frames.push_back({ glm::rotate(glm::mat4(1.0f), glm::radians(90.0f * f / 30.0f), glm::vec3(0, 1, 0)) });
    m.animations.push_back(sweep);
    // Moves the bone up 1 m.
    kke::ModelAnimation up;
    up.name = "Up";
    up.duration = 1.0f;
    for (int f = 0; f <= 30; ++f) up.frames.push_back({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0)) });
    m.animations.push_back(up);
    return m;
}

float yawDegrees(const kke::BoneTRS& b) { return glm::degrees(glm::eulerAngles(b.r).y); }

} // namespace

TEST(Animator, FindsAndSamplesClips) {
    kke::ModelData m = model();
    kke::AnimationSet set(m);
    EXPECT_EQ(set.find("Walk"), 1);
    EXPECT_EQ(set.find("Nope"), -1);
    kke::Pose p;
    set.sample(2, 0.5f, false, p);
    EXPECT_NEAR(yawDegrees(p[0]), 45.0f, 1.0f);
    set.sample(2, 5.0f, false, p); // clamped
    EXPECT_NEAR(yawDegrees(p[0]), 90.0f, 1.0f);
}

// Rotations blend as rotations: halfway between 0 and 60 degrees is 30
// degrees at full length (blending matrices would shrink it).
TEST(Animator, BlendsRotationsAsRotations) {
    kke::Pose a(1), b(1), out;
    b[0].r = glm::angleAxis(glm::radians(60.0f), glm::vec3(0, 1, 0));
    kke::blendPoses(a, b, 0.5f, out);
    EXPECT_NEAR(yawDegrees(out[0]), 30.0f, 0.1f);
    std::vector<glm::mat4> locals;
    kke::poseToLocals(out, locals);
    EXPECT_NEAR(glm::length(glm::vec3(locals[0][0])), 1.0f, 1e-4f); // no shrink
}

TEST(Animator, BlendSpaceFollowsTheParameter) {
    kke::ModelData m = model();
    kke::AnimationSet set(m);
    kke::Animator anim(set);
    int move = anim.addBlendState("move", { { { 0, 0.0f }, { 1, 2.0f } } });
    anim.play(move, 0.0f);
    anim.setParameter(1.0f);
    anim.update(0.016f);
    EXPECT_NEAR(yawDegrees(anim.pose()[0]), 30.0f, 0.5f);
    anim.setParameter(5.0f);
    anim.update(0.016f);
    EXPECT_NEAR(yawDegrees(anim.pose()[0]), 60.0f, 0.5f);
}

TEST(Animator, CrossfadesBetweenStates) {
    kke::ModelData m = model();
    kke::AnimationSet set(m);
    kke::Animator anim(set);
    int idle = anim.addClipState("idle", 0);
    int walk = anim.addClipState("walk", 1);
    anim.play(idle, 0.0f);
    anim.update(0.1f);
    EXPECT_NEAR(yawDegrees(anim.pose()[0]), 0.0f, 0.5f);
    anim.play(walk, 0.2f);
    anim.update(0.1f); // halfway through the fade
    float mid = yawDegrees(anim.pose()[0]);
    EXPECT_GT(mid, 10.0f);
    EXPECT_LT(mid, 50.0f);
    anim.update(0.2f); // fade done
    EXPECT_NEAR(yawDegrees(anim.pose()[0]), 60.0f, 0.5f);
    EXPECT_EQ(anim.current(), walk);
}

TEST(Animator, OneShotStatesFinish) {
    kke::ModelData m = model();
    kke::AnimationSet set(m);
    kke::Animator anim(set);
    int sweep = anim.addClipState("sweep", 2, false);
    anim.play(sweep, 0.0f);
    anim.update(0.5f);
    EXPECT_FALSE(anim.finished());
    anim.update(0.6f);
    EXPECT_TRUE(anim.finished());
    EXPECT_NEAR(yawDegrees(anim.pose()[0]), 90.0f, 1.0f);
}

TEST(Animator, TranslationsBlendLinearly) {
    kke::ModelData m = model();
    kke::AnimationSet set(m);
    kke::Pose rest, up, out;
    set.sample(0, 0.0f, true, rest);
    set.sample(3, 0.0f, true, up);
    kke::blendPoses(rest, up, 0.25f, out);
    EXPECT_NEAR(out[0].t.y, 0.25f, 1e-4f);
}

// A climb authored in place: the bone rises 1.5 m and moves 0.3 m forward
// halfway, then snaps back. Without the lift it stays at its start height
// and keeps its forward motion (that's the pose, not travel).
TEST(Animator, RemoveLiftKeepsTheHeightOnly) {
    kke::ModelData m = model();
    kke::ModelAnimation climb;
    climb.name = "ClimbUp_2m";
    climb.duration = 1.0f;
    for (int f = 0; f <= 30; ++f) {
        const float k = std::sin(3.14159265f * f / 30.0f);
        climb.frames.push_back({ glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.8f + 1.5f * k, 0.3f * k)) });
    }
    m.animations.push_back(climb);
    kke::AnimationSet set(m);
    EXPECT_EQ(set.removeLift(m, 0, "Climb"), 1);
    const int c = set.find("ClimbUp");
    kke::Pose p;
    set.sample(c, 0.5f, false, p);
    EXPECT_NEAR(p[0].t.y, 0.8f, 1e-4f);
    EXPECT_NEAR(p[0].t.z, 0.3f, 1e-3f);
    set.sample(set.find("Up"), 0.5f, false, p); // other clips untouched
    EXPECT_NEAR(p[0].t.y, 1.0f, 1e-4f);
    EXPECT_EQ(set.removeLift(m, 5, "Climb"), 0); // no such bone
}

// A move the controller times: the clip follows the given progress, not
// the clock, and plays on normally once no longer held.
TEST(Animator, SetProgressHoldsTheClipThere) {
    kke::ModelData m = model();
    kke::AnimationSet set(m);
    kke::Animator a(set);
    const int sweep = a.addClipState("sweep", 2, false, 2.0f);
    a.play(sweep, 0.0f);
    a.setProgress(0.5f);
    a.update(0.1f);
    EXPECT_NEAR(yawDegrees(a.pose()[0]), 45.0f, 1.0f);
    a.setProgress(0.25f);
    a.update(0.3f);
    EXPECT_NEAR(yawDegrees(a.pose()[0]), 22.5f, 1.0f);
    a.update(0.1f); // 0.25 of 0.5 s + 0.1 s at speed 2 = 0.45 of the sweep
    EXPECT_NEAR(yawDegrees(a.pose()[0]), 40.5f, 1.5f);
    a.setProgress(2.0f); // clamped
    a.update(0.0f);
    EXPECT_TRUE(a.finished());
}
