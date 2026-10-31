#include "kke/LodChain.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using kke::LodSettings;
using kke::ModelMesh;

namespace {

// A flat n x n grid of quads (2 n n triangles) with a bump, so it has something to lose.
ModelMesh grid(int n) {
    ModelMesh m;
    for (int y = 0; y <= n; ++y)
        for (int x = 0; x <= n; ++x) {
            kke::ModelVertex v;
            const float fx = static_cast<float>(x) / static_cast<float>(n), fy = static_cast<float>(y) / static_cast<float>(n);
            v.position = { fx, 0.15f * std::sin(fx * 6.0f) * std::cos(fy * 5.0f), fy };
            v.normal = { 0, 1, 0 };
            v.uv = { fx, fy };
            m.vertices.push_back(v);
        }
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * (n + 1) + x), b = a + 1, c = a + static_cast<uint32_t>(n + 1), d = c + 1;
            m.indices.insert(m.indices.end(), { a, c, b, b, c, d });
        }
    return m;
}

} // namespace

TEST(LodChain, BuildsCoarserLevelsOverTheSameVertices) {
    const ModelMesh m = grid(40); // 3200 triangles
    const auto chain = kke::buildLodChain(m);
    ASSERT_GE(chain.size(), 2u) << "a 3200 triangle part gets levels";
    size_t before = m.indices.size();
    for (const auto& level : chain) {
        EXPECT_EQ(level.indices.size() % 3, 0u);
        EXPECT_LT(level.indices.size(), before) << "each level is cheaper than the one before";
        EXPECT_LE(static_cast<float>(level.indices.size()), static_cast<float>(before) * 0.76f);
        for (uint32_t i : level.indices) ASSERT_LT(i, m.vertices.size()) << "the same vertices: no new ones";
        before = level.indices.size();
    }
    EXPECT_LT(chain.front().ratio, 0.8f);
    EXPECT_LT(chain.back().ratio, chain.front().ratio);
}

TEST(LodChain, SwitchSizesFallAndFollowTheError) {
    const auto chain = kke::buildLodChain(grid(40));
    ASSERT_GE(chain.size(), 2u);
    for (size_t i = 0; i < chain.size(); ++i) {
        EXPECT_GT(chain[i].switchBelow, 0.0f);
        EXPECT_LE(chain[i].switchBelow, 1.0f);
        if (i) {
            EXPECT_LT(chain[i].switchBelow, chain[i - 1].switchBelow) << "a coarser level takes over at a smaller size";
        }
    }
    // 0.5% error, 1 px tolerance on a 1080 px screen: right at 1 / (0.005 * 1080) = 18.5% of the view's height.
    EXPECT_NEAR(chain.front().switchBelow, 1.0f / (0.005f * 1080.0f), 1e-3f);
}

TEST(LodChain, SmallPartsAndIdentityMeshesGetNone) {
    EXPECT_TRUE(kke::buildLodChain(grid(8)).empty()) << "128 triangles: not worth it";
    LodSettings s;
    s.minTriangles = 10;
    s.ratios = { 0.9f };
    s.errors = { 0.0001f };
    EXPECT_TRUE(kke::buildLodChain(grid(20), s).empty()) << "a level that is not 25% cheaper is dropped";
}

TEST(LodChain, ScreenHeightFallsWithDistance) {
    const float t = std::tan(0.5f * 60.0f * 3.14159265f / 180.0f);
    const float near = kke::lodScreenHeight(1.0f, 5.0f, t), far = kke::lodScreenHeight(1.0f, 50.0f, t);
    EXPECT_NEAR(near / far, 10.0f, 1e-4f);
    EXPECT_NEAR(near, 1.0f / (5.0f * t), 1e-5f);
    EXPECT_GT(kke::lodScreenHeight(1.0f, 0.5f, t), 1.0f) << "inside it: bigger than the view";
}

TEST(LodChain, SelectionPicksTheLevelForTheSizeAndKeepsItNearTheLine) {
    const std::vector<float> sw = { 0.20f, 0.10f, 0.04f };
    EXPECT_EQ(kke::selectLod(sw, 0.5f, 0, 1.0f, 0.0f), 0);
    EXPECT_EQ(kke::selectLod(sw, 0.15f, 0, 1.0f, 0.0f), 1);
    EXPECT_EQ(kke::selectLod(sw, 0.07f, 0, 1.0f, 0.0f), 2);
    EXPECT_EQ(kke::selectLod(sw, 0.02f, 0, 1.0f, 0.0f), 3);
    EXPECT_EQ(kke::selectLod(sw, 0.02f, 3, 1.0f, 0.0f), 3);
    EXPECT_EQ(kke::selectLod(sw, 0.5f, 3, 1.0f, 0.0f), 0) << "and back up in one step";
    // Hysteresis 15%: on the 0.20 line, it stays where it is.
    EXPECT_EQ(kke::selectLod(sw, 0.19f, 0, 1.0f, 0.15f), 0) << "not far enough below the line to go coarser";
    EXPECT_EQ(kke::selectLod(sw, 0.16f, 0, 1.0f, 0.15f), 1);
    EXPECT_EQ(kke::selectLod(sw, 0.21f, 1, 1.0f, 0.15f), 1) << "not far enough above it to go finer";
    EXPECT_EQ(kke::selectLod(sw, 0.24f, 1, 1.0f, 0.15f), 0);
    // A bias of 2 sees every object twice as big: detail stays longer.
    EXPECT_EQ(kke::selectLod(sw, 0.15f, 0, 2.0f, 0.0f), 0);
    EXPECT_EQ(kke::selectLod(sw, 0.15f, 0, 0.5f, 0.0f), 2);
    EXPECT_EQ(kke::selectLod({}, 0.001f, 0), 0) << "no levels, no change";
    EXPECT_EQ(kke::selectLod(sw, 0.001f, 99), 3) << "a stale level is clamped";
}

// The point of the error budget: a level's surface stays within its allowed deviation of the original. The grid is a
// height field, so the deviation is easy to measure: at every original vertex, the height of the level's triangle
// above it against the original height.
TEST(LodChain, EveryLevelStaysWithinItsErrorOfTheOriginalSurface) {
    const ModelMesh m = grid(40);
    LodSettings settings;
    const auto chain = kke::buildLodChain(m, settings);
    ASSERT_GE(chain.size(), 2u);
    glm::vec3 lo(1e9f), hi(-1e9f);
    for (const auto& v : m.vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    const float extent = glm::length(hi - lo);
    for (const auto& level : chain) {
        float worst = 0.0f;
        for (const auto& v : m.vertices) {
            const glm::vec2 p(v.position.x, v.position.z);
            float height = 1e9f;
            for (size_t t = 0; t + 2 < level.indices.size() && height > 1e8f; t += 3) {
                const glm::vec3 a = m.vertices[level.indices[t]].position, b = m.vertices[level.indices[t + 1]].position, c = m.vertices[level.indices[t + 2]].position;
                // barycentric in the xz plane
                const glm::vec2 v0(b.x - a.x, b.z - a.z), v1(c.x - a.x, c.z - a.z), v2(p.x - a.x, p.y - a.z);
                const float d = v0.x * v1.y - v1.x * v0.y;
                if (std::fabs(d) < 1e-12f) continue;
                const float u = (v2.x * v1.y - v1.x * v2.y) / d, w = (v0.x * v2.y - v2.x * v0.y) / d;
                if (u >= -1e-4f && w >= -1e-4f && u + w <= 1.0001f) height = a.y + u * (b.y - a.y) + w * (c.y - a.y);
            }
            if (height < 1e8f) worst = std::max(worst, std::fabs(height - v.position.y));
        }
        // meshoptimizer's error is relative to the mesh's extent; allow a little over for the sampling.
        EXPECT_LE(worst, level.error * extent * 1.5f) << "level at " << level.ratio * 100 << "% triangles";
    }
}
