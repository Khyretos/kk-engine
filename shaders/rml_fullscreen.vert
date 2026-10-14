#version 450

// One triangle that covers the whole target, for kke::RmlVulkanRenderInterface's
// layer passes (ENG-G09): filters, and compositing one layer onto another.
// Draw 3 vertices with no vertex buffer; outUv is 0..1 across the target,
// (0, 0) at the top left like the framebuffer.

layout(location = 0) out vec2 outUv;

void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    outUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
