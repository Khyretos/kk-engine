// Occlusion culling (ENG-F13): the depth map shaders/depth_map.glsl makes,
// and the CPU's test of a model's box against it.

#include "kke/Occlusion.h"

#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

#include <vector>

using namespace kke;

namespace {

constexpr uint32_t kSize = 64;

glm::mat4 camera() {
    // Looking down -z from the origin, as the engine's cameras do.
    return glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 200.0f) * glm::lookAt(glm::vec3(0.0f), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
}

// A wall 5 m ahead covering the middle half of the screen, nothing (far) round it.
DepthMap wallMap() {
    const glm::mat4 vp = camera();
    const glm::vec4 c = vp * glm::vec4(0.0f, 0.0f, -5.0f, 1.0f);
    const float wall = c.z / c.w;
    std::vector<float> depth(kSize * kSize, 1.0f);
    for (uint32_t y = 16; y < 48; ++y)
        for (uint32_t x = 16; x < 48; ++x) depth[y * kSize + x] = wall;
    return buildDepthMap(depth.data(), kSize, kSize, vp);
}

} // namespace

TEST(Occlusion, MapSizeRoundsUp) {
    EXPECT_EQ(depthMapSize(64, 64), glm::uvec2(8, 8));
    EXPECT_EQ(depthMapSize(1920, 1080), glm::uvec2(240, 135));
    EXPECT_EQ(depthMapSize(1921, 1081), glm::uvec2(241, 136));
}

TEST(Occlusion, EachCellKeepsItsFarthestDepth) {
    std::vector<float> depth(16 * 8, 0.2f);
    depth[3 * 16 + 5] = 0.9f; // one far texel in the first cell
    const DepthMap map = buildDepthMap(depth.data(), 16, 8, glm::mat4(1.0f));
    ASSERT_EQ(map.width, 2u);
    ASSERT_EQ(map.height, 1u);
    EXPECT_FLOAT_EQ(map.farthest[0], 0.9f);
    EXPECT_FLOAT_EQ(map.farthest[1], 0.2f);
}

TEST(Occlusion, ABoxBehindTheWallIsHidden) {
    const DepthMap map = wallMap();
    EXPECT_TRUE(occludedBy(map, glm::vec3(-0.5f, -0.5f, -11.0f), glm::vec3(0.5f, 0.5f, -10.0f)));
}

TEST(Occlusion, ABoxInFrontOfTheWallShows) {
    const DepthMap map = wallMap();
    EXPECT_FALSE(occludedBy(map, glm::vec3(-0.5f, -0.5f, -3.5f), glm::vec3(0.5f, 0.5f, -3.0f)));
}

TEST(Occlusion, ABoxPeekingPastTheWallShows) {
    const DepthMap map = wallMap();
    // Behind it, but wide enough to reach past its edge, where the map saw nothing (far).
    EXPECT_FALSE(occludedBy(map, glm::vec3(-8.0f, -0.5f, -11.0f), glm::vec3(0.5f, 0.5f, -10.0f)));
}

TEST(Occlusion, BoxesTheMapCantJudgeShow) {
    const DepthMap map = wallMap();
    EXPECT_FALSE(occludedBy(map, glm::vec3(-1.0f, -1.0f, -1.0f), glm::vec3(1.0f, 1.0f, 1.0f)));       // round the camera
    EXPECT_FALSE(occludedBy(map, glm::vec3(-0.5f, -0.5f, 9.0f), glm::vec3(0.5f, 0.5f, 10.0f)));       // behind it
    EXPECT_FALSE(occludedBy(map, glm::vec3(100.0f, -0.5f, -11.0f), glm::vec3(101.0f, 0.5f, -10.0f))); // off its screen
    EXPECT_FALSE(occludedBy(DepthMap{}, glm::vec3(-0.5f, -0.5f, -11.0f), glm::vec3(0.5f, 0.5f, -10.0f))); // no map
}
