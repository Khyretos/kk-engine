#version 450
#extension GL_GOOGLE_include_directive : require
// Pass 1 of 2: multiplies what's already drawn behind the surface by the
// transmittance (blend ZERO, SRC_COLOR). See translucent.glsl.
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
    vec3 N = normalize(fragNormalWorld);
    vec3 V = normalize(lighting.cameraPos.xyz - fragPosWorld);
    float NdotV = translucentNdotV(N, V);
    outColor = vec4(translucentTransmittance(srgbToLinear(fragColor), fragUV.x, fragUV.y, NdotV), 1.0);
}
