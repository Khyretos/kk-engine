#include "kke/rhi/VulkanPipelineState.h"

namespace kke::rhi::vulkan {

VkPrimitiveTopology topology(rhi::Topology t) {
    switch (t) {
    case rhi::Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case rhi::Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case rhi::Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case rhi::Topology::TriangleList: break;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags cullMode(rhi::CullMode c) {
    return c == rhi::CullMode::None ? VK_CULL_MODE_NONE : c == rhi::CullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
}

VkFrontFace frontFace(rhi::FrontFace f) { return f == rhi::FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE; }

VkBlendFactor blendFactor(rhi::BlendFactor b) {
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
VkCompareOp compareOp(rhi::CompareOp c) { return static_cast<VkCompareOp>(c); } // same order as VkCompareOp

static_assert(static_cast<int>(rhi::StencilOp::DecrementAndClamp) == VK_STENCIL_OP_DECREMENT_AND_CLAMP);
VkStencilOp stencilOp(rhi::StencilOp s) { return static_cast<VkStencilOp>(s); } // the first five of VkStencilOp, in its order

VkFormat vertexFormat(rhi::VertexFormat f) {
    switch (f) {
    case rhi::VertexFormat::Float: return VK_FORMAT_R32_SFLOAT;
    case rhi::VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case rhi::VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case rhi::VertexFormat::UByte4Norm: return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::VertexFormat::Float3: break;
    }
    return VK_FORMAT_R32G32B32_SFLOAT;
}
VkPipelineColorBlendAttachmentState colorBlendAttachment(const GraphicsPipelineDesc& desc) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = desc.colorWriteEnable
        ? (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
        : 0;
    state.blendEnable = desc.blendEnable ? VK_TRUE : VK_FALSE;
    if (desc.blendEnable) {
        state.srcColorBlendFactor = desc.premultipliedAlpha ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = VK_BLEND_OP_ADD;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = desc.premultipliedAlpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
        state.alphaBlendOp = VK_BLEND_OP_ADD;
        if (desc.customColorBlend) {
            state.srcColorBlendFactor = blendFactor(desc.srcColorBlendFactor);
            state.dstColorBlendFactor = blendFactor(desc.dstColorBlendFactor);
        }
    }
    return state;
}

std::vector<VkDynamicState> dynamicStates(const GraphicsPipelineDesc& desc) {
    std::vector<VkDynamicState> states = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    if (desc.stencilTestEnable) {
        states.insert(states.end(), { VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
                                      VK_DYNAMIC_STATE_STENCIL_REFERENCE });
    }
    return states;
}

} // namespace kke::rhi::vulkan
