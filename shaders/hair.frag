#version 450
#extension GL_GOOGLE_include_directive : require
// Hair: each ribbon is shaded as a round fibre (the normal turns across
// it) with the Kajiya-Kay diffuse and two shifted highlights of
// Marschner's model as Scheuermann made them practical: R, the white
// glint off the cuticle, shifted towards the tip, and TRT, the coloured
// one that went through the hair and back, shifted towards the root.
// Roots are darker (light reaches them through the rest of the hair).
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragTangent;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragSideS;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#define HAIR_SET 2
#include "hair_common.glsl"

float strandSpec(vec3 T, vec3 H, float exponent) {
    float th = dot(T, H);
    float sinTH = sqrt(max(1.0 - th * th, 0.0));
    return smoothstep(-1.0, 0.0, th) * pow(sinTH, exponent);
}

void main() {
    vec3 T = normalize(fragTangent);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    // Round fibre: facing the camera in the middle, turning away at the sides.
    vec3 facing = V - T * dot(V, T);
    facing = dot(facing, facing) > 1e-10 ? normalize(facing) : V;
    vec3 B = normalize(cross(T, facing));
    float x = clamp(fragSideS.x, -1.0, 1.0);
    vec3 N = normalize(facing * sqrt(max(1.0 - x * x, 0.0)) + B * x);

    vec3 albedo = srgbToLinear(fragColor);
    float s = fragSideS.y;
    float occlusion = mix(0.45, 1.0, smoothstep(0.0, 0.7, s));
    float shine = frame.tipColor.a;
    float shift = frame.look.x;
    float rough = clamp(frame.look.y, 0.05, 1.0);
    float e1 = mix(400.0, 20.0, rough), e2 = e1 * 0.25;
    vec3 T1 = normalize(T + N * shift), T2 = normalize(T - N * (shift * 1.5));

    float shadow = lighting.ambient.a > 0.5 ? computeShadow(fragPosLightSpace) : 1.0;
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        float intensity = lighting.lights[i].colorIntensity.a;
        if (intensity <= 0.0) continue;
        vec3 radiance = lighting.lights[i].colorIntensity.rgb * intensity * (i == 0 ? shadow : 1.0);
        vec3 L = lighting.lights[i].directionOrPosition.w > 0.5 ? normalize(lighting.lights[i].directionOrPosition.xyz - fragPosWorld)
                                                                : normalize(-lighting.lights[i].directionOrPosition.xyz);
        vec3 H = normalize(L + V);
        // Kajiya-Kay diffuse, wrapped: hair is lit from the side too.
        float TL = dot(T, L);
        float diffuse = sqrt(max(1.0 - TL * TL, 0.0)) * clamp((dot(N, L) + 0.5) / 1.5, 0.0, 1.0);
        float lit = clamp(dot(N, L) * 0.5 + 0.5, 0.0, 1.0);
        vec3 r = vec3(strandSpec(T1, H, e1)) * shine * 0.35;
        vec3 trt = albedo * strandSpec(T2, H, e2) * shine * 0.6;
        Lo += (albedo / PI * diffuse + (r + trt) * lit) * radiance;
    }
    vec3 ambient = ambientIrradiance(N) * albedo;
    vec3 color = applyFog((ambient + Lo) * occlusion, fragPosWorld);
    outColor = vec4(displayColor(color), 1.0);
}
