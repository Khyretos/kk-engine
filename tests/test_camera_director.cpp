#include "kke/CameraDirector.h"

#include <gtest/gtest.h>

#include <cmath>

using kke::Camera;
using kke::CameraDirector;

namespace {
Camera gameplay() {
    Camera c;
    c.position = glm::vec3(0, 2, 5);
    c.target = glm::vec3(0, 1, 0);
    c.fovDegrees = 60.0f;
    return c;
}
std::vector<CameraDirector::Shot> twoShots() {
    return { { glm::vec3(10, 5, 0), glm::vec3(0, 0, 0), 0.0f, 0.0f }, { glm::vec3(-10, 5, 0), glm::vec3(0, 0, 0), 0.0f, 4.0f } };
}
// Runs the director for `seconds` in 60 Hz steps and returns the last camera.
Camera run(CameraDirector& d, float seconds, bool* finished = nullptr) {
    Camera c = gameplay();
    for (int i = 0; i < static_cast<int>(std::lround(seconds * 60.0f)); ++i) {
        c = gameplay();
        if (d.update(1.0f / 60.0f, c).finished && finished) *finished = true;
    }
    return c;
}
float distance(const glm::vec3& a, const glm::vec3& b) { return glm::length(a - b); }
} // namespace

TEST(CameraDirector, IdleLeavesTheGameplayCameraAlone) {
    CameraDirector d;
    Camera c = gameplay();
    EXPECT_FALSE(d.update(0.016f, c).changed);
    EXPECT_EQ(c.position, gameplay().position);
    EXPECT_FALSE(d.directing());
}

TEST(CameraDirector, ThePathGoesThroughEveryShotAndIsSmooth) {
    std::vector<CameraDirector::Shot> shots = { { glm::vec3(0, 0, 0), {}, 0, 0 }, { glm::vec3(4, 0, 0), {}, 0, 1 },
                                                { glm::vec3(4, 0, 4), {}, 0, 2 }, { glm::vec3(0, 0, 4), {}, 0, 3 } };
    for (const auto& s : shots) EXPECT_LT(distance(CameraDirector::sample(shots, s.time).position, s.position), 1e-4f);
    // No jumps: 1 ms apart is never more than a few centimetres apart.
    for (float t = 0.0f; t < 3.0f; t += 0.001f)
        EXPECT_LT(distance(CameraDirector::sample(shots, t).position, CameraDirector::sample(shots, t + 0.001f).position), 0.03f) << t;
    // Clamped outside the path.
    EXPECT_LT(distance(CameraDirector::sample(shots, -5.0f).position, shots.front().position), 1e-4f);
    EXPECT_LT(distance(CameraDirector::sample(shots, 9.0f).position, shots.back().position), 1e-4f);
}

TEST(CameraDirector, BlendsInFromGameplayWithoutAJump) {
    CameraDirector d;
    CameraDirector::PathOptions o;
    o.blendIn = 1.0f;
    d.play(twoShots(), o);
    Camera first = gameplay();
    d.update(1.0f / 60.0f, first);
    EXPECT_LT(distance(first.position, gameplay().position), 0.1f); // the first frame is still nearly gameplay
    const Camera later = run(d, 1.5f);
    const glm::vec3 onPath = CameraDirector::sample(twoShots(), d.time()).position;
    EXPECT_LT(distance(later.position, onPath), 1e-3f); // fully the path after the blend
}

TEST(CameraDirector, EndsOnceAndBlendsBackToGameplay) {
    CameraDirector d;
    CameraDirector::PathOptions o;
    o.blendIn = 0.0f;
    o.blendOut = 0.5f;
    d.play(twoShots(), o);
    EXPECT_FLOAT_EQ(d.duration(), 4.0f);
    bool finished = false;
    Camera c = run(d, 4.1f, &finished);
    EXPECT_TRUE(finished);
    EXPECT_TRUE(d.directing()); // blending out
    EXPECT_FALSE(d.playing());
    bool again = false;
    c = run(d, 0.6f, &again);
    EXPECT_FALSE(again);
    EXPECT_FALSE(d.directing());
    EXPECT_EQ(c.position, gameplay().position);
}

TEST(CameraDirector, HoldStaysOnTheLastShotUntilReleased) {
    CameraDirector d;
    CameraDirector::PathOptions o;
    o.blendIn = 0.0f;
    o.hold = true;
    d.play(twoShots(), o);
    bool finished = false;
    Camera c = run(d, 10.0f, &finished);
    EXPECT_TRUE(finished);
    EXPECT_TRUE(d.directing());
    EXPECT_LT(distance(c.position, twoShots().back().position), 1e-4f);
    d.release(0.0f);
    c = run(d, 0.1f);
    EXPECT_EQ(c.position, gameplay().position);
}

TEST(CameraDirector, LoopingNeverFinishes) {
    CameraDirector d;
    CameraDirector::PathOptions o;
    o.loop = true;
    d.play(twoShots(), o);
    bool finished = false;
    run(d, 20.0f, &finished);
    EXPECT_FALSE(finished);
    EXPECT_TRUE(d.playing());
}

TEST(CameraDirector, ReleaseHalfWayThroughABlendDoesNotJump) {
    CameraDirector d;
    CameraDirector::Shot s{ glm::vec3(20, 10, 0), glm::vec3(0), 30.0f, 0.0f };
    d.hold(s, 1.0f);
    Camera before = run(d, 0.5f);
    d.release(1.0f);
    Camera after = gameplay();
    d.update(1.0f / 60.0f, after);
    EXPECT_LT(distance(before.position, after.position), 0.2f);
    EXPECT_NEAR(before.fovDegrees, after.fovDegrees, 0.5f);
}

TEST(CameraDirector, ANewPathBlendsFromTheShotOnScreen) {
    CameraDirector d;
    d.hold({ glm::vec3(20, 10, 0), glm::vec3(0), 0.0f, 0.0f }, 0.0f);
    const Camera onHold = run(d, 0.1f);
    CameraDirector::PathOptions o;
    o.blendIn = 1.0f;
    d.play(twoShots(), o);
    Camera next = gameplay();
    d.update(1.0f / 60.0f, next);
    EXPECT_LT(distance(onHold.position, next.position), 0.2f); // not back to gameplay for a frame
}

TEST(CameraDirector, FovZeroKeepsGameplaysAndSetOnesEase) {
    CameraDirector d;
    std::vector<CameraDirector::Shot> shots = { { glm::vec3(0), glm::vec3(0, 0, -1), 30.0f, 0.0f }, { glm::vec3(1, 0, 0), glm::vec3(1, 0, -1), 90.0f, 2.0f } };
    EXPECT_NEAR(CameraDirector::sample(shots, 1.0f).fovDegrees, 60.0f, 1e-3f);
    CameraDirector::PathOptions o;
    o.blendIn = 0.0f;
    d.play(twoShots(), o); // no fov in these shots
    EXPECT_FLOAT_EQ(run(d, 1.0f).fovDegrees, 60.0f);
}

TEST(CameraDirector, ShakeMovesTheCameraAndFadesOut) {
    CameraDirector d;
    d.shake(1.0f, 0.5f);
    EXPECT_TRUE(d.shaking());
    EXPECT_FALSE(d.directing());
    float most = 0.0f;
    Camera c = gameplay();
    for (int i = 0; i < 10; ++i) {
        c = gameplay();
        EXPECT_TRUE(d.update(1.0f / 60.0f, c).changed);
        most = std::max(most, distance(c.position, gameplay().position));
    }
    EXPECT_GT(most, 0.01f);
    EXPECT_LT(most, 0.3f);
    run(d, 1.0f);
    EXPECT_FALSE(d.shaking());
    c = gameplay();
    EXPECT_FALSE(d.update(1.0f / 60.0f, c).changed);
    // Stacking is capped at 1: two 0.8s shake like one 1.0.
    CameraDirector twice, once;
    twice.shake(0.8f, 1.0f);
    twice.shake(0.8f, 1.0f);
    once.shake(1.0f, 1.0f);
    Camera a = gameplay(), b = gameplay();
    twice.update(0.1f, a);
    once.update(0.1f, b);
    EXPECT_LT(distance(a.position, b.position), 1e-5f);
    EXPECT_GT(distance(a.position, gameplay().position), 0.0f);
}
