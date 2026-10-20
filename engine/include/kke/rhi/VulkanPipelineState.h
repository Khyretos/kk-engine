#pragma once

#include "kke/rhi/GraphicsPipeline.h"

#include <volk.h>

#include <vector>

// The RHI's pipeline state (kke/rhi/PipelineState.h, GraphicsPipeline.h) in
// Vulkan's words: what VulkanGpuDevice::createGraphicsPipeline fills its
// create infos from (docs/plans/rhi.md, R1h).
namespace kke::rhi::vulkan {

VkPrimitiveTopology topology(Topology t);
VkCullModeFlags cullMode(CullMode c);
VkFrontFace frontFace(FrontFace f);
VkBlendFactor blendFactor(BlendFactor b);
VkCompareOp compareOp(CompareOp c);
VkStencilOp stencilOp(StencilOp s);
VkFormat vertexFormat(VertexFormat f);

// The one colour attachment state every colour attachment of the pass gets:
// write mask, and the blend factors for plain alpha, premultiplied alpha or the
// desc's own factors (customColorBlend); alpha always blends ONE / (ONE_MINUS_SRC_ALPHA or ZERO).
VkPipelineColorBlendAttachmentState colorBlendAttachment(const GraphicsPipelineDesc& desc);

// Viewport and scissor are always dynamic; a stencil test adds its compare mask, write mask and reference.
std::vector<VkDynamicState> dynamicStates(const GraphicsPipelineDesc& desc);

} // namespace kke::rhi::vulkan
