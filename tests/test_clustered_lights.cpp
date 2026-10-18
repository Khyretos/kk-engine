#include "kke/ClusteredLights.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <utility>
#include <vector>

// ENG-F08: many lights, forward, each pixel walking only its cell's lights.

namespace {

struct Cam {
    glm::mat4 view = glm::lookAt(glm::vec3(0, 2, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    glm::mat4 proj;
    glm::vec2 screen{1600.0f, 900.0f};
    Cam() {
        proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 300.0f);
        proj[1][1] *= -1.0f; // Vulkan's flipped Y
    }
    // The pixel a world point lands on and its view depth.
    std::pair<glm::vec2, float> pixelOf(const glm::vec3& p) const {
        const glm::vec4 c = proj * view * glm::vec4(p, 1.0f);
        return { (glm::vec2(c) / c.w * 0.5f + 0.5f) * screen, c.w };
    }
};

} // namespace

TEST(ClusteredLights, SlicesAreThinNearAndDeepFar) {
    const kke::ClusterGrid g;
    EXPECT_EQ(kke::clusterSlice(g, 0.05f), -1);
    EXPECT_EQ(kke::clusterSlice(g, 0.1f), 0);
    EXPECT_EQ(kke::clusterSlice(g, 299.0f), static_cast<int>(g.z) - 1);
    EXPECT_LT(kke::clusterSliceDepth(g, 1.0f) - kke::clusterSliceDepth(g, 0.0f),
              kke::clusterSliceDepth(g, static_cast<float>(g.z)) - kke::clusterSliceDepth(g, static_cast<float>(g.z) - 1.0f));
}

TEST(ClusteredLights, ALightIsListedWhereItShines) {
    // A lamp with a 2 m reach on the ground: the pixel under it lists it; a point 10 m away does not.
    const Cam cam;
    const kke::ClusterGrid g;
    const std::vector<kke::ClusterLight> lights{ { glm::vec3(1.0f, 0.5f, 0.0f), 2.0f } };
    const auto cells = kke::clusterLights(g, lights, cam.view, cam.proj);
    const auto [px, depth] = cam.pixelOf(glm::vec3(1.2f, 0.0f, 0.3f));
    const int c = kke::clusterIndex(g, px, cam.screen, depth);
    ASSERT_GE(c, 0);
    EXPECT_EQ(cells[static_cast<size_t>(c)], std::vector<uint32_t>{ 0 });
    const auto [px2, depth2] = cam.pixelOf(glm::vec3(-9.0f, 0.0f, -6.0f));
    const int c2 = kke::clusterIndex(g, px2, cam.screen, depth2);
    ASSERT_GE(c2, 0);
    EXPECT_TRUE(cells[static_cast<size_t>(c2)].empty());
}

TEST(ClusteredLights, EveryLitPointFindsItsLights) {
    // Many lamps in a grid: every ground point inside a lamp's reach must find that lamp in its cell
    // (a missing light would be a hole in the lighting; extra ones only cost time).
    const Cam cam;
    const kke::ClusterGrid g;
    std::vector<kke::ClusterLight> lights;
    for (int i = -5; i <= 5; ++i)
        for (int j = -5; j <= 5; ++j) lights.push_back({ glm::vec3(static_cast<float>(i) * 2.0f, 0.3f, static_cast<float>(j) * 2.0f), 1.5f });
    const auto cells = kke::clusterLights(g, lights, cam.view, cam.proj);
    int checked = 0;
    for (float x = -8.0f; x <= 8.0f; x += 0.37f)
        for (float z = -8.0f; z <= 8.0f; z += 0.41f) {
            const glm::vec3 p(x, 0.0f, z);
            const auto [px, depth] = cam.pixelOf(p);
            if (px.x < 0 || px.y < 0 || px.x >= cam.screen.x || px.y >= cam.screen.y) continue;
            const int c = kke::clusterIndex(g, px, cam.screen, depth);
            if (c < 0) continue;
            const auto& list = cells[static_cast<size_t>(c)];
            for (size_t i = 0; i < lights.size(); ++i)
                if (glm::length(p - lights[i].position) < lights[i].range) {
                    EXPECT_NE(std::find(list.begin(), list.end(), static_cast<uint32_t>(i)), list.end()) << x << " " << z;
                    ++checked;
                }
        }
    EXPECT_GT(checked, 100);
}

TEST(ClusteredLights, ACellListsFarFewerThanAllLights) {
    const Cam cam;
    const kke::ClusterGrid g;
    std::vector<kke::ClusterLight> lights;
    for (int i = 0; i < 400; ++i) lights.push_back({ glm::vec3(static_cast<float>(i % 20) * 2.0f - 20.0f, 0.3f, static_cast<float>(i / 20) * -2.0f + 5.0f), 1.5f });
    const auto cells = kke::clusterLights(g, lights, cam.view, cam.proj);
    size_t most = 0;
    for (const auto& c : cells) most = std::max(most, c.size());
    EXPECT_GT(most, 0u);
    EXPECT_LT(most, 60u);
}
