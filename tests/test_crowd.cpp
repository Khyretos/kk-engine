// Crowds (ENG-F73, kke/ai/Crowd.h): DetourCrowd agents on a NavMesh.

#include "kke/ai/Crowd.h"
#include "kke/ai/NavMesh.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace kke::ai;

namespace {

// A flat 60 x 60 m square, with a wall across the middle that has a 6 m gap.
NavMesh buildLevel() {
    std::vector<glm::vec3> v;
    std::vector<uint32_t> idx;
    auto quad = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
        const uint32_t base = static_cast<uint32_t>(v.size());
        v.insert(v.end(), { a, b, c, d });
        idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    };
    quad({ -30, 0, -30 }, { 30, 0, -30 }, { 30, 0, 30 }, { -30, 0, 30 });
    // The wall: a box 1 m thick and 3 m high along x = 0, except for z in [-3, 3].
    auto box = [&](glm::vec3 lo, glm::vec3 hi) {
        quad({ lo.x, hi.y, lo.z }, { hi.x, hi.y, lo.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z }); // top
        quad({ lo.x, lo.y, lo.z }, { lo.x, hi.y, lo.z }, { lo.x, hi.y, hi.z }, { lo.x, lo.y, hi.z });
        quad({ hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { hi.x, hi.y, lo.z }, { hi.x, lo.y, lo.z });
    };
    box({ -0.5f, 0.0f, -30.0f }, { 0.5f, 3.0f, -3.0f });
    box({ -0.5f, 0.0f, 3.0f }, { 0.5f, 3.0f, 30.0f });
    NavMesh mesh;
    NavMeshSettings s;
    s.cellSize = 0.3f;
    std::string error;
    EXPECT_TRUE(mesh.build(v, idx, s, &error)) << error;
    return mesh;
}

} // namespace

TEST(Crowd, AgentsCrossThroughTheGapWithoutPilingUp) {
    NavMesh mesh = buildLevel();
    ASSERT_TRUE(mesh.valid());
    Crowd crowd(mesh, 300, 0.5f);
    ASSERT_TRUE(crowd.valid());
    std::vector<Crowd::AgentId> ids;
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 at(-25.0f + static_cast<float>(i % 10) * 1.5f, 0.0f, -14.0f + static_cast<float>(i / 10) * 1.5f);
        const Crowd::AgentId id = crowd.add(at);
        ASSERT_NE(id, 0u) << i;
        ids.push_back(id);
    }
    EXPECT_EQ(crowd.count(), 200u);
    for (size_t i = 0; i < ids.size(); ++i)
        EXPECT_TRUE(crowd.moveTo(ids[i], glm::vec3(14.0f + static_cast<float>(i % 10) * 1.2f, 0.0f, -12.0f + static_cast<float>(i / 10) * 1.2f)));

    double worstMs = 0.0, totalMs = 0.0;
    const int steps = 90 * 60; // a minute and a half at 60 Hz
    for (int s = 0; s < steps; ++s) {
        const auto t0 = std::chrono::steady_clock::now();
        crowd.update(1.0f / 60.0f);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        worstMs = std::max(worstMs, ms);
        totalMs += ms;
    }
    int across = 0;
    for (Crowd::AgentId id : ids)
        if (crowd.position(id).x > 1.0f) ++across;
    EXPECT_GE(across, 190) << "nearly everyone got through the 6 m gap in 90 s";
    // Nobody stands inside anybody else.
    float closest = 1e9f;
    for (size_t a = 0; a < ids.size(); ++a)
        for (size_t b = a + 1; b < ids.size(); ++b) {
            const glm::vec3 d = crowd.position(ids[a]) - crowd.position(ids[b]);
            const float dist = std::sqrt(d.x * d.x + d.z * d.z);
            if (dist < 0.25f)
                std::printf("close: %zu (%.2f %.2f, arrived %d) %zu (%.2f %.2f, arrived %d)\n", a, crowd.position(ids[a]).x, crowd.position(ids[a]).z,
                            crowd.arrived(ids[a]) ? 1 : 0, b, crowd.position(ids[b]).x, crowd.position(ids[b]).z, crowd.arrived(ids[b]) ? 1 : 0);
            closest = std::min(closest, dist);
        }
    EXPECT_GT(closest, 0.25f) << "agents of radius 0.35 squeeze a little in a jam, never into each other";
    std::printf("[ crowd    ] 200 agents: %.3f ms per update on average, worst %.3f ms; closest pair %.2f m\n", totalMs / steps, worstMs, closest);
    EXPECT_LT(totalMs / steps, 4.0) << "a debug build on a slow CI box still has room";
}

TEST(Crowd, IdsVelocityAndRemoval) {
    NavMesh mesh = buildLevel();
    Crowd crowd(mesh, 4, 0.5f);
    EXPECT_EQ(crowd.add(glm::vec3(0.0f, 0.0f, 100.0f)), 0u) << "off the mesh";
    const Crowd::AgentId a = crowd.add(glm::vec3(-10.0f, 0.0f, 0.0f));
    ASSERT_NE(a, 0u);
    EXPECT_TRUE(crowd.setVelocity(a, glm::vec3(1.0f, 0.0f, 0.0f)));
    for (int i = 0; i < 60; ++i) crowd.update(1.0f / 60.0f);
    EXPECT_GT(crowd.position(a).x, -9.5f) << "walked along +x";
    EXPECT_GT(crowd.velocity(a).x, 0.5f);
    EXPECT_FALSE(crowd.moveTo(a, glm::vec3(0.0f, 0.0f, 100.0f))) << "a target off the mesh";
    crowd.remove(a);
    EXPECT_FALSE(crowd.alive(a));
    EXPECT_EQ(crowd.count(), 0u);
    for (int i = 0; i < 4; ++i) EXPECT_NE(crowd.add(glm::vec3(-10.0f + i, 0.0f, 10.0f)), 0u);
    EXPECT_EQ(crowd.add(glm::vec3(-5.0f, 0.0f, 10.0f)), 0u) << "full";
}
