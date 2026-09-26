#include "kke/Buoyancy.h"

#include <algorithm>

namespace kke {

float boxBuoyancy(const BuoyancyBox& box, const BuoyancySettings& s, const WaterSurface& water, std::vector<BuoyancyPoint>& out) {
    out.clear();
    const int n = std::max(1, s.samples);
    const glm::vec3 size = box.halfExtents * 2.0f;
    const float pointVolume = size.x * size.y * size.z / static_cast<float>(n * n * n);
    // How tall a point's share is, upright: it is partly under water
    // while the surface crosses that slab (smooth, no popping).
    const float slab = size.y / static_cast<float>(n);
    float under = 0.0f;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            for (int k = 0; k < n; ++k) {
                const glm::vec3 local = (glm::vec3(i, j, k) + 0.5f) / static_cast<float>(n) * size - box.halfExtents;
                const glm::vec3 r = box.rotation * local;
                const glm::vec3 p = box.center + r;
                glm::vec3 flow(0.0f);
                const float surface = water(p.x, p.z, flow);
                const float f = std::clamp((surface - p.y) / slab + 0.5f, 0.0f, 1.0f);
                if (f <= 0.0f) continue;
                under += f;
                const float displaced = s.waterDensity * pointVolume * f; // kg of water
                const glm::vec3 pointVelocity = box.velocity + glm::cross(box.angularVelocity, r);
                BuoyancyPoint bp;
                bp.point = p;
                bp.force = -s.gravity * displaced - (pointVelocity - flow) * (displaced * s.drag);
                out.push_back(bp);
            }
    return under / static_cast<float>(n * n * n);
}

} // namespace kke
