// The RHI's pipeline state (docs/plans/rhi.md, R1f): PipelineConfig and the
// engine's vertex say what they need without Vulkan's types, so another
// backend (OpenGL ES 3 / WebGL2) can read them.

#include "kke/Mesh.h"
#include "kke/Pipeline.h"
#include "kke/rhi/Device.h"

#include <gtest/gtest.h>

#include <cstddef>

using namespace kke;

TEST(RhiPipelineState, DefaultsAreTheEnginesUsualPipeline) {
    const PipelineConfig c;
    EXPECT_EQ(c.topology, rhi::Topology::TriangleList);
    EXPECT_EQ(c.cullMode, rhi::CullMode::Back);
    EXPECT_EQ(c.frontFace, rhi::FrontFace::Clockwise);
    EXPECT_EQ(c.srcColorBlendFactor, rhi::BlendFactor::One);
    EXPECT_EQ(c.dstColorBlendFactor, rhi::BlendFactor::Zero);
    EXPECT_EQ(c.pushConstantRange.size, 0u);
}

TEST(RhiPipelineState, TheVertexIsFourFloatAttributesInOneBinding) {
    const rhi::VertexBinding b = Vertex::bindingDescription();
    EXPECT_EQ(b.binding, 0u);
    EXPECT_EQ(b.stride, sizeof(Vertex));
    EXPECT_FALSE(b.perInstance);
    const auto a = Vertex::attributeDescriptions();
    EXPECT_EQ(a[0].format, rhi::VertexFormat::Float3);
    EXPECT_EQ(a[0].offset, offsetof(Vertex, position));
    EXPECT_EQ(a[2].offset, offsetof(Vertex, normal));
    EXPECT_EQ(a[3].format, rhi::VertexFormat::Float2);
    EXPECT_EQ(a[3].offset, offsetof(Vertex, uv));
    for (uint32_t i = 0; i < 4; ++i) EXPECT_EQ(a[i].location, i);
}

TEST(RhiPipelineState, DescribeCopiesTheFixedStateAndNamesTheVertexLayout) {
    PipelineConfig c;
    c.topology = rhi::Topology::LineList;
    c.cullMode = rhi::CullMode::None;
    c.frontFace = rhi::FrontFace::CounterClockwise;
    c.depthTestEnable = false;
    c.depthWriteEnable = false;
    c.blendEnable = true;
    c.premultipliedAlpha = true;
    c.customColorBlend = true;
    c.srcColorBlendFactor = rhi::BlendFactor::DstColor;
    c.dstColorBlendFactor = rhi::BlendFactor::SrcColor;
    c.stencilTestEnable = true;
    c.stencilCompareOp = rhi::CompareOp::Equal;
    c.stencilPassOp = rhi::StencilOp::Replace;
    c.colorWriteEnable = false;
    c.depthBiasConstant = 1.5f;
    c.depthBiasSlope = 2.5f;
    c.colorAttachmentCount = 3;
    c.pushConstantRange = { rhi::kVertex | rhi::kFragment, 4, 64 };
    c.fragmentConstants = { 1u, 0u, 7u };
    const rhi::GraphicsPipelineDesc d = Pipeline::describe(c, {});
    EXPECT_EQ(d.topology, rhi::Topology::LineList);
    EXPECT_EQ(d.cullMode, rhi::CullMode::None);
    EXPECT_EQ(d.frontFace, rhi::FrontFace::CounterClockwise);
    EXPECT_FALSE(d.depthTestEnable);
    EXPECT_FALSE(d.depthWriteEnable);
    EXPECT_TRUE(d.blendEnable);
    EXPECT_TRUE(d.premultipliedAlpha);
    EXPECT_TRUE(d.customColorBlend);
    EXPECT_EQ(d.srcColorBlendFactor, rhi::BlendFactor::DstColor);
    EXPECT_EQ(d.dstColorBlendFactor, rhi::BlendFactor::SrcColor);
    EXPECT_TRUE(d.stencilTestEnable);
    EXPECT_EQ(d.stencilCompareOp, rhi::CompareOp::Equal);
    EXPECT_EQ(d.stencilPassOp, rhi::StencilOp::Replace);
    EXPECT_FALSE(d.colorWriteEnable);
    EXPECT_EQ(d.depthBiasConstant, 1.5f);
    EXPECT_EQ(d.depthBiasSlope, 2.5f);
    EXPECT_EQ(d.colorAttachmentCount, 3u);
    EXPECT_EQ(d.pushConstantRange.stages, uint32_t(rhi::kVertex | rhi::kFragment));
    EXPECT_EQ(d.pushConstantRange.offset, 4u);
    EXPECT_EQ(d.pushConstantRange.size, 64u);
    ASSERT_EQ(d.fragmentConstants.size(), 3u);
    EXPECT_EQ(d.fragmentConstants[2], 7u);
    EXPECT_EQ(d.vertex, nullptr);
    EXPECT_EQ(d.fragment, nullptr);
    EXPECT_EQ(d.pass.handle, 0u);
}

TEST(RhiPipelineState, DescribeUsesTheEnginesVertexWhenTheConfigNamesNone) {
    const rhi::GraphicsPipelineDesc d = Pipeline::describe(PipelineConfig{}, {});
    EXPECT_TRUE(d.useVertexInput);
    ASSERT_EQ(d.vertexBindings.size(), 1u);
    EXPECT_EQ(d.vertexBindings[0].stride, sizeof(Vertex));
    ASSERT_EQ(d.vertexAttributes.size(), 4u);
    EXPECT_EQ(d.vertexAttributes[3].offset, offsetof(Vertex, uv));
}

TEST(RhiPipelineState, DescribeTakesACustomVertexLayoutAsItIs) {
    PipelineConfig c;
    c.customVertexBindings = { { 0, 20, false }, { 1, 16, true } };
    c.customVertexAttributes = { { 0, 0, rhi::VertexFormat::Float2, 0 }, { 1, 0, rhi::VertexFormat::UByte4Norm, 8 }, { 2, 1, rhi::VertexFormat::Float4, 0 } };
    const rhi::GraphicsPipelineDesc d = Pipeline::describe(c, {});
    ASSERT_EQ(d.vertexBindings.size(), 2u);
    EXPECT_TRUE(d.vertexBindings[1].perInstance);
    ASSERT_EQ(d.vertexAttributes.size(), 3u);
    EXPECT_EQ(d.vertexAttributes[1].format, rhi::VertexFormat::UByte4Norm);
}

TEST(RhiPipelineState, DescribeLeavesTheVertexInputOutWhenTheShaderMakesItsOwnVertices) {
    PipelineConfig c;
    c.useVertexInput = false;
    const rhi::GraphicsPipelineDesc d = Pipeline::describe(c, {});
    EXPECT_FALSE(d.useVertexInput);
    EXPECT_TRUE(d.vertexBindings.empty());
    EXPECT_TRUE(d.vertexAttributes.empty());
}

TEST(RhiPipelineState, DescribePassesTheBindGroupLayoutsOn) {
    class Layout final : public rhi::BindGroupLayout {};
    Layout a, b;
    const rhi::BindGroupLayout* layouts[] = { &a, &b };
    const rhi::GraphicsPipelineDesc d = Pipeline::describe(PipelineConfig{}, layouts);
    ASSERT_EQ(d.bindGroupLayouts.size(), 2u);
    EXPECT_EQ(d.bindGroupLayouts[1], &b);
}
