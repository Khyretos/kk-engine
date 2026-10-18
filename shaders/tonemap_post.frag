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
layout(set = 0, binding = 2) uniform sampler3D uLut;   // ENG-F10: the colour LUT (size^3, read with texelFetch)

layout(push_constant) uniform Post {
    vec4 tone;       // x = kke::ToneMapper, y = exposure, z = 1: encode sRGB here, w = bloom's share of the light
    vec4 lookSlope;  // rgb = slope, a = saturation
    vec4 lookOffset; // rgb = offset, a = 1 when a look is set
    vec4 lookPower;  // rgb = power, a = the colour LUT's size (0 = none)
} post;

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

vec3 linearToSrgb(vec3 c) {
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}
vec3 srgbToLinearExact(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

// kke::ColorLut::apply: the tetrahedral read of the table, on display-encoded colour.
vec3 lutApply(vec3 c, int size) {
    vec3 p = clamp(c, 0.0, 1.0) * float(size - 1);
    ivec3 i = min(ivec3(p), ivec3(size - 2));
    vec3 f = p - vec3(i);
#define L(dr, dg, db) texelFetch(uLut, i + ivec3(dr, dg, db), 0).rgb
    vec3 c000 = L(0, 0, 0), c111 = L(1, 1, 1);
    if (f.x >= f.y) {
        if (f.y >= f.z) return c000 + (L(1, 0, 0) - c000) * f.x + (L(1, 1, 0) - L(1, 0, 0)) * f.y + (c111 - L(1, 1, 0)) * f.z;
        if (f.x >= f.z) return c000 + (L(1, 0, 0) - c000) * f.x + (L(1, 0, 1) - L(1, 0, 0)) * f.z + (c111 - L(1, 0, 1)) * f.y;
        return c000 + (L(0, 0, 1) - c000) * f.z + (L(1, 0, 1) - L(0, 0, 1)) * f.x + (c111 - L(1, 0, 1)) * f.y;
    }
    if (f.z >= f.y) return c000 + (L(0, 0, 1) - c000) * f.z + (L(0, 1, 1) - L(0, 0, 1)) * f.y + (c111 - L(0, 1, 1)) * f.x;
    if (f.z >= f.x) return c000 + (L(0, 1, 0) - c000) * f.y + (L(0, 1, 1) - L(0, 1, 0)) * f.z + (c111 - L(0, 1, 1)) * f.x;
    return c000 + (L(0, 1, 0) - c000) * f.y + (L(1, 1, 0) - L(0, 1, 0)) * f.x + (c111 - L(1, 1, 0)) * f.z;
}

void main() {
    vec3 light = texture(uScene, inUv).rgb;
    if (post.tone.w > 0.0) light = mix(light, texture(uBloom, inUv).rgb, post.tone.w);
    vec3 c = toneMapLook(light, post.tone, post.lookSlope, post.lookOffset, post.lookPower);
    int lutSize = int(post.lookPower.a + 0.5);
    if (lutSize >= 2) c = srgbToLinearExact(lutApply(linearToSrgb(clamp(c, 0.0, 1.0)), lutSize)); // a .cube grades display colour
    if (post.tone.z > 0.5) c = linearToSrgb(c);
    outColor = vec4(c, 1.0);
}
