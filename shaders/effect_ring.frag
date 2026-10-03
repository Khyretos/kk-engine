#version 450
#extension GL_GOOGLE_include_directive : require
// Rings (kke::ParticleEffects): an expanding band of light, brightest in
// the middle of the band, smooth on both edges. A shockwave on the ground,
// a portal, a pickup's pulse.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec4 fragParams;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorld;
layout(location = 4) in vec2 fragExtra; // (-1 flat / -2 facing, band width as a fraction of the radius)
layout(location = 0) out vec4 outColor;

#include "lighting_ubo.glsl"

void main() {
    float r = length(fragUV);
    float half_ = max(fragExtra.y, 0.01) * 0.5;
    float d = abs(r - (1.0 - half_)) / half_; // 0 in the middle of the band, 1 at its edges
    if (d >= 1.0) discard;
    float k = (1.0 - d * d) * clamp(fragParams.y, 0.0, 1.0);
    outColor = vec4(displayColor(fragColor * k * k), 1.0);
}
