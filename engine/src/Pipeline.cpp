#include "kke/Pipeline.h"
#include "kke/rhi/CommandList.h"
#include "kke/rhi/VulkanCommandList.h"
#include "kke/rhi/VulkanDevice.h"
#include "kke/VulkanDevice.h"
#include "kke/Mesh.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace kke {

// R1h stubs: the Coder's run replaces them.
Pipeline::Pipeline(VulkanDevice& device, VkRenderPass, const std::string&, const std::string&, const PipelineConfig&) : m_device(device) {
    throw std::logic_error("R1h: Pipeline");
}

rhi::GraphicsPipelineDesc Pipeline::describe(const PipelineConfig& config, std::span<const rhi::BindGroupLayout* const> layouts) {
    static const rhi::VertexBinding kBinding = Vertex::bindingDescription();
    static const std::array<rhi::VertexAttribute, 4> kAttributes = Vertex::attributeDescriptions();
    const bool custom = !config.customVertexBindings.empty();
    std::span<const rhi::VertexBinding> bindings;
    std::span<const rhi::VertexAttribute> attributes;
    if (config.useVertexInput) {
        bindings = custom ? std::span<const rhi::VertexBinding>(config.customVertexBindings) : std::span<const rhi::VertexBinding>(&kBinding, 1);
        attributes = custom ? std::span<const rhi::VertexAttribute>(config.customVertexAttributes) : std::span<const rhi::VertexAttribute>(kAttributes);
    }

    return rhi::GraphicsPipelineDesc{
        .vertex = nullptr,
        .fragment = nullptr,
        .pass = {},
        .samples = 1,
        .useVertexInput = config.useVertexInput,
        .vertexBindings = bindings,
        .vertexAttributes = attributes,
        .topology = config.topology,
        .cullMode = config.cullMode,
        .frontFace = config.frontFace,
        .depthTestEnable = config.depthTestEnable,
        .depthWriteEnable = config.depthWriteEnable,
        .blendEnable = config.blendEnable,
        .premultipliedAlpha = config.premultipliedAlpha,
        .customColorBlend = config.customColorBlend,
        .srcColorBlendFactor = config.srcColorBlendFactor,
        .dstColorBlendFactor = config.dstColorBlendFactor,
        .stencilTestEnable = config.stencilTestEnable,
        .stencilCompareOp = config.stencilCompareOp,
        .stencilPassOp = config.stencilPassOp,
        .colorWriteEnable = config.colorWriteEnable,
        .depthBiasConstant = config.depthBiasConstant,
        .depthBiasSlope = config.depthBiasSlope,
        .colorAttachmentCount = config.colorAttachmentCount,
        .pushConstantRange = config.pushConstantRange,
        .bindGroupLayouts = layouts,
        .fragmentConstants = config.fragmentConstants,
    };
}

Pipeline::~Pipeline() = default;

VkPipelineLayout Pipeline::layout() const { throw std::logic_error("R1h: layout"); }

void Pipeline::bind(VkCommandBuffer) const { throw std::logic_error("R1h: bind"); }

void Pipeline::bind(rhi::CommandList& list) const { list.bindPipeline(*this); }

} // namespace kke
