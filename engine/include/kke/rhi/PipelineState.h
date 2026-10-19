#pragma once

#include "kke/rhi/CommandList.h" // Stage

#include <cstdint>

namespace kke::rhi {

// A graphics pipeline's fixed state in the RHI's own words (docs/plans/rhi.md, R1f):
// what kke::PipelineConfig says, so a backend other than Vulkan can read it.
enum class Topology : uint8_t { TriangleList, TriangleStrip, LineList, PointList };
enum class CullMode : uint8_t { None, Back, Front };
enum class FrontFace : uint8_t { Clockwise, CounterClockwise };
enum class BlendFactor : uint8_t { Zero, One, SrcAlpha, OneMinusSrcAlpha, SrcColor, OneMinusSrcColor, DstColor, OneMinusDstColor };
enum class CompareOp : uint8_t { Never, Less, Equal, LessOrEqual, Greater, NotEqual, GreaterOrEqual, Always };
enum class StencilOp : uint8_t { Keep, Zero, Replace, IncrementAndClamp, DecrementAndClamp };

// The push constants a pipeline takes: the stages (Stage bits) and the bytes.
struct PushRange {
    uint32_t stages = 0;
    uint32_t offset = 0;
    uint32_t size = 0; // 0: no push constants
};

// A vertex buffer slot and the attributes read from it.
enum class VertexFormat : uint8_t { Float, Float2, Float3, Float4, UByte4Norm };
struct VertexBinding {
    uint32_t binding = 0;
    uint32_t stride = 0;
    bool perInstance = false; // advances per instance, not per vertex
};
struct VertexAttribute {
    uint32_t location = 0;
    uint32_t binding = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
};

} // namespace kke::rhi
