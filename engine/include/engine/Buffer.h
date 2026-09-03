#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <cstddef>

namespace engine {

class VulkanDevice;

// Thin RAII wrapper around a VkBuffer + VMA allocation.
// This is the piece that keeps us from hand-rolling VkDeviceMemory bookkeeping.
class Buffer {
public:
    Buffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;

    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    void upload(const void* data, VkDeviceSize size);

    VkBuffer handle() const { return m_buffer; }

    // Creates a device-local buffer and fills it via a staging buffer + copy command.
    static Buffer createDeviceLocal(VulkanDevice& device, const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

private:
    VulkanDevice& m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
};

} // namespace engine
