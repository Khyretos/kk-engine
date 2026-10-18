#include "kke/Taa.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

// ENG-F05 part 2: temporal anti-aliasing's pieces (an option over the clean image).

TEST(Taa, JitterStaysInsideThePixelAndAveragesOut) {
    const kke::TaaSettings s;
    glm::vec2 sum(0.0f);
    for (uint32_t f = 0; f < s.jitterCycle; ++f) {
        const glm::vec2 j = kke::taaJitter(f, s);
        EXPECT_LE(std::abs(j.x), 0.5f);
        EXPECT_LE(std::abs(j.y), 0.5f);
        sum += j;
    }
    EXPECT_LT(std::abs(sum.x / static_cast<float>(s.jitterCycle)), 0.07f);
    EXPECT_LT(std::abs(sum.y / static_cast<float>(s.jitterCycle)), 0.07f);
    EXPECT_NE(kke::taaJitter(0, s), kke::taaJitter(1, s));
}

TEST(Taa, JitterMovesThePictureByThatMuch) {
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const glm::vec2 size(1600.0f, 900.0f);
    const glm::vec4 p(1.0f, 0.5f, -5.0f, 1.0f);
    const glm::vec4 a = proj * p, b = kke::taaJitteredProjection(proj, glm::vec2(0.25f, -0.5f), size) * p;
    const glm::vec2 moved = (glm::vec2(b) / b.w - glm::vec2(a) / a.w) * 0.5f * size; // in pixels
    EXPECT_NEAR(moved.x, 0.25f, 1e-3f);
    EXPECT_NEAR(moved.y, -0.5f, 1e-3f);
}

TEST(Taa, AStillCameraFindsThePixelWhereItWas) {
    const glm::mat4 vp = glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 100.0f) *
                         glm::lookAt(glm::vec3(0, 2, 5), glm::vec3(0), glm::vec3(0, 1, 0));
    const glm::vec2 uv = kke::taaReproject(glm::vec2(0.3f, 0.7f), 0.98f, glm::inverse(vp), vp);
    EXPECT_NEAR(uv.x, 0.3f, 1e-4f);
    EXPECT_NEAR(uv.y, 0.7f, 1e-4f);
}

TEST(Taa, ACameraMovingRightFindsThePictureFurtherRight) {
    // The camera slides right: what a pixel shows now was further right on screen last frame.
    const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 100.0f);
    const glm::mat4 now = proj * glm::lookAt(glm::vec3(0.1f, 0, 5), glm::vec3(0.1f, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 before = proj * glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::vec2 uv = kke::taaReproject(glm::vec2(0.5f, 0.5f), 0.99f, glm::inverse(now), before);
    EXPECT_GT(uv.x, 0.5f);
    EXPECT_NEAR(uv.y, 0.5f, 1e-4f);
}

TEST(Taa, AStaleColourIsPulledToTheNeighbourhood) {
    // History says white, but around the pixel everything is now dark red: it may not trail behind.
    const glm::vec3 mean = kke::taaToYCoCg(glm::vec3(0.3f, 0.02f, 0.02f)), dev(0.02f);
    const glm::vec3 h = kke::taaToYCoCg(glm::vec3(1.0f));
    const glm::vec3 clipped = kke::taaClip(h, mean, dev, 1.0f);
    EXPECT_LE(std::abs(clipped.x - mean.x), 0.02f + 1e-5f);
    // A history inside the box is left alone.
    EXPECT_EQ(kke::taaClip(mean + glm::vec3(0.01f), mean, dev, 1.0f), mean + glm::vec3(0.01f));
}

TEST(Taa, YCoCgGoesBothWays) {
    const glm::vec3 c(0.2f, 0.7f, 0.4f);
    const glm::vec3 back = kke::taaFromYCoCg(kke::taaToYCoCg(c));
    EXPECT_NEAR(back.r, c.r, 1e-5f);
    EXPECT_NEAR(back.g, c.g, 1e-5f);
    EXPECT_NEAR(back.b, c.b, 1e-5f);
}
