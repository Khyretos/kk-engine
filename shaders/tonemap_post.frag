#version 450
#extension GL_GOOGLE_include_directive : require

// ENG-G17: the scene's one tone-map pass. Drawn by kke::Renderer as the
// first thing in the UI pass, over the whole swapchain image: it reads the
// HDR scene image (linear scene light, any render scale: the sampler
// filters it up), applies exposure, the curve and the look once per pixel,
// and writes display-linear colour that the sRGB swapchain encodes.
// Per pixel, spatial only: no dithering, nothing temporal
// (docs/RENDERING_PRINCIPLES.md §7).

#include "tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D uScene;

layout(push_constant) uniform Post {
    vec4 tone;       // x = kke::ToneMapper, y = exposure
    vec4 lookSlope;  // rgb = slope, a = saturation
    vec4 lookOffset; // rgb = offset, a = 1 when a look is set
    vec4 lookPower;
} post;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 c = texture(uScene, inUv).rgb;
    outColor = vec4(toneMapLook(c, post.tone, post.lookSlope, post.lookOffset, post.lookPower), 1.0);
}
