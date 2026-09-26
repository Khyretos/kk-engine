#include "kke/ShadowMap.h"

#include <gtest/gtest.h>
#include <cmath>

namespace {

// Where a fixed world point lands in the shadow map, in texels.
glm::vec2 texelOf(const glm::mat4& lightViewProj, const glm::vec3& p, uint32_t res) {
    glm::vec4 c = lightViewProj * glm::vec4(p, 1.0f);
    return (glm::vec2(c) / c.w * 0.5f + 0.5f) * static_cast<float>(res);
}

} // namespace

TEST(ShadowProjection, SnappedCentreMovesInWholeTexels) {
    // A region following the camera: the same world point must land on
    // the same sub-texel position for any centre, or shadow edges shimmer.
    const glm::vec3 dir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    const uint32_t res = 2048;
    const glm::vec3 p(3.3f, 0.7f, -2.1f);
    const glm::vec2 a = texelOf(kke::ShadowMap::computeLightViewProj(dir, glm::vec3(0.0f), 15.0f, res), p, res);
    for (float t : { 0.013f, 0.37f, 1.9f, 7.77f }) {
        const glm::vec3 centre(t, 0.2f * t, -0.6f * t);
        const glm::vec2 b = texelOf(kke::ShadowMap::computeLightViewProj(dir, centre, 15.0f, res), p, res);
        const glm::vec2 shift = b - a;
        EXPECT_NEAR(shift.x, std::round(shift.x), 1e-2f) << "centre offset " << t;
        EXPECT_NEAR(shift.y, std::round(shift.y), 1e-2f) << "centre offset " << t;
    }
}

TEST(ShadowProjection, UnsnappedKeepsExactCentre) {
    const glm::vec3 dir(0.0f, -1.0f, 0.0f); // straight down: exercises the up-vector fallback
    const glm::vec3 centre(1.234f, 0.0f, 5.678f);
    glm::vec4 c = kke::ShadowMap::computeLightViewProj(dir, centre, 10.0f) * glm::vec4(centre, 1.0f);
    EXPECT_NEAR(c.x / c.w, 0.0f, 1e-5f);
    EXPECT_NEAR(c.y / c.w, 0.0f, 1e-5f);
}

TEST(ShadowProjection, CasterConfigHasSlopeBias) {
    const kke::PipelineConfig c = kke::ShadowMap::casterConfig();
    EXPECT_GT(c.depthBiasSlope, 0.0f);
    EXPECT_EQ(c.cullMode, static_cast<VkCullModeFlags>(VK_CULL_MODE_NONE));
}
