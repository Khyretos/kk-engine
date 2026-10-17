#version 450
#extension GL_GOOGLE_include_directive : require
// Particle effects the CPU moves (kke::ParticleEffects): one quad each, six
// vertices of one instance (the corner comes from gl_VertexIndex), facing
// the camera; the shapes are in effect_quad.glsl.
layout(location = 0) in vec3 inCenter;
layout(location = 1) in vec3 inColor;   // smoke: sRGB albedo; spark: linear light (HDR)
layout(location = 2) in vec4 inParams;  // x = radius (m), y = opacity 0..1, z = spin (rad), w = seed
layout(location = 3) in vec3 inVelocity; // sparks: streak = velocity x stretch seconds (m)
layout(location = 5) in vec2 inExtra;   // flake: (squash 0..1, shape); ring: (-1 flat / -2 facing, band width)

#include "lighting_ubo.glsl"
#include "effect_quad.glsl"

void main() {
    effectQuad(inCenter, inColor, inParams, inVelocity, inExtra, kCorners[gl_VertexIndex]);
}
