#pragma once

#include <cstdint>
#include <span>

namespace kke {

class Pipeline;

// The render hardware interface (RHI, docs/plans/rhi.md): what drawing code
// says to the GPU without naming a graphics API, so the same module draws
// through Vulkan today and OpenGL ES 3 / WebGL2 next. R1a is the command
// list the graphics path draws with; the compute passes keep their
// VkCommandBuffer (the GL tier has no compute, so they never run there).
namespace rhi {

// What a descriptor set binds (Vulkan: the VkDescriptorSet; GL: the
// backend's table of textures and buffers), and a buffer, as handles the
// caller passes on without looking inside.
struct BindGroup {
    uint64_t handle = 0;
};
struct BufferRef {
    uint64_t handle = 0;
};

enum class IndexType : uint8_t { U16, U32 };

// Shader stages a push constant reaches (combine with |).
enum Stage : uint32_t { kVertex = 1, kFragment = 2 };

// The draws of one pass, recorded in order. Vulkan records them into a
// command buffer; GL plays them at once.
class CommandList {
public:
    virtual ~CommandList() = default;

    virtual void bindPipeline(const Pipeline& pipeline) = 0;
    // Sets `firstSet` .. firstSet + groups.size() - 1 of `pipeline`'s layout.
    virtual void bindGroups(const Pipeline& pipeline, uint32_t firstSet, std::span<const BindGroup> groups) = 0;
    virtual void pushConstants(const Pipeline& pipeline, uint32_t stages, uint32_t offset, uint32_t size, const void* data) = 0;
    virtual void bindVertexBuffer(uint32_t slot, BufferRef buffer, uint64_t offset) = 0;
    virtual void bindIndexBuffer(BufferRef buffer, uint64_t offset, IndexType type) = 0;
    virtual void draw(uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) = 0;
    virtual void drawIndexed(uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) = 0;
    virtual void setViewport(float x, float y, float width, float height) = 0;
    virtual void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;
};

} // namespace rhi
} // namespace kke
