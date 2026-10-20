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

Pipeline::Pipeline(VulkanDevice& device, VkRenderPass renderPass, const std::string& vertPath, const std::string& fragPath, const PipelineConfig& config) : m_device(device) {
    rhi::VulkanGpuDevice gpu(device);
    auto vert = gpu.createShaderModule(loadSpirv(vertPath));
    auto frag = gpu.createShaderModule(loadSpirv(fragPath));

    std::vector<std::unique_ptr<rhi::VulkanBindGroupLayout>> owned;
    std::vector<const rhi::BindGroupLayout*> layouts;
    for (auto l : config.descriptorSetLayouts) {
        owned.push_back(rhi::VulkanBindGroupLayout::borrowed(l));
        layouts.push_back(owned.back().get());
    }

    rhi::GraphicsPipelineDesc desc = describe(config, layouts);
    desc.vertex = vert.get();
    desc.fragment = frag.get();
    desc.pass = { rhi::VulkanCommandList::toHandle(renderPass) };
    desc.samples = static_cast<uint32_t>(device.renderPassSamples(renderPass));

    m_pipeline = gpu.createGraphicsPipeline(desc);
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

    rhi::GraphicsPipelineDesc desc{};
    desc.vertex = nullptr;
    desc.fragment = nullptr;
    desc.pass = {};
    desc.samples = 1;
    desc.useVertexInput = config.useVertexInput;
    desc.vertexBindings = bindings;
    desc.vertexAttributes = attributes;
    desc.topology = config.topology;
    desc.cullMode = config.cullMode;
    desc.frontFace = config.frontFace;
    desc.depthTestEnable = config.depthTestEnable;
    desc.depthWriteEnable = config.depthWriteEnable;
    desc.blendEnable = config.blendEnable;
    desc.premultipliedAlpha = config.premultipliedAlpha;
    desc.customColorBlend = config.customColorBlend;
    desc.srcColorBlendFactor = config.srcColorBlendFactor;
    desc.dstColorBlendFactor = config.dstColorBlendFactor;
    desc.stencilTestEnable = config.stencilTestEnable;
    desc.stencilCompareOp = config.stencilCompareOp;
    desc.stencilPassOp = config.stencilPassOp;
    desc.colorWriteEnable = config.colorWriteEnable;
    desc.depthBiasConstant = config.depthBiasConstant;
    desc.depthBiasSlope = config.depthBiasSlope;
    desc.colorAttachmentCount = config.colorAttachmentCount;
    desc.pushConstantRange = config.pushConstantRange;
    desc.bindGroupLayouts = layouts;
    desc.fragmentConstants = config.fragmentConstants;
    return desc;
}

Pipeline::~Pipeline() = default;

VkPipelineLayout Pipeline::layout() const { return static_cast<const rhi::VulkanGraphicsPipeline&>(*m_pipeline).layout(); }

void Pipeline::bind(VkCommandBuffer cmd) const { vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, static_cast<const rhi::VulkanGraphicsPipeline&>(*m_pipeline).pipeline()); }

void Pipeline::bind(rhi::CommandList& list) const { list.bindPipeline(*this); }

} // namespace kke
