#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Volumetric fog (ENG-F03): the height fog (kke::Fog) lit for real, with
// the sun's shadow (light shafts) and the point lights (halos), in a grid
// of cells over the view frustum (kke::VolumetricFogPass), integrated from
// the camera outwards once a frame. No jitter and nothing carried between
// frames: it never needs a temporal filter to look clean
// (docs/RENDERING_PRINCIPLES.md). Beyond the grid's far distance the
// analytic fog takes over, so far hills are fogged exactly as before.
struct VolumetricFogSettings {
    uint32_t width = 128, height = 72, slices = 48; // cells across, down and in depth
    float nearDistance = 0.5f;                      // metres: the first slice starts here
    float farDistance = 96.0f;                      // metres: the last slice ends here
};

// Slices are spaced exponentially (thin near the camera, thick far away):
// the distance from the camera where continuous slice coordinate `s`
// (0 .. slices) lies, and the inverse.
float volumetricSliceDistance(const VolumetricFogSettings& s, float slice);
float volumetricSliceOf(const VolumetricFogSettings& s, float distance);

// One cell's light: what it sends towards the camera per unit of fog
// (rgb, the fog's colour as lit there) and how thick it is (extinction per metre).
struct FogCell {
    glm::vec3 radiance{0.0f};
    float extinction = 0.0f;
};

// Integrates a column of cells from the camera outwards (Hillaire 2015's
// energy-conserving step): for each slice, the light scattered towards the
// camera up to its far edge (rgb) and how much of what lies behind gets
// through (a, transmittance). The CPU copy of shaders/volumetric_integrate.comp.
std::vector<glm::vec4> integrateFogColumn(const VolumetricFogSettings& s, const std::vector<FogCell>& column);

} // namespace kke
