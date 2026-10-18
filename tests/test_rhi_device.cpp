// The RHI's device (docs/plans/rhi.md, R1c): buffer usage maps to Vulkan's
// bits, and the interface can be implemented without Vulkan (as the
// OpenGL ES 3 backend will), a buffer being bytes a draw names by ref().

#include "kke/rhi/Device.h"
#include "kke/rhi/VulkanDevice.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace kke;

namespace {

// A backend that keeps each buffer in CPU memory, the way a test (or a
// GL backend before glBufferData) sees them.
class MemoryBuffer final : public rhi::Buffer {
public:
    MemoryBuffer(uint64_t id, const rhi::BufferDesc& desc) : bytes(desc.size), m_id(id) {}
    rhi::BufferRef ref() const override { return { m_id }; }
    uint64_t size() const override { return bytes.size(); }
    void upload(const void* data, uint64_t n, uint64_t offset = 0) override { std::memcpy(bytes.data() + offset, data, n); }
    std::vector<unsigned char> bytes;

private:
    uint64_t m_id;
};

class MemoryDevice final : public rhi::Device {
public:
    std::unique_ptr<rhi::Buffer> createBuffer(const rhi::BufferDesc& desc, const void* initial = nullptr) override {
        auto b = std::make_unique<MemoryBuffer>(++m_next, desc);
        if (initial) b->upload(initial, desc.size);
        return b;
    }

private:
    uint64_t m_next = 0;
};

} // namespace

TEST(RhiDevice, UsageBecomesVulkanBits) {
    EXPECT_EQ(rhi::VulkanGpuDevice::usageFlags(rhi::kVertexBuffer), VkBufferUsageFlags(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    EXPECT_EQ(rhi::VulkanGpuDevice::usageFlags(rhi::kIndexBuffer), VkBufferUsageFlags(VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    EXPECT_EQ(rhi::VulkanGpuDevice::usageFlags(rhi::kUniformBuffer | rhi::kStorageBuffer),
              VkBufferUsageFlags(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    EXPECT_EQ(rhi::VulkanGpuDevice::usageFlags(0), VkBufferUsageFlags(0));
}

TEST(RhiDevice, ABackendKeepsTheBytesAndNamesEachBuffer) {
    MemoryDevice device;
    rhi::Device& d = device;
    const uint32_t indices[3] = { 0, 1, 2 };
    auto ib = d.createBuffer({ sizeof(indices), rhi::kIndexBuffer, rhi::Memory::Device }, indices);
    auto vb = d.createBuffer({ 16, rhi::kVertexBuffer, rhi::Memory::Upload });
    const float x = 2.5f;
    vb->upload(&x, sizeof(x), 8);
    EXPECT_NE(ib->ref().handle, vb->ref().handle);
    EXPECT_EQ(ib->size(), sizeof(indices));
    EXPECT_EQ(std::memcmp(static_cast<MemoryBuffer&>(*ib).bytes.data(), indices, sizeof(indices)), 0);
    float back = 0.0f;
    std::memcpy(&back, static_cast<MemoryBuffer&>(*vb).bytes.data() + 8, sizeof(back));
    EXPECT_EQ(back, 2.5f);
}
