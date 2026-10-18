// Depth of field (ENG-F10 part 3): the thin-lens circle, the auto focus's
// follow and the gather's samples, as shaders/dof.glsl uses them.

#include "kke/DepthOfField.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace kke;

TEST(DepthOfField, FocalLengthFromTheFieldOfView) {
    // A 24 mm high sensor: about 27 degrees is a 50 mm lens, 90 degrees a 12 mm one.
    EXPECT_NEAR(dofFocalLength(2.0f * std::atan(12.0f / 50.0f)), 50.0f, 1e-3f);
    EXPECT_NEAR(dofFocalLength(1.5707963f), 12.0f, 1e-3f);
}

TEST(DepthOfField, SharpAtTheFocusNegativeInFrontPositiveBehind) {
    const float h = 1080.0f, most = 100.0f;
    EXPECT_NEAR(dofCircle(5.0f, 5.0f, 50.0f, 2.8f, h, most), 0.0f, 1e-3f);
    EXPECT_LT(dofCircle(2.0f, 5.0f, 50.0f, 2.8f, h, most), 0.0f);
    EXPECT_GT(dofCircle(20.0f, 5.0f, 50.0f, 2.8f, h, most), 0.0f);
}

TEST(DepthOfField, ThinLensSize) {
    // 50 mm at f/2 focused at 2 m, a point at infinity (far away): diameter f^2 / (N (s - f)) = 2500 / (2 * 1950) mm
    // = 0.641 mm on a 24 mm sensor, so the radius is 0.641 / 24 / 2 of the image height.
    const float r = dofCircle(1e6f, 2.0f, 50.0f, 2.0f, 1000.0f, 1000.0f);
    EXPECT_NEAR(r, 2500.0f / (2.0f * 1950.0f) / 24.0f * 0.5f * 1000.0f, 0.05f);
}

TEST(DepthOfField, WiderApertureLongerLensMoreBlur) {
    const float h = 1080.0f, most = 1000.0f;
    const float base = dofCircle(10.0f, 3.0f, 35.0f, 4.0f, h, most);
    EXPECT_GT(dofCircle(10.0f, 3.0f, 35.0f, 1.4f, h, most), base);
    EXPECT_GT(dofCircle(10.0f, 3.0f, 85.0f, 4.0f, h, most), base);
}

TEST(DepthOfField, CircleIsCappedBothWays) {
    EXPECT_FLOAT_EQ(dofCircle(0.2f, 50.0f, 85.0f, 1.2f, 2160.0f, 12.0f), -12.0f);
    EXPECT_FLOAT_EQ(dofCircle(1e5f, 0.5f, 85.0f, 1.2f, 2160.0f, 12.0f), 12.0f);
}

TEST(DepthOfField, FocusNeverInsideTheLens) {
    // A focus nearer than the focal length would flip the sign; it is held just past it.
    const float r = dofCircle(10.0f, 0.01f, 50.0f, 2.8f, 1080.0f, 1e6f);
    EXPECT_TRUE(std::isfinite(r));
    EXPECT_GT(r, 0.0f);
}

TEST(DepthOfField, AutoFocusJumpsFirstThenFollowsEvenlyInLogDistance) {
    EXPECT_FLOAT_EQ(dofFollow(0.0f, 7.0f, 6.0f, 0.016f), 7.0f);
    // Halfway in time constant terms: the same share of the way to 4x as to 1/4x, in log distance.
    const float out = dofFollow(2.0f, 8.0f, 1.0f, 0.5f), in = dofFollow(2.0f, 0.5f, 1.0f, 0.5f);
    EXPECT_NEAR(std::log(out / 2.0f), -std::log(in / 2.0f), 1e-4f);
    EXPECT_GT(out, 2.0f);
    EXPECT_LT(out, 8.0f);
    float f = 2.0f;
    for (int i = 0; i < 200; ++i) f = dofFollow(f, 30.0f, 6.0f, 1.0f / 60.0f);
    EXPECT_NEAR(f, 30.0f, 0.01f);
}

TEST(DepthOfField, TapsFillTheDiscEvenlyFromTheMiddleOut) {
    const int n = 48;
    float last = 0.0f, meanSquared = 0.0f;
    glm::vec2 sum(0.0f);
    for (int i = 0; i < n; ++i) {
        const glm::vec2 t = dofTap(i, n);
        const float r = glm::length(t);
        EXPECT_LE(r, 1.0f);
        EXPECT_GE(r, last); // sorted outward
        last = r;
        meanSquared += r * r / n;
        sum += t;
    }
    EXPECT_NEAR(meanSquared, 0.5f, 0.01f);        // even by area: r^2 is uniform
    EXPECT_LT(glm::length(sum) / n, 0.05f);       // no side favoured
}
