#version 450
#extension GL_GOOGLE_include_directive : require

// ENG-G17: the scene's one tone-map pass. Drawn by kke::Renderer as the
// first thing in the UI pass, over the whole swapchain image: it reads the
// HDR scene image (linear scene light, any render scale: the sampler
// filters it up), applies exposure, the curve and the look once per pixel,
// and writes display-linear colour that the sRGB swapchain encodes, or on
// a UNORM (10-bit) swapchain the sRGB-encoded colour itself (tone.z).
// Per pixel, spatial only: no dithering, nothing temporal
// (docs/RENDERING_PRINCIPLES.md §7).

#include "tonemap.glsl"

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform sampler2D uBloom; // ENG-F10, half resolution; the scene itself when there is none

layout(push_constant) uniform Post {
    vec4 tone;       // x = kke::ToneMapper, y = exposure, z = 1: encode sRGB here, w = bloom's share of the light
    vec4 lookSlope;  // rgb = slope, a = saturation
    vec4 lookOffset; // rgb = offset, a = 1 when a look is set
    vec4 lookPower;
} post;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    vec3 light = texture(uScene, inUv).rgb;
    if (post.tone.w > 0.0) light = mix(light, texture(uBloom, inUv).rgb, post.tone.w);
    vec3 c = toneMapLook(light, post.tone, post.lookSlope, post.lookOffset, post.lookPower);
    if (post.tone.z > 0.5) c = linearToSrgb(c);
    outColor = vec4(c, 1.0);
}
