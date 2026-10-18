#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Many lights without a deferred path (ENG-F08, kke::ClusterLightPass).
// A deferred renderer pays for many lights with a G-buffer: no MSAA, no
// glass, a fat target read back per light. KKE stays forward and clusters
// instead (Olsson et al. 2012): the view is cut into a grid of cells, 16
// across, 9 down and 24 deep (thin near the camera, deep far away); once a
// frame a compute pass lists, per cell, the lights whose reach touches it,
// and each lit pixel walks only its cell's list. The 16 key lights
// (Lighting::lights, with shadows) are untouched; these are the many small
// ones beyond them (Lighting::manyLights): lamps, torches, sparks.
struct ClusterGrid {
    uint32_t x = 16, y = 9, z = 24;
    float nearDistance = 0.1f;   // view depth where the first slice starts
    float farDistance = 300.0f;  // and where the last ends; lights past it are not drawn
    uint32_t maxPerCluster = 128; // lights a cell lists at most
};

struct ClusterLight {
    glm::vec3 position{0.0f}; // world space
    float range = 10.0f;      // metres; it reaches nothing past this
};

// The slice (0 .. z-1) a view depth falls in; -1 outside near..far.
int clusterSlice(const ClusterGrid& grid, float viewDepth);
// The view depth where slice `s` starts (s = z: the far distance).
float clusterSliceDepth(const ClusterGrid& grid, float s);
// The cell index of pixel `pixel` (in a `screen`-pixel image) at view depth `viewDepth`; -1 outside.
int clusterIndex(const ClusterGrid& grid, const glm::vec2& pixel, const glm::vec2& screen, float viewDepth);
// For each cell, the lights (indices into `lights`) whose sphere touches it,
// with a camera `view` and perspective `proj` (Vulkan's flipped Y). The CPU
// copy of shaders/cluster_lights.comp.
std::vector<std::vector<uint32_t>> clusterLights(const ClusterGrid& grid, const std::vector<ClusterLight>& lights, const glm::mat4& view,
                                                 const glm::mat4& proj);

} // namespace kke
