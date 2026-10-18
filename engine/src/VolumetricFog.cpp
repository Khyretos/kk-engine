#include "kke/VolumetricFog.h"

#include <algorithm>
#include <cmath>

namespace kke {

float volumetricSliceDistance(const VolumetricFogSettings& s, float slice) {
    return s.nearDistance * std::pow(s.farDistance / s.nearDistance, slice / static_cast<float>(s.slices));
}

float volumetricSliceOf(const VolumetricFogSettings& s, float distance) {
    return static_cast<float>(s.slices) * std::log(std::max(distance, 1e-6f) / s.nearDistance) / std::log(s.farDistance / s.nearDistance);
}

std::vector<glm::vec4> integrateFogColumn(const VolumetricFogSettings& s, const std::vector<FogCell>& column) {
    std::vector<glm::vec4> out(column.size());
    glm::vec3 scattered(0.0f);
    float through = 1.0f;
    for (size_t i = 0; i < column.size(); ++i) {
        const float depth = volumetricSliceDistance(s, static_cast<float>(i + 1)) - volumetricSliceDistance(s, static_cast<float>(i));
        const float t = std::exp(-column[i].extinction * depth);
        // The light scattered in this slice, dimmed by the fog in front of it.
        scattered += through * column[i].radiance * (1.0f - t);
        through *= t;
        out[i] = glm::vec4(scattered, through);
    }
    return out;
}

} // namespace kke
