// The RHI's device (docs/plans/rhi.md, R1c and R1d): buffer usage maps to
// Vulkan's bits, and the interface can be implemented without Vulkan (as the
// OpenGL ES 3 backend will): a buffer being bytes a draw names by ref(), a
// bind group a table of what each binding points at.

#include "kke/rhi/Device.h"
#include "kke/rhi/VulkanDevice.h"

#include <gtest/gtest.h>

#include <cstring>
#include <map>
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

class MemoryLayout final : public rhi::BindGroupLayout {
public:
    explicit MemoryLayout(std::span<const rhi::BindingLayout> b) : bindings(b.begin(), b.end()) {}
    std::vector<rhi::BindingLayout> bindings;
};

class MemoryTexture final : public rhi::Texture {
public:
    MemoryTexture(uint64_t id, const rhi::TextureDesc& d, const void* pixels, size_t bytes)
        : desc(d), data(static_cast<const unsigned char*>(pixels), static_cast<const unsigned char*>(pixels) + bytes), m_id(id) {}
    rhi::TextureRef ref() const override { return { m_id, m_id }; }
    rhi::TextureDesc desc;
    std::vector<unsigned char> data;

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
    std::unique_ptr<rhi::BindGroupLayout> createBindGroupLayout(std::span<const rhi::BindingLayout> bindings) override {
        return std::make_unique<MemoryLayout>(bindings);
    }
    rhi::BindGroup createBindGroup(const rhi::BindGroupLayout&) override {
        const rhi::BindGroup g{ ++m_next };
        groups[g.handle];
        return g;
    }
    void writeBindGroup(rhi::BindGroup group, const rhi::BindGroupLayout&, std::span<const rhi::BindingWrite> writes) override {
        for (const rhi::BindingWrite& w : writes) groups[group.handle][w.binding] = w;
    }
    void releaseBindGroup(rhi::BindGroup group) override { groups.erase(group.handle); }
    std::unique_ptr<rhi::Texture> createTexture(const rhi::TextureDesc& desc, const void* pixels) override {
        return std::make_unique<MemoryTexture>(++m_next, desc, pixels, rhi::VulkanTexture::byteSize(desc));
    }
    std::map<uint64_t, std::map<uint32_t, rhi::BindingWrite>> groups; // what each live group's bindings point at

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

TEST(RhiDevice, ABindGroupPointsEachBindingAtItsResource) {
    MemoryDevice device;
    rhi::Device& d = device;
    const rhi::BindingLayout bindings[2] = { { 0, rhi::BindingType::StorageBuffer, rhi::kVertex },
                                             { 1, rhi::BindingType::Texture, rhi::kFragment } };
    auto layout = d.createBindGroupLayout(bindings);
    EXPECT_EQ(static_cast<MemoryLayout&>(*layout).bindings.size(), 2u);
    auto buffer = d.createBuffer({ 64, rhi::kStorageBuffer, rhi::Memory::Upload });
    const rhi::BindGroup a = d.createBindGroup(*layout), b = d.createBindGroup(*layout);
    EXPECT_NE(a.handle, 0u);
    EXPECT_NE(a.handle, b.handle);
    const rhi::BindingWrite writes[2] = { { .binding = 0, .buffer = buffer->ref(), .offset = 16 },
                                          { .binding = 1, .texture = { 7, 9 } } };
    d.writeBindGroup(a, *layout, writes);
    EXPECT_EQ(device.groups[a.handle][0].buffer.handle, buffer->ref().handle);
    EXPECT_EQ(device.groups[a.handle][0].offset, 16u);
    EXPECT_EQ(device.groups[a.handle][1].texture.sampler, 9u);
    EXPECT_TRUE(device.groups[b.handle].empty());
    d.releaseBindGroup(a);
    EXPECT_EQ(device.groups.count(a.handle), 0u);
}

TEST(RhiDevice, StagesBecomeVulkanBits) {
    EXPECT_EQ(rhi::VulkanCommandList::stagesOf(rhi::kVertex | rhi::kCompute),
              VkShaderStageFlags(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT));
    EXPECT_EQ(rhi::VulkanCommandList::stagesOf(rhi::kFragment), VkShaderStageFlags(VK_SHADER_STAGE_FRAGMENT_BIT));
}

TEST(RhiDevice, ATextureKeepsEveryLevel) {
    // 4x2 RGBA8 with its mips: 4x2, 2x1 and 1x1 texels, 4 bytes each.
    rhi::TextureDesc desc;
    desc.width = 4;
    desc.height = 2;
    desc.levels = 3;
    EXPECT_EQ(rhi::VulkanTexture::byteSize(desc), (8u + 2u + 1u) * 4u);
    desc.format = rhi::TextureFormat::RGBA16F;
    EXPECT_EQ(rhi::VulkanTexture::byteSize(desc), (8u + 2u + 1u) * 8u);
    desc.format = rhi::TextureFormat::RGBA8;
    std::vector<unsigned char> pixels(rhi::VulkanTexture::byteSize(desc));
    for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<unsigned char>(i);
    MemoryDevice device;
    auto texture = static_cast<rhi::Device&>(device).createTexture(desc, pixels.data());
    const auto& made = static_cast<MemoryTexture&>(*texture);
    EXPECT_EQ(made.data, pixels);
    EXPECT_EQ(made.desc.levels, 3u);
    EXPECT_NE(texture->ref().view, 0u);
}

TEST(RhiDevice, ATextureBindingSaysWhenComputeWritesIt) {
    MemoryDevice device;
    rhi::Device& d = device;
    const rhi::BindingLayout binding{ 0, rhi::BindingType::Texture, rhi::kFragment | rhi::kCompute };
    auto layout = d.createBindGroupLayout({ &binding, 1 });
    const rhi::BindGroup g = d.createBindGroup(*layout);
    const rhi::BindingWrite write{ .binding = 0, .texture = { 3, 4, true } };
    d.writeBindGroup(g, *layout, { &write, 1 });
    EXPECT_TRUE(device.groups[g.handle][0].texture.computeWritten);
    EXPECT_FALSE(rhi::TextureRef{}.computeWritten);
}
