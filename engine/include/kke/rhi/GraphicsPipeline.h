#pragma once

#include "kke/rhi/PipelineState.h"

#include <cstdint>
#include <span>

namespace kke::rhi {

class BindGroupLayout; // Device.h

// A shader as the backend takes it: SPIR-V in, made by the Device (R1h). Vulkan
// keeps a VkShaderModule; the OpenGL ES 3 backend turns the SPIR-V into GLSL ES
// at build time and keeps that.
class ShaderModule {
public:
    virtual ~ShaderModule() = default;
};

// The render pass a pipeline draws into, as a handle the caller passes on (the
// passes themselves move behind the RHI in R1i).
struct RenderPassRef {
    uint64_t handle = 0;
};

// Everything a graphics pipeline is made from. The fixed state is
// kke::PipelineConfig's, in the RHI's words (R1f); the spans point at the
// caller's arrays and are read while createGraphicsPipeline runs.
struct GraphicsPipelineDesc {
    const ShaderModule* vertex = nullptr;
    const ShaderModule* fragment = nullptr;
    RenderPassRef pass{};
    uint32_t samples = 1; // of the pass's colour target (MSAA scene pass or 1)
    bool useVertexInput = true; // false: the shader makes its own vertices
    std::span<const VertexBinding> vertexBindings;
    std::span<const VertexAttribute> vertexAttributes;
    Topology topology = Topology::TriangleList;
    CullMode cullMode = CullMode::Back;
    FrontFace frontFace = FrontFace::Clockwise;
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    bool blendEnable = false;
    bool premultipliedAlpha = false;
    bool customColorBlend = false;
    BlendFactor srcColorBlendFactor = BlendFactor::One;
    BlendFactor dstColorBlendFactor = BlendFactor::Zero;
    bool stencilTestEnable = false;
    CompareOp stencilCompareOp = CompareOp::Always;
    StencilOp stencilPassOp = StencilOp::Keep;
    bool colorWriteEnable = true;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    uint32_t colorAttachmentCount = 1;
    PushRange pushConstantRange{};
    std::span<const BindGroupLayout* const> bindGroupLayouts; // set 0, 1, ... of the pipeline
    std::span<const uint32_t> fragmentConstants; // constant_id 0, 1, ..., 32 bits each
};

// A graphics pipeline made by the Device.
class GraphicsPipeline {
public:
    virtual ~GraphicsPipeline() = default;
};

} // namespace kke::rhi
