#version 450
#extension GL_GOOGLE_include_directive : require
// Drawn hairs: camera-facing ribbons expanded from the guide strands
// (hair_common.glsl), no vertex buffer. Shaded by hair.frag.
#include "lighting_ubo.glsl"
#define HAIR_SET 2
#include "hair_common.glsl"

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragTangent;
layout(location = 2) out vec3 fragPosWorld;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) out vec2 fragSideS; // x = -1..1 across the ribbon, y = root 0 .. tip 1

void main() {
    int hair, point;
    float side, s;
    hairCorner(gl_VertexIndex, hair, point, side);
    vec3 p, t;
    hairCentre(hair, point, p, t, s);
    vec3 view = lighting.cameraPos.xyz - p;
    // Tapers to the tip, never thinner than most of a pixel (thinner
    // ribbons would flicker in and out between pixels).
    float w = 0.5 * max(frame.counts.z * (1.0 - 0.6 * s), 0.75 * frame.counts.w * length(view));
    vec3 world = p + across(t, view) * (side * w);
    gl_Position = lighting.viewProj * vec4(world, 1.0);
    fragColor = mix(frame.rootColor.rgb, frame.tipColor.rgb, s) * hairs[hair].color.rgb;
    fragTangent = t;
    fragPosWorld = world;
    fragPosLightSpace = lighting.lightViewProj * vec4(world, 1.0);
    fragSideS = vec2(side, s);
}
