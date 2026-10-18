#pragma once

#include <cstdint>
#include <string>

namespace kke {

// Ray-traced sun shadows (ENG-F07, Application::setShadowRays). Shadow maps
// stay the main tool (docs/RENDERING_PRINCIPLES.md §5): sharp up close,
// soft-edged, cheap. They end at the shadow distance (80 m), so past it
// nothing casts a shadow. `Kke` traces a ray towards the sun only there,
// through the simplified stand-ins (kke::RayScene), so far hills, towers
// and trees shade the land under them again. `Classic` traces every pixel
// through the full geometry and uses the ray instead of the map: hard
// shadows everywhere, the usual ray-traced look, for comparison.
// The rays are cast after the frame from its depth and read by the next
// frame's lit shaders (as GI and reflections are), reprojected; where the
// reprojection fails the shadow map answers.
enum class ShadowRays { Off, Kke, Classic };
// "off", "kke", "classic"; anything else gives `fallback`.
ShadowRays shadowRaysFromName(const std::string& name, ShadowRays fallback);

struct ShadowRaySettings {
    float kkeFrom = 75.0f;       // metres from the camera where KKE starts tracing (just inside the shadow distance)
    float maxDistance = 600.0f;  // metres: farther pixels are not traced
    float reprojectSlack = 0.03f; // how far last frame's depth may differ (share of the distance) and still be used
};

// Whether a pixel `distance` metres away is traced.
bool shadowRayTraced(ShadowRays mode, float distance, const ShadowRaySettings& settings);
// The traced result for a surface `expectedDepth` metres away (along the
// view axis) from last frame's pixel (`lit` 0..1, its `tracedDepth`, 0 =
// not traced): the ray's answer, or -1 when the pixel showed something else
// (then the shadow map answers).
float shadowRayLookup(float lit, float tracedDepth, float expectedDepth, const ShadowRaySettings& settings);

} // namespace kke
