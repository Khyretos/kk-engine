#version 450
#extension GL_GOOGLE_include_directive : require
// Pass 1 of 2 with refraction (ENG-F02 part 4): instead of multiplying what
// is drawn behind (translucent_absorb.frag), reads the scene drawn before
// the see-through surfaces (kke::BehindPass) where the refracted ray leads,
// times the transmittance (blend ONE, ZERO). The CPU copy is
// kke::refractedSample (engine/src/Refraction.cpp). metallic = the
// index of refraction (DynamicMeshRenderer::drawTranslucent's ior).
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormalWorld;
layout(location = 2) in vec3 fragPosWorld;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) in vec2 fragMetallicRoughness;
layout(location = 5) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#include "translucent.glsl"

const float kRefractDepth = 1.0; // metres a refracted ray goes at most (the body is taken as solid that far)

vec2 uvOf(vec3 p) {
    vec4 c = lighting.viewProj * vec4(p, 1.0);
    return c.xy / c.w * 0.5 + 0.5;
}

void main() {
    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    float NdotV = translucentNdotV(N, V);
    vec3 T = translucentTransmittance(srgbToLinear(fragColor), fragUV.x, fragUV.y, NdotV);
    // The straight-through pixel, and how far behind the glass it is.
    vec2 uv = gl_FragCoord.xy / lighting.behind.yz;
    float glass = length(fragPosWorld - lighting.cameraPos.xyz);
    float behind = texture(behindImage, uv).a;
    float depth = behind > 0.0 ? clamp(behind - glass, 0.0, kRefractDepth) : kRefractDepth;
    vec3 R = refract(-V, N, 1.0 / max(fragMetallicRoughness.x, 1.0));
    if (dot(R, R) > 1e-6) {
        vec2 bent = uvOf(fragPosWorld + R * depth);
        if (all(greaterThanEqual(bent, vec2(0.0))) && all(lessThanEqual(bent, vec2(1.0)))) {
            float there = texture(behindImage, bent).a;
            // Something nearer than the glass (in front of it) is not seen through it.
            if (!(there > 0.0 && there < glass)) uv = bent;
        }
    }
    outColor = vec4(texture(behindImage, uv).rgb * T, 1.0);
}
