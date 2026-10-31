#include "kke/LodChain.h"

#include "kke/MeshLod.h"

#include <algorithm>
#include <cmath>

namespace kke {

std::vector<LodLevel> buildLodChain(const ModelMesh& mesh, const LodSettings& settings) {
    std::vector<LodLevel> chain;
    const size_t full = mesh.indices.size() / 3;
    if (full < settings.minTriangles) return chain;
    size_t before = full;
    float lastSwitch = 1.0f;
    const size_t levels = std::min(settings.ratios.size(), settings.errors.size());
    for (size_t i = 0; i < levels; ++i) {
        std::vector<uint32_t> indices = simplifyIndices(mesh, settings.ratios[i], settings.errors[i]);
        const size_t triangles = indices.size() / 3;
        // Worth having only if it is clearly cheaper than the level before it.
        if (triangles == 0 || static_cast<float>(triangles) > static_cast<float>(before) * (1.0f - settings.minReduction)) continue;
        LodLevel level;
        level.ratio = static_cast<float>(triangles) / static_cast<float>(full);
        level.error = settings.errors[i];
        // error * height(px) <= tolerance  ->  height <= tolerance / (error * reference): the size it takes over at.
        level.switchBelow = std::min(lastSwitch * 0.9f, settings.pixelTolerance / (std::max(level.error, 1e-4f) * settings.referenceHeight));
        level.indices = std::move(indices);
        lastSwitch = level.switchBelow;
        before = triangles;
        chain.push_back(std::move(level));
    }
    return chain;
}

float lodScreenHeight(float radius, float distance, float tanHalfFov) {
    if (tanHalfFov <= 0.0f) return 1.0f;
    if (distance <= radius) return 10.0f; // the camera is inside it
    return radius / (distance * tanHalfFov); // 2r over the view's height 2 * d * tan(fov/2)
}

int selectLod(const std::vector<float>& switchBelow, float screenHeight, int current, float bias, float hysteresis) {
    const int levels = static_cast<int>(switchBelow.size());
    const float size = screenHeight * std::max(bias, 1e-3f);
    int lod = std::clamp(current, 0, levels);
    while (lod < levels && size < switchBelow[static_cast<size_t>(lod)] * (1.0f - hysteresis)) ++lod; // coarser
    while (lod > 0 && size > switchBelow[static_cast<size_t>(lod) - 1] * (1.0f + hysteresis)) --lod;  // finer
    return lod;
}

} // namespace kke
