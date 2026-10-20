#include "kke/Pipeline.h"
#include "kke/rhi/CommandList.h"
#include "kke/rhi/VulkanCommandList.h"
#include "kke/VulkanDevice.h"
#include "kke/Mesh.h"
#include "kke/ShaderUtils.h"
#include "kke/VulkanCheck.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace kke {

namespace {

// The RHI's pipeline state (kke/rhi/PipelineState.h) in Vulkan's words.
VkPrimitiveTopology vk(rhi::Topology t) {
    switch (t) {
    case rhi::Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case rhi::Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case rhi::Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case rhi::Topology::TriangleList: break;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags vk(rhi::CullMode c) {
    return c == rhi::CullMode::None ? VK_CULL_MODE_NONE : c == rhi::CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
}

VkFrontFace vk(rhi::FrontFace f) { return f == rhi::FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE; }

VkBlendFactor vk(rhi::BlendFactor b) {
    switch (b) {
    case rhi::BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case rhi::BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    case rhi::BlendFactor::OneMinusSrcAlpha: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case rhi::BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case rhi::BlendFactor::OneMinusSrcColor: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case rhi::BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case rhi::BlendFactor::OneMinusDstColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case rhi::BlendFactor::One: break;
    }
    return VK_BLEND_FACTOR_ONE;
}

static_assert(static_cast<int>(rhi::CompareOp::Always) == VK_COMPARE_OP_ALWAYS && static_cast<int>(rhi::CompareOp::Equal) == VK_COMPARE_OP_EQUAL);
VkCompareOp vk(rhi::CompareOp c) { return static_cast<VkCompareOp>(c); } // same order as VkCompareOp

static_assert(static_cast<int>(rhi::StencilOp::DecrementAndClamp) == VK_STENCIL_OP_DECREMENT_AND_CLAMP);
VkStencilOp vk(rhi::StencilOp s) { return static_cast<VkStencilOp>(s); } // the first five of VkStencilOp, in its order

VkFormat vk(rhi::VertexFormat f) {
    switch (f) {
    case rhi::VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case rhi::VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case rhi::VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case rhi::VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::VertexFormat::Float3: break;
    }
    return VK_FORMAT_R32G32B32_SFLOAT;
}

} // namespace

Pipeline::Pipeline(VulkanDevice& device, VkRenderPass renderPass,
                    const std::string& vertPath, const std::string& fragPath,
                    const PipelineConfig& config)
    : m_device(device) {
    VkShaderModule vertModule = loadShaderModule(device, vertPath);
    VkShaderModule fragModule = loadShaderModule(device, fragPath);

    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";
    std::vector<VkSpecializationMapEntry> specEntries;
    VkSpecializationInfo specInfo{};
    if (!config.fragmentConstants.empty()) {
        for (uint32_t i = 0; i < config.fragmentConstants.size(); ++i)
            specEntries.push_back({ i, static_cast<uint32_t>(i * sizeof(uint32_t)), sizeof(uint32_t) });
        specInfo.mapEntryCount = static_cast<uint32_t>(specEntries.size());
        specInfo.pMapEntries = specEntries.data();
        specInfo.dataSize = config.fragmentConstants.size() * sizeof(uint32_t);
        specInfo.pData = config.fragmentConstants.data();
        fragStage.pSpecializationInfo = &specInfo;
    }

    VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (config.useVertexInput) {
        const bool custom = !config.customVertexBindings.empty();
        const auto defaultAttributes = Vertex::attributeDescriptions();
        const std::vector<rhi::VertexBinding> rb = custom ? config.customVertexBindings : std::vector<rhi::VertexBinding>{ Vertex::bindingDescription() };
        const std::vector<rhi::VertexAttribute> ra =
            custom ? config.customVertexAttributes : std::vector<rhi::VertexAttribute>(defaultAttributes.begin(), defaultAttributes.end());
        for (const rhi::VertexBinding& b : rb)
            bindings.push_back({ b.binding, b.stride, b.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX });
        for (const rhi::VertexAttribute& a : ra) attributes.push_back({ a.location, a.binding, vk(a.format), a.offset });
        vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }
    // else: leave vertexInput zeroed — the vertex shader synthesizes its own
    // vertices (fullscreen triangle, gl_VertexIndex into an SSBO, etc).

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = vk(config.topology);

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = vk(config.cullMode);
    rasterizer.frontFace = vk(config.frontFace);
    rasterizer.lineWidth = 1.0f;
    if (config.depthBiasConstant != 0.0f || config.depthBiasSlope != 0.0f) {
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = config.depthBiasConstant;
        rasterizer.depthBiasSlopeFactor = config.depthBiasSlope;
    }

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = device.renderPassSamples(renderPass); // MSAA scene pass or 1x

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = config.colorWriteEnable
        ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
        : 0;
    colorBlendAttachment.blendEnable = config.blendEnable ? VK_TRUE : VK_FALSE;
    if (config.blendEnable) {
        colorBlendAttachment.srcColorBlendFactor = config.premultipliedAlpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = config.premultipliedAlpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        if (config.customColorBlend) {
            colorBlendAttachment.srcColorBlendFactor = vk(config.srcColorBlendFactor);
            colorBlendAttachment.dstColorBlendFactor = vk(config.dstColorBlendFactor);
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    std::vector<VkPipelineColorBlendAttachmentState> blendStates(config.colorAttachmentCount, colorBlendAttachment);
    colorBlending.attachmentCount = config.colorAttachmentCount;
    colorBlending.pAttachments = blendStates.data();

    std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    if (config.stencilTestEnable) {
        dynamicStates.insert(dynamicStates.end(), { VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                                    VK_DYNAMIC_STATE_STENCIL_REFERENCE });
    }
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkPushConstantRange push{ rhi::VulkanCommandList::stagesOf(config.pushConstantRange.stages), config.pushConstantRange.offset,
                                    config.pushConstantRange.size };
    if (push.size > 0) {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;
    }
    layoutInfo.setLayoutCount = static_cast<uint32_t>(config.descriptorSetLayouts.size());
    layoutInfo.pSetLayouts = config.descriptorSetLayouts.empty() ? nullptr : config.descriptorSetLayouts.data();

    VK_CHECK(vkCreatePipelineLayout(m_device.device(), &layoutInfo, nullptr, &m_layout));

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = config.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = config.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (config.stencilTestEnable) {
        depthStencil.stencilTestEnable = VK_TRUE;
        VkStencilOpState op{};
        op.failOp = VK_STENCIL_OP_KEEP;
        op.depthFailOp = VK_STENCIL_OP_KEEP;
        op.passOp = vk(config.stencilPassOp);
        op.compareOp = vk(config.stencilCompareOp);
        op.compareMask = op.writeMask = 0xFF; // dynamic; placeholders
        depthStencil.front = op;
        depthStencil.back = op;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_layout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    VK_CHECK(vkCreateGraphicsPipelines(m_device.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline));

    vkDestroyShaderModule(m_device.device(), vertModule, nullptr);
    vkDestroyShaderModule(m_device.device(), fragModule, nullptr);
}

rhi::GraphicsPipelineDesc Pipeline::describe(const PipelineConfig&, std::span<const rhi::BindGroupLayout* const>) { throw std::logic_error("R1h: describe"); }

Pipeline::~Pipeline() {
    if (m_pipeline) vkDestroyPipeline(m_device.device(), m_pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device.device(), m_layout, nullptr);
}


void Pipeline::bind(VkCommandBuffer cmd) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
}

void Pipeline::bind(rhi::CommandList& list) const { list.bindPipeline(*this); }

} // namespace kke
