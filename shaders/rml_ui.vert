#version 450

// Matches Rml::Vertex exactly: Vector2f position, ColourbPremultiplied
// colour (4x unorm8), Vector2f tex_coord — see RmlVulkanRenderInterface.h.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 transform;    // pixel-space projection * the element's CSS transform
    vec2 translation;  // pixels, RmlUi's per-draw-call offset
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;

void main() {
    // Translation is applied before the transform, matching RmlUi's own
    // GL3 backend: transforms are relative to the element's own origin.
    gl_Position = pc.transform * vec4(inPosition + pc.translation, 0.0, 1.0);
    outColor = inColor;
    outTexCoord = inTexCoord;
}
