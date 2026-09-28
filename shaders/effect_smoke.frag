#version 450
#extension GL_GOOGLE_include_directive : require
// Smoke puffs (kke::ParticleEffects): a soft, lumpy disc lit like a ball
// of haze by the sun (with the shadow map: smoke in a car's shadow is
// darker) and the sky, fogged like everything else, premultiplied alpha.
// Soft falloff from filtering the shape itself, no dithering or noise
// textures (docs/RENDERING_PRINCIPLES.md).
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec4 fragParams;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorld;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"

void main() {
    float r = length(fragUV);
    if (r >= 1.0) discard;
    // Lumps: a few smooth lobes around the rim (from the particle's own
    // seed), so neighbouring puffs don't look like the same stamp.
    float a = atan(fragUV.y, fragUV.x);
    float seed = fragParams.w * 6.2831853;
    float lumps = 0.08 * sin(3.0 * a + seed) + 0.06 * sin(5.0 * a + seed * 1.7) + 0.04 * sin(7.0 * a - seed * 2.3);
    float edge = 0.92 + lumps;
    float body = 1.0 - smoothstep(edge * 0.25, edge, r);
    float alpha = clamp(fragParams.y, 0.0, 1.0) * body * body;
    if (alpha < 1.0 / 512.0) discard;

    // Lit as a sphere of haze: the normal bulges out of the sprite.
    vec3 toCam = normalize(lighting.cameraPos.xyz - fragWorld);
    vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
    vec3 up = cross(toCam, right);
    vec3 N = normalize(right * fragUV.x + up * fragUV.y + toCam * sqrt(max(1.0 - r * r, 0.0)));
    vec3 albedo = srgbToLinear(fragColor);
    float shadow = lighting.ambient.a > 0.5 ? computeShadow(lighting.lightViewProj * vec4(fragWorld, 1.0)) : 1.0;
    vec3 sun = lighting.lights[0].colorIntensity.rgb * max(lighting.lights[0].colorIntensity.a, 0.0);
    // Haze scatters: light wraps round it (half-Lambert), a little always gets through.
    float wrap = dot(N, sunDirection()) * 0.5 + 0.5;
    vec3 lit = albedo * (ambientIrradiance(N) + sun * (0.25 + 0.75 * wrap * wrap) * shadow / PI);
    lit = displayColor(applyFog(lit, fragWorld));
    outColor = vec4(lit * alpha, alpha);
}
