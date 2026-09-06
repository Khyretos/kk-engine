#version 450

// Deliberately minimal — the shadow pass only needs depth, so this
// shader has nothing to output but clip-space position. Uses the same
// kke::Vertex layout (position/color/normal) as cube.vert so it can
// bind the exact same vertex buffers unmodified; color and normal
// attributes are simply left unused here.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProj;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}
