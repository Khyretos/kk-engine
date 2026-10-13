// kke::EnvMap: the sky prefiltered for reflections (ENG-G06), the GGX
// sampling it is built with, and the analytic environment BRDF the lit
// shaders weight it by (shaders/pbr_common.glsl mirrors envBRDFApprox).

#include "kke/EnvMap.h"
#include "kke/Sky.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace kke;

namespace {

void expectNear(const glm::vec3& a, const glm::vec3& b, float tol) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}

glm::vec3 texelDir(int x, int y, int w) {
    const int h = w / 2;
    return equirectDir({ (static_cast<float>(x) + 0.5f) / static_cast<float>(w), (static_cast<float>(y) + 0.5f) / static_cast<float>(h) });
}

// Solid-angle weighted mean of one level (rows are equal steps of theta).
glm::vec3 levelMean(const EnvMap& m, int level) {
    const int w = m.levelWidth(level), h = w / 2;
    glm::vec3 sum(0.0f);
    float wsum = 0.0f;
    for (int y = 0; y < h; ++y) {
        const float s = std::sin((static_cast<float>(y) + 0.5f) * 3.14159265f / static_cast<float>(h));
        for (int x = 0; x < w; ++x) {
            sum += m.texel(level, x, y) * s;
            wsum += s;
        }
    }
    return sum / wsum;
}

glm::vec3 skyish(const glm::vec3& d) {
    return d.y > 0.0f ? glm::vec3(0.2f, 0.4f, 1.0f) * (0.5f + d.y) : glm::vec3(0.1f);
}

} // namespace

TEST(EnvMapSampling, HammersleyPoints) {
    const glm::vec2 a = hammersley(0, 4), b = hammersley(1, 4), c = hammersley(3, 4);
    EXPECT_FLOAT_EQ(a.x, 0.0f);
    EXPECT_FLOAT_EQ(a.y, 0.0f);
    EXPECT_FLOAT_EQ(b.x, 0.25f);
    EXPECT_FLOAT_EQ(b.y, 0.5f);
    EXPECT_FLOAT_EQ(c.x, 0.75f);
    EXPECT_FLOAT_EQ(c.y, 0.75f);
}

TEST(EnvMapSampling, GgxHalfVectorAngle) {
    // alpha = 1: cos(theta) = sqrt(1 - xi.y), whatever the tangent frame.
    for (const glm::vec3& n : { glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), glm::normalize(glm::vec3(1, -2, 0.5f)) }) {
        const glm::vec3 h = importanceSampleGGX({ 0.25f, 0.75f }, n, 1.0f);
        EXPECT_NEAR(glm::length(h), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::dot(h, n), 0.5f, 1e-5f);
    }
    // A mirror (alpha 0) gives the normal itself.
    const glm::vec3 n = glm::normalize(glm::vec3(0.3f, 0.9f, -0.2f));
    expectNear(importanceSampleGGX({ 0.6f, 0.9f }, n, 0.0f), n, 1e-5f);
}

TEST(EnvMapBrdf, KarisAnalyticFit) {
    // Karis, "Physically Based Shading on Mobile" (2014), worked by hand.
    expectNear(envBRDFApprox(glm::vec3(0.04f), 0.0f, 1.0f), glm::vec3(0.045638f), 1e-4f);
    expectNear(envBRDFApprox(glm::vec3(0.04f), 1.0f, 1.0f), glm::vec3(0.015696f), 1e-4f);
    // Never negative for real materials (F0 >= 0.04).
    for (float r = 0.0f; r <= 1.0f; r += 0.125f)
        for (float nv = 0.0f; nv <= 1.0f; nv += 0.125f)
            for (float f0 : { 0.04f, 0.5f, 1.0f }) EXPECT_GE(envBRDFApprox(glm::vec3(f0), r, nv).x, 0.0f) << r << " " << nv << " " << f0;
}

TEST(EnvMapPrefilter, LevelSizesAndRoughness) {
    const EnvMap m = EnvMap::prefilter([](const glm::vec3&) { return glm::vec3(1.0f); }, 64, 4, 16);
    ASSERT_EQ(m.levelCount(), 4);
    EXPECT_EQ(m.levelWidth(0), 64);
    EXPECT_EQ(m.levelWidth(3), 8);
    EXPECT_EQ(m.level(2).size(), static_cast<size_t>(16 * 8));
    EXPECT_FLOAT_EQ(EnvMap::levelRoughness(0, 4), 0.0f);
    EXPECT_FLOAT_EQ(EnvMap::levelRoughness(3, 4), 1.0f);
    EXPECT_NEAR(EnvMap::levelRoughness(1, 4), 1.0f / 3.0f, 1e-6f);
}

TEST(EnvMapPrefilter, UniformSkyStaysUniform) {
    const glm::vec3 c(0.3f, 0.5f, 0.9f);
    const EnvMap m = EnvMap::prefilter([&](const glm::vec3&) { return c; });
    ASSERT_EQ(m.levelCount(), EnvMap::kLevels);
    for (int l = 0; l < m.levelCount(); ++l) {
        const int w = m.levelWidth(l);
        for (int y = 0; y < w / 2; ++y)
            for (int x = 0; x < w; ++x) expectNear(m.texel(l, x, y), c, 1e-4f);
    }
}

TEST(EnvMapPrefilter, LevelZeroIsTheRadianceAtTexelCentres) {
    const EnvMap m = EnvMap::prefilter(skyish, 32, 3, 32);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 32; ++x) expectNear(m.texel(0, x, y), skyish(texelDir(x, y, 32)), 1e-6f);
}

TEST(EnvMapPrefilter, EveryLevelKeepsTheEnergy) {
    const EnvMap m = EnvMap::prefilter(skyish);
    const glm::vec3 base = levelMean(m, 0);
    for (int l = 1; l < m.levelCount(); ++l) {
        const glm::vec3 mean = levelMean(m, l);
        for (int k = 0; k < 3; ++k) EXPECT_NEAR(mean[k], base[k], 0.05f * base[k]) << "level " << l << " channel " << k;
    }
}

TEST(EnvMapPrefilter, ASunSpotWidensWithRoughness) {
    const glm::vec3 sun = glm::normalize(glm::vec3(1.0f, 0.2f, 0.3f));
    const float cosR = std::cos(glm::radians(4.0f));
    const EnvMap m = EnvMap::prefilter([&](const glm::vec3& d) { return glm::dot(d, sun) > cosR ? glm::vec3(100.0f) : glm::vec3(0.0f); });
    std::vector<float> peak, spread;
    for (int l = 0; l < m.levelCount(); ++l) {
        const int w = m.levelWidth(l), h = w / 2;
        float mx = 0.0f;
        for (const glm::vec3& t : m.level(l)) mx = std::max(mx, t.x);
        float above = 0.0f, total = 0.0f;
        for (int y = 0; y < h; ++y) {
            const float s = std::sin((static_cast<float>(y) + 0.5f) * 3.14159265f / static_cast<float>(h));
            for (int x = 0; x < w; ++x) {
                if (m.texel(l, x, y).x > 0.5f * mx) above += s;
                total += s;
            }
        }
        peak.push_back(mx);
        spread.push_back(above / total);
    }
    // The brightest texel dims as the spot spreads. Not checked on the last
    // level: its 4 x 2 texel centres lie 45 degrees or more from the spot, so
    // its maximum says where the centres fall, not how wide the lobe is.
    for (int l = 1; l + 1 < m.levelCount(); ++l) EXPECT_LT(peak[l], peak[l - 1]) << "level " << l;
    for (int l = 1; l + 1 < m.levelCount(); ++l) EXPECT_GT(spread[l], spread[l - 1]) << "level " << l;
    EXPECT_GE(spread.back(), spread[spread.size() - 2]);
}

TEST(EnvMapPrefilter, SampleIsBilinearAndWrapsTheHorizon) {
    const EnvMap m = EnvMap::prefilter(skyish, 32, 2, 16);
    // At a texel centre: that texel.
    expectNear(m.sample(0, texelDir(5, 3, 32)), m.texel(0, 5, 3), 1e-4f);
    // On the seam (u = 0): halfway between the first and the last column.
    const glm::vec3 seam = equirectDir({ 0.0f, 3.5f / 16.0f });
    expectNear(m.sample(0, seam), 0.5f * (m.texel(0, 0, 3) + m.texel(0, 31, 3)), 1e-4f);
}

TEST(EnvMapPrefilter, HalfRgbaHoldsEveryLevel) {
    const EnvMap m = EnvMap::prefilter([](const glm::vec3&) { return glm::vec3(2.0f); }, 16, 3, 8);
    const std::vector<uint16_t> half = m.toHalfRgba();
    EXPECT_EQ(half.size(), static_cast<size_t>((16 * 8 + 8 * 4 + 4 * 2) * 4));
    EXPECT_EQ(half[0], 0x4000); // 2.0 as a half float
    EXPECT_EQ(half[3], 0x3c00); // alpha 1.0
    EXPECT_EQ(half[half.size() - 4], 0x4000);
}

TEST(EnvMapResolver, AGradientSkyGetsReflections) {
    Sky sky;
    sky.kind = Sky::Kind::Gradient;
    sky.ambientSaturation = 1.0f;
    sky.ambientStrength = 2.0f;
    SkyResolver r;
    const SkyEnvironment& e = r.resolve(sky, Fog{}, glm::vec3(0.1f));
    ASSERT_NE(e.reflections, nullptr);
    EXPECT_EQ(e.reflections->levelCount(), EnvMap::kLevels);
    EXPECT_EQ(e.reflections->levelWidth(0), EnvMap::kWidth);
    // The same light the ambient SH is projected from: the sky without its sun, times ambientStrength.
    for (int x : { 0, 40, 100 }) {
        expectNear(e.reflections->texel(0, x, 3), 2.0f * gradientSkyRadiance(sky, texelDir(x, 3, EnvMap::kWidth)), 1e-4f);
        expectNear(e.reflections->texel(0, x, 60), 2.0f * gradientSkyRadiance(sky, texelDir(x, 60, EnvMap::kWidth)), 1e-4f);
    }
    // Cached with the rest: resolving the same sky again does not rebuild it.
    const std::shared_ptr<const EnvMap> first = e.reflections;
    EXPECT_EQ(r.resolve(sky, Fog{}, glm::vec3(0.1f)).reflections, first);
}

TEST(EnvMapResolver, NoReflectionsWithoutASkyThatLightsTheScene) {
    SkyResolver r;
    EXPECT_EQ(r.resolve(Sky{}, Fog{}, glm::vec3(0.2f)).reflections, nullptr);
    Sky sky;
    sky.kind = Sky::Kind::Gradient;
    sky.lightsScene = false;
    EXPECT_EQ(r.resolve(sky, Fog{}, glm::vec3(0.2f)).reflections, nullptr);
}

TEST(EnvMapResolver, DesaturatedLikeTheAmbient) {
    Sky sky;
    sky.kind = Sky::Kind::Gradient;
    sky.ambientSaturation = 0.0f;
    SkyResolver r;
    const SkyEnvironment& e = r.resolve(sky, Fog{}, glm::vec3(0.1f));
    ASSERT_NE(e.reflections, nullptr);
    const glm::vec3 c = e.reflections->texel(0, 10, 5);
    EXPECT_NEAR(c.r, c.g, 1e-5f);
    EXPECT_NEAR(c.g, c.b, 1e-5f);
}
