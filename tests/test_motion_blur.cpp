// Motion blur from the camera's movement (ENG-F10 part 4): each pixel's
// motion through the depth and last frame's camera, the smear's length and
// the depth weighing, as shaders/motion_blur.glsl uses them.

#include "kke/MotionBlur.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

using namespace kke;

namespace {

glm::mat4 projection() { return glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 500.0f); }

// The NDC depth of a point `distance` metres straight ahead.
float depthAhead(const glm::mat4& proj, float distance) {
    const glm::vec4 c = proj * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
    return c.z / c.w;
}

} // namespace

TEST(MotionBlur, StillCameraStaysStill) {
    const glm::mat4 viewProj = projection() * glm::lookAt(glm::vec3(0, 2, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::mat4 reproject = viewProj * glm::inverse(viewProj);
    const glm::vec2 uv(0.3f, 0.7f);
    const glm::vec2 prev = motionPrevious(uv, 0.95f, reproject);
    EXPECT_NEAR(prev.x, uv.x, 1e-4f);
    EXPECT_NEAR(prev.y, uv.y, 1e-4f);
}

TEST(MotionBlur, SidewaysStepMovesNearPointsMoreThanFarOnes) {
    const glm::mat4 proj = projection();
    const glm::mat4 now = proj * glm::lookAt(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    const glm::mat4 before = proj * glm::lookAt(glm::vec3(-0.5f, 0, 0), glm::vec3(-0.5f, 0, -1), glm::vec3(0, 1, 0));
    const glm::mat4 reproject = before * glm::inverse(now);
    const glm::vec2 centre(0.5f);
    const float nearShift = motionPrevious(centre, depthAhead(proj, 2.0f), reproject).x - 0.5f;
    const float farShift = motionPrevious(centre, depthAhead(proj, 50.0f), reproject).x - 0.5f;
    EXPECT_GT(nearShift, 0.0f); // the camera came from the left: things were further right in its view
    EXPECT_GT(nearShift, farShift * 10.0f);
}

TEST(MotionBlur, SmearIsTheShutterShareCappedInLength) {
    const glm::vec2 s = motionSmear(glm::vec2(10.0f, 0.0f), 0.5f, 100.0f);
    EXPECT_FLOAT_EQ(s.x, 5.0f);
    const glm::vec2 capped = motionSmear(glm::vec2(300.0f, 400.0f), 1.0f, 50.0f);
    EXPECT_NEAR(glm::length(capped), 50.0f, 1e-3f);
    EXPECT_NEAR(capped.x / capped.y, 0.75f, 1e-4f); // the direction kept
    EXPECT_FLOAT_EQ(glm::length(motionSmear(glm::vec2(0.0f), 0.5f, 50.0f)), 0.0f);
}

TEST(MotionBlur, ViewDistanceFromDepth) {
    const glm::mat4 proj = projection();
    for (float d : { 0.5f, 3.0f, 40.0f, 300.0f })
        EXPECT_NEAR(motionViewDistance(depthAhead(proj, d), proj[2][2], proj[3][2]), d, d * 1e-3f);
}

TEST(MotionBlur, InFrontIsASoftStep) {
    EXPECT_FLOAT_EQ(motionInFront(5.0f, 10.0f), 1.0f);
    EXPECT_FLOAT_EQ(motionInFront(10.0f, 5.0f), 0.0f);
    EXPECT_NEAR(motionInFront(10.0f, 10.0f), 0.5f, 1e-6f);
    // 2% of the distance wide: 1% nearer is fully in front.
    EXPECT_FLOAT_EQ(motionInFront(99.0f, 100.0f), 1.0f);
}
