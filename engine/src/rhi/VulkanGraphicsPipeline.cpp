#include "kke/rhi/VulkanDevice.h"
#include "kke/rhi/VulkanPipelineState.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <vector>

namespace kke::rhi {

VulkanGraphicsPipeline::~VulkanGraphicsPipeline() {
    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_layout) vkDestroyPipelineLayout(m_device, m_layout, nullptr);
}

std::unique_ptr<GraphicsPipeline> VulkanGpuDevice::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    VkShaderModule vertModule = VulkanShaderModule::vk(*desc.vertex);
    VkShaderModule fragModule = VulkanShaderModule::vk(*desc.fragment);

    std::vector<VkSpecializationMapEntry> specEntries;
    VkSpecializationInfo specInfo{};
    if (!desc.fragmentConstants.empty()) {
        for (uint32_t i = 0; i < desc.fragmentConstants.size(); ++i) {
            specEntries.push_back({ i, static_cast<uint32_t>(i * sizeof(uint32_t)), sizeof(uint32_t) });
        }
        specInfo.mapEntryCount = static_cast<uint32_t>(specEntries.size());
        specInfo.pMapEntries = specEntries.data();
        specInfo.dataSize = desc.fragmentConstants.size() * sizeof(uint32_t);
        specInfo.pData = desc.fragmentConstants.data();
    }

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
    if (specInfo.mapEntryCount) fragStage.pSpecializationInfo = &specInfo;

    std::vector<VkPipelineShaderStageCreateInfo> stages = { vertStage, fragStage };

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.useVertexInput) {
        for (const auto& b : desc.vertexBindings) {
            bindings.push_back({ b.binding, b.stride, b.perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX });
        }
        for (const auto& a : desc.vertexAttributes) {
            attributes.push_back({ a.location, a.binding, rhi::vulkan::vertexFormat(a.format), a.offset });
        }
        vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
        vertexInput.pVertexBindingDescriptions = bindings.data();
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = rhi::vulkan::topology(desc.topology);

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = rhi::vulkan::cullMode(desc.cullMode);
    rasterizer.frontFace = rhi::vulkan::frontFace(desc.frontFace);
    rasterizer.lineWidth = 1.0f;
    if (desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f) {
        rasterizer.depthBiasEnable = VK_TRUE;
        rasterizer.depthBiasConstantFactor = desc.depthBiasConstant;
        rasterizer.depthBiasSlopeFactor = desc.depthBiasSlope;
    }

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = static_cast<VkSampleCountFlagBits>(desc.samples);

    VkPipelineColorBlendAttachmentState colorBlendAttachment = rhi::vulkan::colorBlendAttachment(desc);
    std::vector<VkPipelineColorBlendAttachmentState> blendStates(desc.colorAttachmentCount, colorBlendAttachment);

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = static_cast<uint32_t>(blendStates.size());
    colorBlending.pAttachments = blendStates.data();

    std::vector<VkDynamicState> dynamicStates = rhi::vulkan::dynamicStates(desc);

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    std::vector<VkDescriptorSetLayout> setLayouts;
    for (const BindGroupLayout* layout : desc.bindGroupLayouts) setLayouts.push_back(VulkanBindGroupLayout::vk(*layout));
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    const VkPushConstantRange push{ VulkanCommandList::stagesOf(desc.pushConstantRange.stages), desc.pushConstantRange.offset,
                                    desc.pushConstantRange.size };
    if (push.size > 0) {
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &push;
    }
    layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.empty() ? nullptr : setLayouts.data();

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(m_device.device(), &layoutInfo, nullptr, &pipelineLayout));

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (desc.stencilTestEnable) {
        depthStencil.stencilTestEnable = VK_TRUE;
        VkStencilOpState op{};
        op.failOp = VK_STENCIL_OP_KEEP;
        op.depthFailOp = VK_STENCIL_OP_KEEP;
        op.passOp = rhi::vulkan::stencilOp(desc.stencilPassOp);
        op.compareOp = rhi::vulkan::compareOp(desc.stencilCompareOp);
        op.compareMask = op.writeMask = 0xFF; // dynamic; placeholders
        depthStencil.front = op;
        depthStencil.back = op;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = VulkanCommandList::fromHandle<VkRenderPass>(desc.pass.handle);
    pipelineInfo.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(m_device.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline));

    return std::make_unique<VulkanGraphicsPipeline>(m_device.device(), pipeline, pipelineLayout);
}

} // namespace kke::rhi
