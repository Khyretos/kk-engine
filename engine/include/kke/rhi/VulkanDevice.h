#pragma once

#include "kke/Buffer.h"
#include "kke/rhi/Device.h"
#include "kke/rhi/VulkanCommandList.h"

#include <volk.h>

#include <utility>

namespace kke {
class VulkanDevice;
}

namespace kke::rhi {

// A Device buffer on Vulkan: a kke::Buffer (VMA) underneath.
class VulkanBuffer final : public Buffer {
public:
    VulkanBuffer(kke::Buffer buffer, uint64_t size) : m_buffer(std::move(buffer)), m_size(size) {}
    BufferRef ref() const override { return VulkanCommandList::buffer(m_buffer.handle()); }
    uint64_t size() const override { return m_size; }
    void upload(const void* data, uint64_t bytes, uint64_t offset = 0) override { m_buffer.uploadAt(offset, data, bytes); }

    // For the Vulkan-only code still beside it (descriptor writes, compute, ray tracing).
    kke::Buffer& vk() { return m_buffer; }
    static VkBuffer handle(const Buffer& buffer) { return VulkanCommandList::fromHandle<VkBuffer>(buffer.ref().handle); }
    static VkDeviceAddress deviceAddress(Buffer& buffer) { return static_cast<VulkanBuffer&>(buffer).m_buffer.deviceAddress(); }

private:
    kke::Buffer m_buffer;
    uint64_t m_size;
};

// The Device on Vulkan.
class VulkanGpuDevice final : public Device {
public:
    explicit VulkanGpuDevice(kke::VulkanDevice& device) : m_device(device) {}
    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initial = nullptr) override {
        return createBuffer(desc, initial, 0);
    }
    // With Vulkan usage bits the RHI does not name (ray-traced geometry, compute writes).
    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initial, VkBufferUsageFlags extra);
    static VkBufferUsageFlags usageFlags(uint32_t usage);

private:
    kke::VulkanDevice& m_device;
};

} // namespace kke::rhi
