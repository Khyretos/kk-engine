#include "kke/ClusteredLights.h"

#include <algorithm>
#include <cmath>

namespace kke {

int clusterSlice(const ClusterGrid& grid, float viewDepth) {
    if (viewDepth < grid.nearDistance || viewDepth >= grid.farDistance) return -1;
    const float s = static_cast<float>(grid.z) * std::log(viewDepth / grid.nearDistance) / std::log(grid.farDistance / grid.nearDistance);
    return std::min(static_cast<int>(s), static_cast<int>(grid.z) - 1);
}

float clusterSliceDepth(const ClusterGrid& grid, float s) {
    return grid.nearDistance * std::pow(grid.farDistance / grid.nearDistance, s / static_cast<float>(grid.z));
}

int clusterIndex(const ClusterGrid& grid, const glm::vec2& pixel, const glm::vec2& screen, float viewDepth) {
    const int z = clusterSlice(grid, viewDepth);
    if (z < 0) return -1;
    const int x = std::clamp(static_cast<int>(pixel.x / screen.x * static_cast<float>(grid.x)), 0, static_cast<int>(grid.x) - 1);
    const int y = std::clamp(static_cast<int>(pixel.y / screen.y * static_cast<float>(grid.y)), 0, static_cast<int>(grid.y) - 1);
    return (z * static_cast<int>(grid.y) + y) * static_cast<int>(grid.x) + x;
}

std::vector<std::vector<uint32_t>> clusterLights(const ClusterGrid& grid, const std::vector<ClusterLight>& lights, const glm::mat4& view,
                                                 const glm::mat4& proj) {
    std::vector<std::vector<uint32_t>> cells(static_cast<size_t>(grid.x * grid.y * grid.z));
    std::vector<glm::vec3> centres(lights.size());
    for (size_t i = 0; i < lights.size(); ++i) centres[i] = glm::vec3(view * glm::vec4(lights[i].position, 1.0f));
    for (uint32_t cz = 0; cz < grid.z; ++cz)
        for (uint32_t cy = 0; cy < grid.y; ++cy)
            for (uint32_t cx = 0; cx < grid.x; ++cx) {
                // The cell's corners in view space: its screen tile between its two depths.
                glm::vec3 lo(1e30f), hi(-1e30f);
                for (int k = 0; k < 8; ++k) {
                    const float nx = (static_cast<float>(cx + (k & 1)) / static_cast<float>(grid.x)) * 2.0f - 1.0f;
                    const float ny = (static_cast<float>(cy + ((k >> 1) & 1)) / static_cast<float>(grid.y)) * 2.0f - 1.0f;
                    const float d = clusterSliceDepth(grid, static_cast<float>(cz + ((k >> 2) & 1)));
                    const glm::vec3 p(nx * d / proj[0][0], ny * d / proj[1][1], -d);
                    lo = glm::min(lo, p);
                    hi = glm::max(hi, p);
                }
                std::vector<uint32_t>& list = cells[static_cast<size_t>((cz * grid.y + cy) * grid.x + cx)];
                for (size_t i = 0; i < lights.size() && list.size() < grid.maxPerCluster; ++i) {
                    const glm::vec3 nearest = glm::clamp(centres[i], lo, hi);
                    const glm::vec3 d = centres[i] - nearest;
                    if (glm::dot(d, d) <= lights[i].range * lights[i].range) list.push_back(static_cast<uint32_t>(i));
                }
            }
    return cells;
}

} // namespace kke
