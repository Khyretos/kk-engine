// The RHI's graphics pipeline (docs/plans/rhi.md, R1h): the state a pipeline is
// made from reads the same in Vulkan's words as it did when Pipeline.cpp held it.

#include "kke/ShaderUtils.h"
#include "kke/rhi/GraphicsPipeline.h"
#include "kke/rhi/VulkanDevice.h"
#include "kke/rhi/VulkanPipelineState.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace kke;

TEST(RhiGraphicsPipeline, StateBecomesVulkanEnums) {
    EXPECT_EQ(rhi::vulkan::topology(rhi::Topology::TriangleList), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    EXPECT_EQ(rhi::vulkan::topology(rhi::Topology::TriangleStrip), VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP);
    EXPECT_EQ(rhi::vulkan::topology(rhi::Topology::LineList), VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
    EXPECT_EQ(rhi::vulkan::topology(rhi::Topology::PointList), VK_PRIMITIVE_TOPOLOGY_POINT_LIST);
    EXPECT_EQ(rhi::vulkan::cullMode(rhi::CullMode::None), VkCullModeFlags(VK_CULL_MODE_NONE));
    EXPECT_EQ(rhi::vulkan::cullMode(rhi::CullMode::Back), VkCullModeFlags(VK_CULL_MODE_BACK_BIT));
    EXPECT_EQ(rhi::vulkan::cullMode(rhi::CullMode::Front), VkCullModeFlags(VK_CULL_MODE_FRONT_BIT));
    EXPECT_EQ(rhi::vulkan::frontFace(rhi::FrontFace::Clockwise), VK_FRONT_FACE_CLOCKWISE);
    EXPECT_EQ(rhi::vulkan::frontFace(rhi::FrontFace::CounterClockwise), VK_FRONT_FACE_COUNTER_CLOCKWISE);
    EXPECT_EQ(rhi::vulkan::compareOp(rhi::CompareOp::Never), VK_COMPARE_OP_NEVER);
    EXPECT_EQ(rhi::vulkan::compareOp(rhi::CompareOp::LessOrEqual), VK_COMPARE_OP_LESS_OR_EQUAL);
    EXPECT_EQ(rhi::vulkan::compareOp(rhi::CompareOp::Always), VK_COMPARE_OP_ALWAYS);
    EXPECT_EQ(rhi::vulkan::stencilOp(rhi::StencilOp::Keep), VK_STENCIL_OP_KEEP);
    EXPECT_EQ(rhi::vulkan::stencilOp(rhi::StencilOp::Replace), VK_STENCIL_OP_REPLACE);
    EXPECT_EQ(rhi::vulkan::stencilOp(rhi::StencilOp::DecrementAndClamp), VK_STENCIL_OP_DECREMENT_AND_CLAMP);
}

TEST(RhiGraphicsPipeline, BlendFactorsAndVertexFormatsMapOneToOne) {
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::Zero), VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::One), VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::SrcAlpha), VK_BLEND_FACTOR_SRC_ALPHA);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::OneMinusSrcAlpha), VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::SrcColor), VK_BLEND_FACTOR_SRC_COLOR);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::OneMinusSrcColor), VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::DstColor), VK_BLEND_FACTOR_DST_COLOR);
    EXPECT_EQ(rhi::vulkan::blendFactor(rhi::BlendFactor::OneMinusDstColor), VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR);
    EXPECT_EQ(rhi::vulkan::vertexFormat(rhi::VertexFormat::Float), VK_FORMAT_R32_SFLOAT);
    EXPECT_EQ(rhi::vulkan::vertexFormat(rhi::VertexFormat::Float2), VK_FORMAT_R32G32_SFLOAT);
    EXPECT_EQ(rhi::vulkan::vertexFormat(rhi::VertexFormat::Float3), VK_FORMAT_R32G32B32_SFLOAT);
    EXPECT_EQ(rhi::vulkan::vertexFormat(rhi::VertexFormat::Float4), VK_FORMAT_R32G32B32A32_SFLOAT);
    EXPECT_EQ(rhi::vulkan::vertexFormat(rhi::VertexFormat::UByte4Norm), VK_FORMAT_R8G8B8A8_UNORM);
}

TEST(RhiGraphicsPipeline, WithoutBlendingTheAttachmentWritesAllFourChannels) {
    rhi::GraphicsPipelineDesc d;
    const VkPipelineColorBlendAttachmentState s = rhi::vulkan::colorBlendAttachment(d);
    EXPECT_EQ(s.blendEnable, VkBool32(VK_FALSE));
    EXPECT_EQ(s.colorWriteMask, VkColorComponentFlags(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                                      VK_COLOR_COMPONENT_A_BIT));
    d.colorWriteEnable = false;
    EXPECT_EQ(rhi::vulkan::colorBlendAttachment(d).colorWriteMask, VkColorComponentFlags(0));
}

TEST(RhiGraphicsPipeline, PlainAlphaBlendingKeepsTheDestinationAlphaOut) {
    rhi::GraphicsPipelineDesc d;
    d.blendEnable = true;
    const VkPipelineColorBlendAttachmentState s = rhi::vulkan::colorBlendAttachment(d);
    EXPECT_EQ(s.blendEnable, VkBool32(VK_TRUE));
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_SRC_ALPHA);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.colorBlendOp, VK_BLEND_OP_ADD);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(s.alphaBlendOp, VK_BLEND_OP_ADD);
}

TEST(RhiGraphicsPipeline, PremultipliedAlphaBlendsOneOverOneMinusSourceAlpha) {
    rhi::GraphicsPipelineDesc d;
    d.blendEnable = true;
    d.premultipliedAlpha = true;
    const VkPipelineColorBlendAttachmentState s = rhi::vulkan::colorBlendAttachment(d);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
}

TEST(RhiGraphicsPipeline, ACustomColourBlendReplacesOnlyTheColourFactors) {
    rhi::GraphicsPipelineDesc d;
    d.blendEnable = true;
    d.customColorBlend = true;
    d.srcColorBlendFactor = rhi::BlendFactor::Zero;
    d.dstColorBlendFactor = rhi::BlendFactor::SrcColor;
    const VkPipelineColorBlendAttachmentState s = rhi::vulkan::colorBlendAttachment(d);
    EXPECT_EQ(s.srcColorBlendFactor, VK_BLEND_FACTOR_ZERO);
    EXPECT_EQ(s.dstColorBlendFactor, VK_BLEND_FACTOR_SRC_COLOR);
    EXPECT_EQ(s.srcAlphaBlendFactor, VK_BLEND_FACTOR_ONE);
    EXPECT_EQ(s.dstAlphaBlendFactor, VK_BLEND_FACTOR_ZERO);
    // With blending off the custom factors are not read.
    d.blendEnable = false;
    EXPECT_EQ(rhi::vulkan::colorBlendAttachment(d).blendEnable, VkBool32(VK_FALSE));
}

TEST(RhiGraphicsPipeline, ViewportAndScissorAreDynamicAndAStencilTestAddsItsThree) {
    rhi::GraphicsPipelineDesc d;
    const std::vector<VkDynamicState> plain = rhi::vulkan::dynamicStates(d);
    ASSERT_EQ(plain.size(), 2u);
    EXPECT_EQ(plain[0], VK_DYNAMIC_STATE_VIEWPORT);
    EXPECT_EQ(plain[1], VK_DYNAMIC_STATE_SCISSOR);
    d.stencilTestEnable = true;
    const std::vector<VkDynamicState> stencil = rhi::vulkan::dynamicStates(d);
    ASSERT_EQ(stencil.size(), 5u);
    EXPECT_EQ(stencil[2], VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK);
    EXPECT_EQ(stencil[3], VK_DYNAMIC_STATE_STENCIL_WRITE_MASK);
    EXPECT_EQ(stencil[4], VK_DYNAMIC_STATE_STENCIL_REFERENCE);
}

TEST(RhiGraphicsPipeline, ASpirvFileBecomesItsWords) {
    const std::string path = (std::filesystem::temp_directory_path() / "kke_r1h_words.spv").string();
    const uint32_t words[] = { 0x07230203u, 0x00010000u, 0xdeadbeefu };
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(words), sizeof(words));
    }
    const std::vector<uint32_t> read = loadSpirv(path);
    ASSERT_EQ(read.size(), 3u);
    EXPECT_EQ(read[0], 0x07230203u);
    EXPECT_EQ(read[2], 0xdeadbeefu);
    std::remove(path.c_str());
}

TEST(RhiGraphicsPipeline, ASpirvFileThatIsMissingOrNotWholeWordsThrows) {
    EXPECT_THROW(loadSpirv("/nonexistent/kke_r1h.spv"), std::runtime_error);
    const std::string path = (std::filesystem::temp_directory_path() / "kke_r1h_odd.spv").string();
    {
        std::ofstream f(path, std::ios::binary);
        f.write("abcde", 5);
    }
    EXPECT_THROW(loadSpirv(path), std::runtime_error);
    std::remove(path.c_str());
}

TEST(RhiGraphicsPipeline, ABorrowedLayoutNamesTheHandleAndLeavesItAlone) {
    const VkDescriptorSetLayout handle = reinterpret_cast<VkDescriptorSetLayout>(static_cast<uintptr_t>(0x1234));
    {
        const std::unique_ptr<rhi::VulkanBindGroupLayout> layout = rhi::VulkanBindGroupLayout::borrowed(handle);
        ASSERT_NE(layout, nullptr);
        EXPECT_EQ(layout->vk(), handle);
        EXPECT_EQ(rhi::VulkanBindGroupLayout::vk(*layout), handle);
    } // destroying it must not call vkDestroyDescriptorSetLayout: there is no device here
}
