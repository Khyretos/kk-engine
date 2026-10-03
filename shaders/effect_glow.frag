#version 450
#extension GL_GOOGLE_include_directive : require
// Glows (kke::ParticleEffects): soft round light added onto the image, a
// hot core fading smoothly to nothing at the rim. Flames, embers, magic,
// a muzzle flash; overlapping glows add up and bloom.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec4 fragParams;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorld;
layout(location = 0) out vec4 outColor;

#include "lighting_ubo.glsl"

void main() {
    float r = length(fragUV);
    if (r >= 1.0) discard;
    float soft = 1.0 - r;
    float k = soft * soft * (0.6 + 0.4 * soft) * clamp(fragParams.y, 0.0, 1.0);
    outColor = vec4(displayColor(fragColor * k), 1.0);
}
