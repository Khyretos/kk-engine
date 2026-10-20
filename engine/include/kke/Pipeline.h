#pragma once

#include "kke/rhi/GraphicsPipeline.h"
#include "kke/rhi/PipelineState.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace kke {

class VulkanDevice;
namespace rhi {
class CommandList;
}

// Every knob a graphics pipeline in this engine might need to flip.
// Grid, particles, and the cube all go through this one config struct
// instead of each getting a bespoke Pipeline subclass — new draw styles
// (wireframe, skybox, UI-in-world, ...) are new configs, not new classes.
struct PipelineConfig {
    bool useVertexInput = true;          // false = shader synthesizes its own vertices (e.g. fullscreen tri, SSBO-driven particles)
    // If useVertexInput is true and this is left empty, the pipeline uses
    // kke::Vertex's layout (position+color, see Mesh.h) — the common case
    // for this engine's own meshes. Set both of these explicitly instead
    // when binding a *different* vertex format (e.g. RmlUi's own Vertex
    // struct in RmlVulkanRenderInterface) — they must describe the same
    // buffer layout the caller will actually bind.
    std::vector<rhi::VertexBinding> customVertexBindings;
    std::vector<rhi::VertexAttribute> customVertexAttributes;
    rhi::Topology topology = rhi::Topology::TriangleList;
    rhi::CullMode cullMode = rhi::CullMode::Back;
    rhi::FrontFace frontFace = rhi::FrontFace::Clockwise;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    bool blendEnable = false;
    // Source colors are already multiplied by their own alpha (RmlUi 6
    // hands over premultiplied vertex colors and textures). Blending
    // those with SRC_ALPHA applies alpha twice — translucent content
    // comes out too dark and muddy. See BUGS.md BUG-021.
    bool premultipliedAlpha = false;
    // Set to override the colour blend factors outright (alpha keeps the
    // defaults above): e.g. ZERO/SRC_COLOR multiplies what's behind by the
    // output (coloured glass, jelly), ONE/ONE adds light.
    bool customColorBlend = false;
    rhi::BlendFactor srcColorBlendFactor = rhi::BlendFactor::One;
    rhi::BlendFactor dstColorBlendFactor = rhi::BlendFactor::Zero;
    // Stencil, for RmlUi clip masks (see RmlVulkanRenderInterface). When
    // enabled, reference/compare mask/write mask are dynamic state set
    // with vkCmdSetStencil*; the ops below are fixed per pipeline.
    bool stencilTestEnable = false;
    rhi::CompareOp stencilCompareOp = rhi::CompareOp::Always;
    rhi::StencilOp stencilPassOp = rhi::StencilOp::Keep;
    bool colorWriteEnable = true;
    // Rasterizer depth bias (both 0 = off). Shadow casters get theirs from
    // ShadowMap::casterConfig(): slope-scaled, so steep surfaces are pushed
    // further than flat ones and neither shows acne.
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    // Color attachments in the target render pass (same blend state for
    // all). 2+ for multiple render targets, 0 for depth-only passes.
    uint32_t colorAttachmentCount = 1;
    rhi::PushRange pushConstantRange{}; // size == 0 => pipeline has no push constants
    std::vector<VkDescriptorSetLayout> descriptorSetLayouts; // empty => no descriptor sets
    // The fragment shader's specialization constants (constant_id 0, 1, ...,
    // 32-bit each; a bool is 0 or 1): e.g. the UI shaders' "the target is
    // UNORM, write sRGB-encoded colour" switch (ENG-G17).
    std::vector<uint32_t> fragmentConstants;
};

// One graphics pipeline: shaders + fixed-function state for drawing
// geometry, configured by PipelineConfig above. This is the "lego piece"
// boundary — a new visual style is a new Pipeline instance with a new
// config, not a new code path in the renderer.
class Pipeline {
public:
    Pipeline(VulkanDevice& device, VkRenderPass renderPass,
              const std::string& vertPath, const std::string& fragPath,
              const PipelineConfig& config = {});
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;
    void bind(rhi::CommandList& list) const; // the RHI's way (docs/plans/rhi.md)
    VkPipelineLayout layout() const { return m_layout; }

    // `config` as the Device takes it (R1h): the fixed state, the vertex layout (kke::Vertex's when the
    // config names none and uses vertex input) and `layouts` as the bind group layouts. The shaders and
    // the pass are left for the caller; the spans point into `config` and `layouts`.
    static rhi::GraphicsPipelineDesc describe(const PipelineConfig& config, std::span<const rhi::BindGroupLayout* const> layouts);

private:
    VulkanDevice& m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

} // namespace kke
