// The RHI's pipeline state (docs/plans/rhi.md, R1f): PipelineConfig and the
// engine's vertex say what they need without Vulkan's types, so another
// backend (OpenGL ES 3 / WebGL2) can read them.

#include "kke/Mesh.h"
#include "kke/Pipeline.h"

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
