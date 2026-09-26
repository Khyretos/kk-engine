#version 450
#extension GL_GOOGLE_include_directive : require

layout(location = 0) in vec3 fragCenter;
layout(location = 1) in vec3 fragColor;
layout(location = 2) in vec3 fragParams;
layout(location = 3) in vec2 fragCorner;
layout(location = 0) out vec4 outColor;

#include "pbr_common.glsl"
#include "glow.glsl"

void main() {
    float r2 = dot(fragCorner, fragCorner);
    vec3 toCam = normalize(lighting.cameraPos.xyz - fragCenter);
    vec3 right = normalize(cross(abs(toCam.y) > 0.99 ? vec3(1, 0, 0) : vec3(0, 1, 0), toCam));
    vec3 up = cross(toCam, right);
    vec3 N = normalize(right * fragCorner.x + up * fragCorner.y + toCam * sqrt(max(1.0 - r2, 0.0)));
    float specAA = specularAAKernel(N); // before the discard: it takes derivatives
    if (r2 > 1.0) discard;
    vec3 P = fragCenter + N * fragParams.x;
    // Real depth of the sphere surface, so spheres intersect each other
    // and the world correctly (it disables early-Z for this pipeline; worth
    // it for a few thousand particles).
    vec4 clip = lighting.viewProj * vec4(P, 1.0);
    gl_FragDepth = clip.z / clip.w;
    vec3 albedo = srgbToLinear(fragColor);
    vec3 lit = shadeSurfaceAA(albedo, vec2(0.0, fragParams.z), N, P, lighting.lightViewProj * vec4(P, 1.0), specAA);
    outColor = vec4(lit + glowColor(fragParams.y), 1.0);
}
