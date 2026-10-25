#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <cstddef>

namespace kke {

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
    // Writes `size` bytes at `offset` of a host-visible buffer.
    void uploadAt(VkDeviceSize offset, const void* data, VkDeviceSize size);
    // Reads `size` bytes back from a host-visible buffer (e.g. a
    // GPU_TO_CPU readback target), after the GPU is done writing it.
    void download(void* out, VkDeviceSize size) const;
    // The same, `size` bytes from `offset`.
    void downloadAt(VkDeviceSize offset, void* out, VkDeviceSize size) const;

    VkBuffer handle() const { return m_buffer; }
    // Its GPU address (needs VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT and a
    // device with buffer device addresses, VulkanDevice::rayQuery()).
    VkDeviceAddress deviceAddress() const;

    // Creates a device-local buffer and fills it via a staging buffer + copy command.
    static Buffer createDeviceLocal(VulkanDevice& device, const void* data, VkDeviceSize size, VkBufferUsageFlags usage);

private:
    VulkanDevice& m_device;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
};

} // namespace kke
