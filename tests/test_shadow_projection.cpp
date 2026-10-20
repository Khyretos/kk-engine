#include "kke/ShadowMap.h"

#include <gtest/gtest.h>
#include <array>
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
    EXPECT_EQ(c.cullMode, kke::rhi::CullMode::None);
}

namespace {

// The 8 corners of the view frustum between distances n and f.
std::array<glm::vec3, 8> sliceCorners(const glm::vec3& eye, const glm::vec3& fwd, float tanY, float aspect, float n, float f) {
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up = glm::cross(right, fwd);
    std::array<glm::vec3, 8> c;
    int i = 0;
    for (float d : { n, f })
        for (float sx : { -1.0f, 1.0f })
            for (float sy : { -1.0f, 1.0f }) c[i++] = eye + fwd * d + right * (sx * d * tanY * aspect) + up * (sy * d * tanY);
    return c;
}

} // namespace

TEST(ShadowCascades, SpheresCoverTheirSlicesNearToFar) {
    const glm::vec3 eye(4.0f, 2.0f, -3.0f);
    const glm::vec3 fwd = glm::normalize(glm::vec3(0.3f, -0.2f, 1.0f));
    const float tanY = std::tan(glm::radians(22.5f)), aspect = 16.0f / 9.0f, n = 0.1f, dist = 80.0f;
    const auto s = kke::ShadowMap::cascadeSpheres(eye, fwd, tanY, aspect, n, dist);
    ASSERT_EQ(s.size(), kke::ShadowMap::kCascades);
    // Slices meet: each starts where the one before ends, the last ends at dist.
    float start = n;
    for (uint32_t k = 0; k < s.size(); ++k) {
        const float end = kke::ShadowMap::cascadeSplit(k + 1, n, dist);
        EXPECT_GT(end, start) << "cascade " << k;
        for (const glm::vec3& c : sliceCorners(eye, fwd, tanY, aspect, start, end)) {
            EXPECT_LE(glm::length(c - glm::vec3(s[k])), s[k].w * 1.0001f + 1e-4f) << "cascade " << k;
        }
        if (k > 0) {
            EXPECT_GT(s[k].w, s[k - 1].w) << "cascade " << k;
        }
        start = end;
    }
    EXPECT_NEAR(start, dist, 1e-3f);
    EXPECT_NEAR(kke::ShadowMap::cascadeSplit(0, n, dist), n, 1e-6f);
}

TEST(ShadowCascades, RadiusIgnoresWhereTheCameraLooks) {
    // Same lens, any direction: the radii stay put, so texels keep their size.
    const float tanY = std::tan(glm::radians(30.0f));
    const auto a = kke::ShadowMap::cascadeSpheres(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), tanY, 1.5f, 0.2f, 60.0f);
    const auto b = kke::ShadowMap::cascadeSpheres(glm::vec3(9.0f, 1.0f, 2.0f), glm::normalize(glm::vec3(1.0f, -0.5f, 0.2f)), tanY, 1.5f, 0.2f, 60.0f);
    for (uint32_t k = 0; k < kke::ShadowMap::kCascades; ++k) {
        EXPECT_NEAR(a[k].w, b[k].w, 1e-4f * a[k].w) << "cascade " << k;
    }
}

TEST(ShadowCascades, ScaleOffsetMapsFarCascadeToNear) {
    // A near cascade is the far cascade's map scaled and shifted, and its
    // matrix gives the same depth (the shaders pick a cascade from the far
    // cascade's coordinates alone).
    const glm::vec3 dir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    const uint32_t res = 2048;
    const glm::vec3 farCentre(10.0f, 1.0f, 20.0f), centre(2.0f, 0.5f, 3.0f);
    const float farRadius = 40.0f, radius = 5.0f;
    const glm::mat4 far = kke::ShadowMap::computeLightViewProj(dir, farCentre, farRadius, res);
    const glm::vec4 so = kke::ShadowMap::cascadeScaleOffset(far, dir, farRadius, centre, radius, res);
    EXPECT_NEAR(so.x, farRadius / radius, 1e-4f);
    EXPECT_NEAR(so.y, farRadius / radius, 1e-4f);
    const glm::mat4 near = kke::ShadowMap::cascadeViewProj(far, so);
    // Its centre is within a texel of the middle of its map.
    const glm::vec4 c = near * glm::vec4(centre, 1.0f);
    EXPECT_LT(std::abs(c.x), 2.0f / res + 1e-5f);
    EXPECT_LT(std::abs(c.y), 2.0f / res + 1e-5f);
    for (const glm::vec3& p : { glm::vec3(3.1f, 0.0f, 1.7f), glm::vec3(-1.0f, 2.0f, 4.0f), glm::vec3(5.5f, -0.5f, 2.2f) }) {
        const glm::vec4 f = far * glm::vec4(p, 1.0f), q = near * glm::vec4(p, 1.0f);
        EXPECT_NEAR(q.x, f.x * so.x + so.z, 1e-4f);
        EXPECT_NEAR(q.y, f.y * so.y + so.w, 1e-4f);
        EXPECT_NEAR(q.z, f.z, 1e-6f);
        EXPECT_NEAR(q.w, 1.0f, 1e-6f);
    }
}

TEST(ShadowCascades, NearCascadeMovesInWholeTexels) {
    // Both the far cascade and a near one follow the camera; a fixed point
    // must keep its sub-texel position in the near one, or edges shimmer.
    const glm::vec3 dir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    const uint32_t res = 2048;
    const glm::vec3 p(3.3f, 0.7f, -2.1f);
    auto texel = [&](const glm::vec3& cam) {
        const glm::mat4 far = kke::ShadowMap::computeLightViewProj(dir, cam + glm::vec3(0.0f, 0.0f, 30.0f), 40.0f, res);
        const glm::vec4 so = kke::ShadowMap::cascadeScaleOffset(far, dir, 40.0f, cam + glm::vec3(0.0f, 0.0f, 2.0f), 4.0f, res);
        return texelOf(kke::ShadowMap::cascadeViewProj(far, so), p, res);
    };
    const glm::vec2 a = texel(glm::vec3(0.0f));
    for (float t : { 0.013f, 0.37f, 1.9f }) {
        const glm::vec2 shift = texel(glm::vec3(t, 0.2f * t, -0.6f * t)) - a;
        EXPECT_NEAR(shift.x, std::round(shift.x), 2e-2f) << "camera offset " << t;
        EXPECT_NEAR(shift.y, std::round(shift.y), 2e-2f) << "camera offset " << t;
    }
}

TEST(ShadowCascades, GridHoldsEveryTile) {
    EXPECT_EQ(kke::ShadowMap::gridFor(1), 1u);
    EXPECT_EQ(kke::ShadowMap::gridFor(4), 2u);
    EXPECT_EQ(kke::ShadowMap::gridFor(5), 3u);
    EXPECT_EQ(kke::ShadowMap::gridFor(8), 3u);
    EXPECT_EQ(kke::ShadowMap::gridFor(9), 3u);
    EXPECT_EQ(kke::ShadowMap::gridFor(16), 4u);
}

TEST(ShadowProjection, DepthCoversTheWholeBoxInVulkanRange) {
    // Vulkan clips depth outside 0..1: every caster in the light's box, on
    // either side of the centre, must land inside it, or it casts nothing.
    const glm::vec3 dir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f));
    const glm::vec3 centre(3.0f, -2.0f, 1.0f);
    const float r = 20.0f;
    const glm::mat4 m = kke::ShadowMap::computeLightViewProj(dir, centre, r, 2048);
    for (float along : { -2.9f * r, -r, 0.0f, r, 2.9f * r }) {
        const glm::vec4 c = m * glm::vec4(centre + dir * along, 1.0f);
        EXPECT_GT(c.z / c.w, 0.0f) << "along " << along;
        EXPECT_LT(c.z / c.w, 1.0f) << "along " << along;
    }
    // Closer to the light is smaller depth (the map stores the nearest).
    const glm::vec4 nearer = m * glm::vec4(centre - dir, 1.0f), farther = m * glm::vec4(centre + dir, 1.0f);
    EXPECT_LT(nearer.z, farther.z);
}

TEST(ShadowCascades, TileResolutionFollowsTheSetting) {
    // One view: 4 cascades in a 2 x 2 grid of tiles as big as the setting.
    EXPECT_EQ(kke::ShadowMap::tileResolutionFor(1024, 4), 1024u);
    EXPECT_EQ(kke::ShadowMap::tileResolutionFor(2048, 4), 2048u);
    // Split screen: the whole map stays within kMaxResolution.
    EXPECT_EQ(kke::ShadowMap::tileResolutionFor(2048, 16), kke::ShadowMap::kMaxResolution / 4);
    EXPECT_EQ(kke::ShadowMap::tileResolutionFor(1024, 16), 1024u);
}

TEST(PointShadows, FacePicksTheLargestAxis) {
    using kke::ShadowMap;
    EXPECT_EQ(ShadowMap::pointFace(glm::vec3(2.0f, 1.0f, -1.0f)), 0u);  // +X
    EXPECT_EQ(ShadowMap::pointFace(glm::vec3(-2.0f, 1.0f, 1.0f)), 1u);  // -X
    EXPECT_EQ(ShadowMap::pointFace(glm::vec3(0.5f, 3.0f, -1.0f)), 2u);  // +Y
    EXPECT_EQ(ShadowMap::pointFace(glm::vec3(0.5f, -3.0f, 1.0f)), 3u);  // -Y
    EXPECT_EQ(ShadowMap::pointFace(glm::vec3(0.5f, 1.0f, 4.0f)), 4u);   // +Z
    EXPECT_EQ(ShadowMap::pointFace(glm::vec3(-0.5f, 1.0f, -4.0f)), 5u); // -Z
}

TEST(PointShadows, EveryDirectionLandsInsideItsFace) {
    // Whatever the direction from the light, the face pointFace picks sees it,
    // at least kPointFaceMargin texels in from the tile's edge (room for the
    // normal offset and the 3x3 taps), with depth inside Vulkan's 0..1.
    using kke::ShadowMap;
    const glm::vec3 light(1.0f, 3.0f, -2.0f);
    const float range = 15.0f;
    const uint32_t res = 512;
    const float edge = 1.0f - 2.0f * static_cast<float>(ShadowMap::kPointFaceMargin) / static_cast<float>(res) + 1e-4f;
    for (int i = 0; i < 400; ++i) {
        const float a = static_cast<float>(i) * 2.399963f, y = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / 400.0f;
        const float r = std::sqrt(1.0f - y * y);
        const glm::vec3 d(r * std::cos(a), y, r * std::sin(a));
        const uint32_t face = ShadowMap::pointFace(d);
        ASSERT_LT(face, ShadowMap::kPointFaces);
        const glm::mat4 m = ShadowMap::pointFaceViewProj(light, face, range, res);
        for (float dist : { 0.5f, 5.0f, 14.0f }) {
            const glm::vec4 c = m * glm::vec4(light + d * dist, 1.0f);
            ASSERT_GT(c.w, 0.0f) << i;
            const glm::vec3 p = glm::vec3(c) / c.w;
            EXPECT_LE(std::abs(p.x), edge) << i << " " << dist;
            EXPECT_LE(std::abs(p.y), edge) << i << " " << dist;
            EXPECT_GT(p.z, 0.0f) << i << " " << dist;
            EXPECT_LT(p.z, 1.0f) << i << " " << dist;
        }
    }
}

TEST(PointShadows, FartherFromTheLightIsDeeper) {
    using kke::ShadowMap;
    const glm::vec3 light(0.0f, 2.0f, 0.0f);
    for (uint32_t face = 0; face < ShadowMap::kPointFaces; ++face) {
        glm::vec3 axis(0.0f);
        axis[face / 2] = face % 2 == 0 ? 1.0f : -1.0f;
        ASSERT_EQ(ShadowMap::pointFace(axis), face);
        const glm::mat4 m = ShadowMap::pointFaceViewProj(light, face, 20.0f, 512);
        const glm::vec4 nearer = m * glm::vec4(light + axis * 2.0f, 1.0f), farther = m * glm::vec4(light + axis * 6.0f, 1.0f);
        EXPECT_LT(nearer.z / nearer.w, farther.z / farther.w) << face;
        // Straight along the face's axis is the middle of its tile.
        EXPECT_NEAR(nearer.x / nearer.w, 0.0f, 1e-5f) << face;
        EXPECT_NEAR(nearer.y / nearer.w, 0.0f, 1e-5f) << face;
        // The far plane is the light's shadow range.
        const glm::vec4 beyond = m * glm::vec4(light + axis * 21.0f, 1.0f);
        EXPECT_GT(beyond.z / beyond.w, 1.0f) << face;
    }
}
