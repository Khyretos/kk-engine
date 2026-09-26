#version 450
#extension GL_GOOGLE_include_directive : require
// Pass 2 of 2: adds the light the surface sends to the eye (blend ONE,
// ONE): reflections, light scattered inside the body, and light shining
// through it from behind. See translucent.glsl.
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 5) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#include "translucent.glsl"

void main() {
    vec3 tint = srgbToLinear(fragColor);
    float density = fragUV.x, milkiness = clamp(fragUV.y, 0.0, 1.0);
    float roughness = clamp(fragMetallicRoughness.y, 0.05, 0.95);
    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    float NdotV = translucentNdotV(N, V);
    float F = translucentFresnel(NdotV);
    vec3 T = translucentTransmittance(tint, density, milkiness, NdotV);
    // What doesn't pass straight through is scattered (coloured by the
    // body) or absorbed; milkier bodies scatter more of it back out.
    float scatter = (1.0 - (T.r + T.g + T.b) / 3.0) * mix(0.35, 0.9, milkiness);
    float shadow = lighting.ambient.a > 0.5 ? computeShadow(fragPosLightSpace) : 1.0;

    vec3 body = lighting.ambient.rgb * tint * scatter;
    vec3 spec = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float intensity = lighting.lights[i].colorIntensity.a;
        if (intensity <= 0.0) continue;
        vec3 radiance = lighting.lights[i].colorIntensity.rgb * intensity;
        vec3 L = lighting.lights[i].directionOrPosition.w > 0.5 ? normalize(lighting.lights[i].directionOrPosition.xyz - fragPosWorld)
                                                               : normalize(-lighting.lights[i].directionOrPosition.xyz);
        float lit = i == 0 ? shadow : 1.0;
        // Wrapped diffuse: light scattered inside reaches round the side
        // a little, so the body never has a hard terminator.
        float wrap = max((dot(N, L) + 0.4) / 1.4, 0.0);
        body += tint * radiance * wrap * scatter * lit / PI * 2.0;
        // Light through the body toward the eye (a jelly held up to a lamp glows).
        float through = pow(max(dot(V, -L), 0.0), 5.0);
        body += tint * radiance * through * 0.35 * (1.0 - milkiness * 0.5);
        // Glossy surface highlight.
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float D = distributionGGX(N, H, roughness);
        float G = geometrySmith(N, V, L, roughness);
        float Fh = translucentFresnel(max(dot(H, V), 0.0));
        spec += D * G * Fh / (4.0 * NdotV * NdotL + 0.0001) * radiance * NdotL * lit;
    }
    // Reflection of the surroundings at the edges (a flat stand-in for an
    // environment map, as the opaque shader's ambient is).
    vec3 reflection = F * (lighting.ambient.rgb * 1.6 + vec3(0.06));
    vec3 color = body + spec + reflection;
    color = color / (color + vec3(1.0)); // same Reinhard as shadeSurface
    outColor = vec4(color, 1.0);
}
