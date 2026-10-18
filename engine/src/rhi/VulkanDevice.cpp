#include "kke/rhi/VulkanDevice.h"

#include <memory>
#include <utility>

namespace kke::rhi {

VkBufferUsageFlags VulkanGpuDevice::usageFlags(uint32_t usage) {
    VkBufferUsageFlags flags = 0;
    if (usage & kVertexBuffer) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (usage & kIndexBuffer) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (usage & kUniformBuffer) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (usage & kStorageBuffer) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    return flags;
}

std::unique_ptr<Buffer> VulkanGpuDevice::createBuffer(const BufferDesc& desc, const void* initial, VkBufferUsageFlags extra) {
    const VkBufferUsageFlags flags = usageFlags(desc.usage) | extra;
    if (desc.memory == Memory::Device && initial)
        return std::make_unique<VulkanBuffer>(kke::Buffer::createDeviceLocal(m_device, initial, desc.size, flags), desc.size);
    kke::Buffer buffer(m_device, desc.size, flags,
                       desc.memory == Memory::Upload ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_GPU_ONLY);
    if (initial) buffer.upload(initial, desc.size);
    return std::make_unique<VulkanBuffer>(std::move(buffer), desc.size);
}

} // namespace kke::rhi
