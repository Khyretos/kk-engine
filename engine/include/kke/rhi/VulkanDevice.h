#pragma once

#include "kke/Buffer.h"
#include "kke/rhi/Device.h"
#include "kke/rhi/VulkanCommandList.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <unordered_map>
#include <utility>
#include <vector>

namespace kke {
class Texture;
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

// A bind group layout on Vulkan: a VkDescriptorSetLayout (what pipelines are made with).
class VulkanBindGroupLayout final : public BindGroupLayout {
public:
    VulkanBindGroupLayout(VkDevice device, std::span<const BindingLayout> bindings);
    ~VulkanBindGroupLayout() override;
    VulkanBindGroupLayout(const VulkanBindGroupLayout&) = delete;
    VulkanBindGroupLayout& operator=(const VulkanBindGroupLayout&) = delete;
    VkDescriptorSetLayout vk() const { return m_layout; }
    static VkDescriptorSetLayout vk(const BindGroupLayout& layout) { return static_cast<const VulkanBindGroupLayout&>(layout).m_layout; }
    // The binding's type (Texture when the layout has no such binding).
    BindingType type(uint32_t binding) const;
    // A layout that stands for a VkDescriptorSetLayout some module made itself (until it moves to the device):
    // the VkDescriptorSetLayout is not destroyed with it.
    static std::unique_ptr<VulkanBindGroupLayout> borrowed(VkDescriptorSetLayout layout);

private:
    explicit VulkanBindGroupLayout(VkDescriptorSetLayout borrowed);
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    bool m_owned = true;
    std::vector<BindingLayout> m_bindings;
};

// A shader on Vulkan: a VkShaderModule.
class VulkanShaderModule final : public ShaderModule {
public:
    VulkanShaderModule(VkDevice device, std::span<const uint32_t> spirv);
    ~VulkanShaderModule() override;
    VulkanShaderModule(const VulkanShaderModule&) = delete;
    VulkanShaderModule& operator=(const VulkanShaderModule&) = delete;
    VkShaderModule vk() const { return m_module; }
    static VkShaderModule vk(const ShaderModule& module) { return static_cast<const VulkanShaderModule&>(module).m_module; }

private:
    VkDevice m_device;
    VkShaderModule m_module = VK_NULL_HANDLE;
};

// A graphics pipeline on Vulkan: a VkPipeline with its VkPipelineLayout.
class VulkanGraphicsPipeline final : public GraphicsPipeline {
public:
    VulkanGraphicsPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout) : m_device(device), m_pipeline(pipeline), m_layout(layout) {}
    ~VulkanGraphicsPipeline() override;
    VulkanGraphicsPipeline(const VulkanGraphicsPipeline&) = delete;
    VulkanGraphicsPipeline& operator=(const VulkanGraphicsPipeline&) = delete;
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

private:
    VkDevice m_device;
    VkPipeline m_pipeline;
    VkPipelineLayout m_layout;
};

// A Device texture on Vulkan: an image with its view and sampler.
class VulkanTexture final : public Texture {
public:
    VulkanTexture(kke::VulkanDevice& device, const TextureDesc& desc, const void* pixels);
    ~VulkanTexture() override;
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    TextureRef ref() const override { return { VulkanCommandList::toHandle(m_view), VulkanCommandList::toHandle(m_sampler) }; }
    VkImageView view() const { return m_view; }
    VkSampler sampler() const { return m_sampler; }
    // Bytes of all of desc's levels.
    static size_t byteSize(const TextureDesc& desc);

private:
    kke::VulkanDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

// The Device on Vulkan.
class VulkanGpuDevice final : public Device {
public:
    explicit VulkanGpuDevice(kke::VulkanDevice& device) : m_device(device) {}
    ~VulkanGpuDevice() override;
    VulkanGpuDevice(const VulkanGpuDevice&) = delete;
    VulkanGpuDevice& operator=(const VulkanGpuDevice&) = delete;
    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initial = nullptr) override {
        return createBuffer(desc, initial, 0);
    }
    std::unique_ptr<BindGroupLayout> createBindGroupLayout(std::span<const BindingLayout> bindings) override;
    BindGroup createBindGroup(const BindGroupLayout& layout) override;
    void writeBindGroup(BindGroup group, const BindGroupLayout& layout, std::span<const BindingWrite> writes) override;
    void releaseBindGroup(BindGroup group) override;
    std::unique_ptr<Texture> createTexture(const TextureDesc& desc, const void* pixels) override;
    std::unique_ptr<ShaderModule> createShaderModule(std::span<const uint32_t> spirv) override;
    std::unique_ptr<GraphicsPipeline> createGraphicsPipeline(const GraphicsPipelineDesc& desc) override;
    // A texture as a binding sees it.
    static TextureRef texture(const kke::Texture& texture);
    static VkDescriptorSet set(BindGroup group) { return VulkanCommandList::fromHandle<VkDescriptorSet>(group.handle); }
    // With Vulkan usage bits the RHI does not name (ray-traced geometry, compute writes).
    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc, const void* initial, VkBufferUsageFlags extra);
    static VkBufferUsageFlags usageFlags(uint32_t usage);

private:
    kke::VulkanDevice& m_device;
    std::vector<VkDescriptorPool> m_pools; // bind groups come from the last; a full one gets a successor
    std::unordered_map<uint64_t, VkDescriptorPool> m_poolOf; // each live group's pool, to give it back
};

} // namespace kke::rhi
