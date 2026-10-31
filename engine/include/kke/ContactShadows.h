#pragma once

#include <functional>

namespace kke {

// Contact shadows (ENG-F16): a short march towards the sun through the
// frame's depth, for the small shadows the shadow map is too coarse to
// draw: where a foot meets the floor, a cup stands on a table, a crate
// leans on a wall. After the frame a compute pass (shaders/
// contact_shadows.glsl, ContactShadowPass) writes how lit each pixel is;
// the next frame's lit shaders multiply the sun's shadow with it,
// reprojected (as the shadow rays are). Any GPU: no ray tracing needed.
// The graphics setting "contactShadows" or KKE_CONTACT_SHADOWS=0/1.
struct ContactShadowSettings {
    float length = 0.35f;       // metres marched towards the sun
    int steps = 16;             // samples along it
    float thickness = 0.12f;    // metres: a surface in front by more than this is a separate object (no shadow)
    float maxDistance = 60.0f;  // metres from the camera; farther pixels aren't marched
    float fadeFrom = 45.0f;     // they fade out between here and maxDistance
    float reprojectSlack = 0.03f;
};

// The march the shader does, on the CPU (tests): from view-space point
// `pos` (metres, camera looking down -z) towards `toSun` (view space,
// unit), sampling `sceneDepthAt(viewPoint)`, the view depth (positive
// metres) the frame drew at that point's pixel, 0 for sky. Returns how lit
// the point is: 1 = nothing in the way, 0 = fully shadowed. `jitter` 0..1
// offsets the first step (the shader uses a per-pixel pattern).
float contactShadowMarch(const float pos[3], const float toSun[3], const std::function<float(const float viewPoint[3])>& sceneDepthAt,
                         const ContactShadowSettings& settings, float jitter = 0.5f);

// How much of the contact shadow is kept `distance` metres from the camera (1 near, 0 past maxDistance).
float contactShadowFade(float distance, const ContactShadowSettings& settings);

} // namespace kke
