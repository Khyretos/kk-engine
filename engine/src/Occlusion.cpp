#include "kke/Occlusion.h"

#include <algorithm>
#include <cmath>

namespace kke {

glm::uvec2 depthMapSize(uint32_t w, uint32_t h) {
    return glm::uvec2((w + DepthMap::kCell - 1) / DepthMap::kCell, (h + DepthMap::kCell - 1) / DepthMap::kCell);
}

DepthMap buildDepthMap(const float* depth, uint32_t w, uint32_t h, const glm::mat4& viewProj) {
    DepthMap map;
    const glm::uvec2 size = depthMapSize(w, h);
    map.width = size.x;
    map.height = size.y;
    map.viewProj = viewProj;
    map.farthest.assign(static_cast<size_t>(size.x) * size.y, 0.0f);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            float& cell = map.farthest[static_cast<size_t>(y / DepthMap::kCell) * size.x + x / DepthMap::kCell];
            cell = std::max(cell, depth[static_cast<size_t>(y) * w + x]);
        }
    return map;
}

bool occludedBy(const DepthMap& map, const glm::vec3& mn, const glm::vec3& mx) {
    if (!map.valid()) return false;
    float nearest = 1e30f;
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (int k = 0; k < 8; ++k) {
        const glm::vec3 corner((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z);
        const glm::vec4 clip = map.viewProj * glm::vec4(corner, 1.0f);
        if (clip.w <= 1e-4f) return false; // crosses the camera's plane
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        nearest = std::min(nearest, ndc.z);
        lo = glm::min(lo, glm::vec2(ndc));
        hi = glm::max(hi, glm::vec2(ndc));
    }
    if (hi.x < -1.0f || hi.y < -1.0f || lo.x > 1.0f || lo.y > 1.0f) return false; // off the map's screen: not its call
    // The cells the box's rectangle covers (clamped to the screen).
    const glm::vec2 cells(static_cast<float>(map.width), static_cast<float>(map.height));
    const glm::ivec2 a = glm::clamp(glm::ivec2(glm::floor((glm::max(lo, glm::vec2(-1.0f)) * 0.5f + 0.5f) * cells)), glm::ivec2(0),
                                    glm::ivec2(map.width - 1, map.height - 1));
    const glm::ivec2 b = glm::clamp(glm::ivec2(glm::floor((glm::min(hi, glm::vec2(1.0f)) * 0.5f + 0.5f) * cells)), glm::ivec2(0),
                                    glm::ivec2(map.width - 1, map.height - 1));
    if (static_cast<uint32_t>((b.x - a.x + 1) * (b.y - a.y + 1)) > kMaxOcclusionCells) return false;
    for (int y = a.y; y <= b.y; ++y)
        for (int x = a.x; x <= b.x; ++x)
            if (map.farthest[static_cast<size_t>(y) * map.width + static_cast<size_t>(x)] >= nearest) return false; // seen past it here
    return true;
}

} // namespace kke
