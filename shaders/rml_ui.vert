#version 450

// Matches Rml::Vertex exactly: Vector2f position, ColourbPremultiplied
// colour (4x unorm8), Vector2f tex_coord — see RmlVulkanRenderInterface.h.
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(push_constant) uniform PushConstants {
    vec2 screenSize;   // pixels
    vec2 translation;  // pixels, RmlUi's per-draw-call offset
} pc;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outTexCoord;

void main() {
    vec2 pixelPos = inPosition + pc.translation;
    // Vulkan's default viewport transform maps NDC (-1,-1) to the
    // top-left of the framebuffer already (no Y-flip needed here, unlike
    // the 3D pipelines' GLM projection matrices) — RmlUi's pixel space is
    // also top-left-origin, so this is a direct, unflipped mapping.
    vec2 ndc = (pixelPos / pc.screenSize) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);

    outColor = inColor;
    outTexCoord = inTexCoord;
}
