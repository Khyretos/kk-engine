#include "kke/rhi/VulkanCommandList.h"

#include "kke/Pipeline.h"

#include <algorithm>
#include <array>

namespace kke::rhi {

VkShaderStageFlags VulkanCommandList::stagesOf(uint32_t stages) {
    VkShaderStageFlags out = 0;
    if (stages & kVertex) out |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stages & kFragment) out |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stages & kCompute) out |= VK_SHADER_STAGE_COMPUTE_BIT;
    return out;
}

void VulkanCommandList::bindPipeline(const Pipeline& pipeline) { pipeline.bind(m_cmd); }

void VulkanCommandList::bindGroups(const Pipeline& pipeline, uint32_t firstSet, std::span<const BindGroup> groups) {
    std::array<VkDescriptorSet, 8> sets{};
    const uint32_t n = static_cast<uint32_t>(std::min(groups.size(), sets.size()));
    for (uint32_t i = 0; i < n; ++i) sets[i] = fromHandle<VkDescriptorSet>(groups[i].handle);
    vkCmdBindDescriptorSets(m_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), firstSet, n, sets.data(), 0, nullptr);
}

void VulkanCommandList::pushConstants(const Pipeline& pipeline, uint32_t stages, uint32_t offset, uint32_t size, const void* data) {
    vkCmdPushConstants(m_cmd, pipeline.layout(), stagesOf(stages), offset, size, data);
}

void VulkanCommandList::bindVertexBuffer(uint32_t slot, BufferRef buffer, uint64_t offset) {
    const VkBuffer b = fromHandle<VkBuffer>(buffer.handle);
    const VkDeviceSize o = offset;
    vkCmdBindVertexBuffers(m_cmd, slot, 1, &b, &o);
}

void VulkanCommandList::bindIndexBuffer(BufferRef buffer, uint64_t offset, IndexType type) {
    vkCmdBindIndexBuffer(m_cmd, fromHandle<VkBuffer>(buffer.handle), offset, type == IndexType::U16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
}

void VulkanCommandList::draw(uint32_t vertices, uint32_t instances, uint32_t firstVertex, uint32_t firstInstance) {
    vkCmdDraw(m_cmd, vertices, instances, firstVertex, firstInstance);
}

void VulkanCommandList::drawIndexed(uint32_t indices, uint32_t instances, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    vkCmdDrawIndexed(m_cmd, indices, instances, firstIndex, vertexOffset, firstInstance);
}

void VulkanCommandList::setViewport(float x, float y, float width, float height) {
    const VkViewport v{ x, y, width, height, 0.0f, 1.0f };
    vkCmdSetViewport(m_cmd, 0, 1, &v);
}

void VulkanCommandList::setScissor(int32_t x, int32_t y, uint32_t width, uint32_t height) {
    const VkRect2D r{ { x, y }, { width, height } };
    vkCmdSetScissor(m_cmd, 0, 1, &r);
}

} // namespace kke::rhi
