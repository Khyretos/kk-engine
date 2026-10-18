#pragma once

#include "kke/rhi/CommandList.h"

#include <volk.h>

#include <array>
#include <type_traits>

namespace kke::rhi {

// The command list on Vulkan (docs/plans/rhi.md): each call is the one
// vkCmd* it stands for, into the frame's command buffer.
class VulkanCommandList final : public CommandList {
public:
    explicit VulkanCommandList(VkCommandBuffer cmd = VK_NULL_HANDLE) : m_cmd(cmd) {}
    void reset(VkCommandBuffer cmd) { m_cmd = cmd; }
    VkCommandBuffer commandBuffer() const { return m_cmd; }

    // Vulkan's handles as RHI handles (a pointer on 64-bit builds, an integer on 32-bit ones).
    template <typename H> static uint64_t toHandle(H h) {
        if constexpr (std::is_pointer_v<H>) return reinterpret_cast<uint64_t>(h);
        else return static_cast<uint64_t>(h);
    }
    template <typename H> static H fromHandle(uint64_t h) {
        if constexpr (std::is_pointer_v<H>) return reinterpret_cast<H>(h);
        else return static_cast<H>(h);
    }
    static BindGroup group(VkDescriptorSet set) { return BindGroup{ toHandle(set) }; }
    static BufferRef buffer(VkBuffer buffer) { return BufferRef{ toHandle(buffer) }; }
    // `count` descriptor sets as bind groups, for bindGroups() (at most 8).
    struct Groups {
        std::array<BindGroup, 8> groups{};
        uint32_t count = 0;
        operator std::span<const BindGroup>() const { return { groups.data(), count }; }
    };
    static Groups groups(const VkDescriptorSet* sets, uint32_t count) {
        Groups g;
        g.count = count < 8 ? count : 8;
        for (uint32_t i = 0; i < g.count; ++i) g.groups[i] = group(sets[i]);
        return g;
    }

    void bindPipeline(const Pipeline& pipeline) override;
    void bindGroups(const Pipeline& pipeline, uint32_t firstSet, std::span<const BindGroup> groups) override;
    void pushConstants(const Pipeline& pipeline, uint32_t stages, uint32_t offset, uint32_t size, const void* data) override;
    void bindVertexBuffer(uint32_t slot, BufferRef buffer, uint64_t offset) override;
    void bindIndexBuffer(BufferRef buffer, uint64_t offset, IndexType type) override;
    void draw(uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) override;
    void drawIndexed(uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) override;
    void setViewport(float x, float y, float width, float height) override;
    void setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) override;

private:
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;
};

} // namespace kke::rhi
