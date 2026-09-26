#version 450

// A screen-space quad for the intro's title image (kke::LogoIntro).
layout(push_constant) uniform TextPush {
    vec4 rect;   // NDC x0, y0, x1, y1 (+Y down)
    vec4 params; // x alpha, y line split, z sheen
} pc;

layout(location = 0) out vec2 outUV;

void main() {
    const vec2 corners[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 0), vec2(1, 1), vec2(0, 1));
    vec2 c = corners[gl_VertexIndex];
    outUV = c;
    gl_Position = vec4(mix(pc.rect.xy, pc.rect.zw, c), 0.0, 1.0);
}
