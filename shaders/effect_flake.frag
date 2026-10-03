#version 450
#extension GL_GOOGLE_include_directive : require
// Flakes (kke::ParticleEffects): small solid cards (confetti, snow, leaves,
// wood chips, shards of glass) lit by the sun, with its shadow, and the
// sky, fogged like everything else, premultiplied alpha. Edges are
// antialiased from the shape's own derivatives (no dithering).
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec4 fragParams;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragWorld;
layout(location = 4) in vec2 fragExtra; // (squash 0..1, shape: 0 square, 1 disc, 2 shard)
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"

void main() {
    // Distance-like value: < 1 inside the outline.
    float shape = fragExtra.y;
    float d;
    if (shape < 0.5) {
        d = max(abs(fragUV.x), abs(fragUV.y));
    } else if (shape < 1.5) {
        d = length(fragUV);
    } else {
        // A shard: a lopsided diamond, its tip set by the particle's seed.
        float skew = 0.3 * (fragParams.w - 0.5);
        d = abs(fragUV.x - skew * fragUV.y) * 1.4 + abs(fragUV.y);
    }
    float w = max(fwidth(d), 1e-4);
    float cover = 1.0 - smoothstep(1.0 - w, 1.0, d);
    float alpha = clamp(fragParams.y, 0.0, 1.0) * cover;
    if (alpha < 1.0 / 512.0) discard;

    // Lit as a thin card: face-on catches the sun on either side, edge-on
    // (squashed) mostly the sky. A glint where it faces the light (glass, snow).
    vec3 toCam = normalize(lighting.cameraPos.xyz - fragWorld);
    vec3 albedo = srgbToLinear(fragColor);
    float shadow = lighting.ambient.a > 0.5 ? computeShadow(lighting.lightViewProj * vec4(fragWorld, 1.0)) : 1.0;
    vec3 sun = lighting.lights[0].colorIntensity.rgb * max(lighting.lights[0].colorIntensity.a, 0.0);
    float face = fragExtra.x;
    float lambert = 0.35 + 0.65 * abs(dot(toCam, sunDirection())) * face;
    float glint = pow(max(0.0, 1.0 - abs(face - 0.5) * 2.0), 8.0) * 0.6;
    vec3 lit = albedo * (ambientIrradiance(toCam) + sun * (lambert + glint) * shadow / PI);
    lit = displayColor(applyFog(lit, fragWorld));
    outColor = vec4(lit * alpha, alpha);
}
