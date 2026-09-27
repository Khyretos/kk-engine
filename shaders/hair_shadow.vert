#version 450
#extension GL_GOOGLE_include_directive : require
// Drawn hairs into the shadow map: the same ribbons as hair.vert, facing
// the light, a little wider (a clump shadows more than one hair's width).
#define HAIR_SET 0
#include "hair_common.glsl"

layout(push_constant) uniform ShadowPush {
    mat4 lightViewProj;
    vec4 lightDir; // xyz: towards the light
} pc;

void main() {
    int hair, point;
    float side, s;
    hairCorner(gl_VertexIndex, hair, point, side);
    vec3 p, t;
    hairCentre(hair, point, p, t, s);
    vec3 world = p + across(t, pc.lightDir.xyz) * (side * 0.5 * frame.look.w * frame.shape.z * (1.0 - 0.6 * s));
    gl_Position = pc.lightViewProj * vec4(world, 1.0);
}
