#include "kke/Buffer.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"

#include <cstring>

namespace kke {

Buffer::Buffer(VulkanDevice& device, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    : m_device(device), m_size(size) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memoryUsage;
    if (memoryUsage == VMA_MEMORY_USAGE_CPU_ONLY || memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
    }

    VK_CHECK(vmaCreateBuffer(device.allocator(), &bufferInfo, &allocInfo, &m_buffer, &m_allocation, nullptr));
}

Buffer::~Buffer() {
    if (m_buffer) {
        vmaDestroyBuffer(m_device.allocator(), m_buffer, m_allocation);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_device(other.m_device), m_buffer(other.m_buffer), m_allocation(other.m_allocation), m_size(other.m_size) {
    other.m_buffer = VK_NULL_HANDLE;
    other.m_allocation = VK_NULL_HANDLE;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        if (m_buffer) {
            vmaDestroyBuffer(m_device.allocator(), m_buffer, m_allocation);
        }
        m_buffer = other.m_buffer;
        m_allocation = other.m_allocation;
        m_size = other.m_size;
        other.m_buffer = VK_NULL_HANDLE;
        other.m_allocation = VK_NULL_HANDLE;
    }
    return *this;
}

void Buffer::upload(const void* data, VkDeviceSize size) {
    void* mapped = nullptr;
    VK_CHECK(vmaMapMemory(m_device.allocator(), m_allocation, &mapped));
    std::memcpy(mapped, data, static_cast<size_t>(size));
    vmaUnmapMemory(m_device.allocator(), m_allocation);
}

Buffer Buffer::createDeviceLocal(VulkanDevice& device, const void* data, VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer staging(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.upload(data, size);

    Buffer deviceLocal(device, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = device.commandPool();
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device.device(), &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(cmd, staging.handle(), deviceLocal.handle(), 1, &copyRegion);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(device.graphicsQueue()));

    vkFreeCommandBuffers(device.device(), device.commandPool(), 1, &cmd);

    return deviceLocal;
}

} // namespace kke
