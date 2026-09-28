#version 450
#extension GL_GOOGLE_include_directive : require
// Sparks (kke::ParticleEffects): hot streaks added onto the image,
// brightest along their middle and at the head.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec4 fragParams;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorld;
layout(location = 0) out vec4 outColor;

#include "lighting_ubo.glsl"

void main() {
    float across = 1.0 - smoothstep(0.0, 1.0, abs(fragUV.x));
    float along = 0.35 + 0.65 * (fragUV.y * 0.5 + 0.5);
    float k = across * across * along * clamp(fragParams.y, 0.0, 1.0);
    outColor = vec4(displayColor(fragColor * k), 1.0);
}
