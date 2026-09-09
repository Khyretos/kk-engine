#version 450

// Deliberately minimal — the shadow pass only needs depth, so this
// shader has nothing to output but clip-space position. Uses the same
// kke::Vertex layout as cube.vert so it can bind the exact same vertex
// buffers unmodified. color/normal/uv are simply left unused here --
// but genuinely still declared, all four of them, matching Vertex's
// own current attribute count exactly. A real bug, found and fixed:
// this used to declare only 3 (position/color/normal), from before
// Vertex grew a 4th (uv) field for material-texture work later this
// project -- leaving the pipeline's own vertex input state (always
// built from Vertex::attributeDescriptions(), see kke::Pipeline)
// describing 4 attributes while this shader only consumed 3. That
// mismatch didn't trip Vulkan validation (unused pipeline attributes
// aren't strictly invalid), but caused a real, silent failure on this
// project's own software rasterizer: every shadow-casting draw call
// produced no visible depth output at all, leaving the shadow map at
// its cleared value forever -- confirmed directly, not guessed, by a
// real diagnostic chain (light-space UV correct, matrix correct,
// texture size correct, shadow map's own sampled depth always exactly
// 1.0, the clear value) that eliminated every other candidate first.
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(push_constant) uniform ShadowPushConstants {
    mat4 lightViewProj;
    mat4 model;
} pc;

void main() {
    gl_Position = pc.lightViewProj * pc.model * vec4(inPosition, 1.0);
}
