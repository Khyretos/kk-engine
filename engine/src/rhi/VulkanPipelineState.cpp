#include "kke/rhi/VulkanPipelineState.h"

#include <stdexcept>

namespace kke::rhi::vulkan {

// R1h stubs: each unit's Coder run replaces its function.
VkPrimitiveTopology topology(Topology) { throw std::logic_error("R1h: topology"); }
VkCullModeFlags cullMode(CullMode) { throw std::logic_error("R1h: cullMode"); }
VkFrontFace frontFace(FrontFace) { throw std::logic_error("R1h: frontFace"); }
VkBlendFactor blendFactor(BlendFactor) { throw std::logic_error("R1h: blendFactor"); }
VkCompareOp compareOp(CompareOp) { throw std::logic_error("R1h: compareOp"); }
VkStencilOp stencilOp(StencilOp) { throw std::logic_error("R1h: stencilOp"); }
VkFormat vertexFormat(VertexFormat) { throw std::logic_error("R1h: vertexFormat"); }
VkPipelineColorBlendAttachmentState colorBlendAttachment(const GraphicsPipelineDesc&) { throw std::logic_error("R1h: colorBlendAttachment"); }
std::vector<VkDynamicState> dynamicStates(const GraphicsPipelineDesc&) { throw std::logic_error("R1h: dynamicStates"); }

} // namespace kke::rhi::vulkan
