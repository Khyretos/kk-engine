#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Occlusion culling (ENG-F13): models hidden behind others are neither
// skinned, uploaded nor drawn. The usual way (a hierarchical depth buffer
// and GPU-driven indirect draws) needs every renderer rebuilt around
// indirect buffers. Ours keeps the CPU's batching: after each frame one
// compute dispatch shrinks its depth to a small map, each cell the
// farthest depth of an 8x8 block, straight into memory the CPU reads two
// frames later (no copy, no stall). A model's box is hidden when its
// nearest corner is farther than everything the map saw over the box's
// rectangle on screen, tested with the camera the map was made with. The
// two frames of delay show when the camera moves fast: something coming out
// from behind a wall can appear a frame or two late. Off by default
// (graphics setting "occlusionCulling"): it keeps the depth (memory
// bandwidth every frame) and pays off where many models hide behind others.
struct DepthMap {
    static constexpr uint32_t kCell = 8; // screen pixels per cell, each way
    uint32_t width = 0, height = 0;
    std::vector<float> farthest; // width x height, row by row: the farthest depth in each cell
    glm::mat4 viewProj{1.0f};    // the camera the depth was drawn with
    bool valid() const { return width > 0 && height > 0 && farthest.size() == static_cast<size_t>(width) * height; }
};

// The map's size for a `w` x `h` depth image.
glm::uvec2 depthMapSize(uint32_t w, uint32_t h);
// The map of a `w` x `h` depth image (NDC depth, larger farther), on the CPU: the tests' reference
// for shaders/depth_map.comp.
DepthMap buildDepthMap(const float* depth, uint32_t w, uint32_t h, const glm::mat4& viewProj);
// True when the world box `mn`..`mx` is certainly hidden behind what the map saw. Conservative:
// a box crossing the camera's plane, off the map's screen or over more than kMaxCells cells counts
// as visible.
bool occludedBy(const DepthMap& map, const glm::vec3& mn, const glm::vec3& mx);
constexpr uint32_t kMaxOcclusionCells = 1024;

} // namespace kke
