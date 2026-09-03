#version 450

layout(location = 0) in vec4 inColor;
layout(location = 1) in vec2 inTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uTexture;

void main() {
    // Untextured draws bind a persistent 1x1 opaque-white texture (see
    // RmlVulkanRenderInterface's default texture), so sampling here is
    // always valid and multiplying by white is a no-op for plain colors.
    outColor = texture(uTexture, inTexCoord) * inColor;
}
