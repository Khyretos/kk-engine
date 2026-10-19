#include "kke/rhi/VulkanDevice.h"

#include "kke/Texture.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <array>
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

namespace {

VkDescriptorType descriptorType(BindingType type) {
    switch (type) {
    case BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::Texture: break;
    }
    return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
}

} // namespace

VulkanBindGroupLayout::VulkanBindGroupLayout(VkDevice device, std::span<const BindingLayout> bindings)
    : m_device(device), m_bindings(bindings.begin(), bindings.end()) {
    std::vector<VkDescriptorSetLayoutBinding> vk(bindings.size());
    for (size_t i = 0; i < bindings.size(); ++i)
        vk[i] = { bindings[i].binding, descriptorType(bindings[i].type), 1, VulkanCommandList::stagesOf(bindings[i].stages), nullptr };
    VkDescriptorSetLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.bindingCount = static_cast<uint32_t>(vk.size());
    info.pBindings = vk.data();
    VK_CHECK(vkCreateDescriptorSetLayout(m_device, &info, nullptr, &m_layout));
}

VulkanBindGroupLayout::~VulkanBindGroupLayout() { vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr); }

BindingType VulkanBindGroupLayout::type(uint32_t binding) const {
    for (const BindingLayout& b : m_bindings)
        if (b.binding == binding) return b.type;
    return BindingType::Texture;
}

VulkanGpuDevice::~VulkanGpuDevice() {
    for (VkDescriptorPool pool : m_pools) vkDestroyDescriptorPool(m_device.device(), pool, nullptr);
}

std::unique_ptr<BindGroupLayout> VulkanGpuDevice::createBindGroupLayout(std::span<const BindingLayout> bindings) {
    return std::make_unique<VulkanBindGroupLayout>(m_device.device(), bindings);
}

BindGroup VulkanGpuDevice::createBindGroup(const BindGroupLayout& layout) {
    const VkDescriptorSetLayout vkLayout = VulkanBindGroupLayout::vk(layout);
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (attempt == 1 || m_pools.empty()) {
            // Room for 256 groups of a few bindings each; a full pool gets a successor, so no
            // module sizes a pool of its own (each did before R1d).
            constexpr uint32_t kSets = 256;
            const std::array<VkDescriptorPoolSize, 3> sizes{ { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kSets },
                                                               { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 * kSets },
                                                               { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kSets } } };
            VkDescriptorPoolCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // releaseBindGroup
            info.maxSets = kSets;
            info.poolSizeCount = static_cast<uint32_t>(sizes.size());
            info.pPoolSizes = sizes.data();
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(m_device.device(), &info, nullptr, &pool) != VK_SUCCESS) return {};
            m_pools.push_back(pool);
        }
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = m_pools.back();
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &vkLayout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        if (vkAllocateDescriptorSets(m_device.device(), &alloc, &set) == VK_SUCCESS) {
            const BindGroup group = VulkanCommandList::group(set);
            m_poolOf[group.handle] = m_pools.back();
            return group;
        }
    }
    return {};
}

void VulkanGpuDevice::writeBindGroup(BindGroup group, const BindGroupLayout& layout, std::span<const BindingWrite> writes) {
    const auto& vkLayout = static_cast<const VulkanBindGroupLayout&>(layout);
    std::vector<VkDescriptorBufferInfo> buffers(writes.size());
    std::vector<VkDescriptorImageInfo> images(writes.size());
    std::vector<VkWriteDescriptorSet> vk(writes.size());
    for (size_t i = 0; i < writes.size(); ++i) {
        const BindingWrite& w = writes[i];
        const BindingType type = vkLayout.type(w.binding);
        vk[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        vk[i].dstSet = set(group);
        vk[i].dstBinding = w.binding;
        vk[i].descriptorCount = 1;
        vk[i].descriptorType = descriptorType(type);
        if (type == BindingType::Texture) {
            images[i] = { VulkanCommandList::fromHandle<VkSampler>(w.texture.sampler), VulkanCommandList::fromHandle<VkImageView>(w.texture.view),
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
            vk[i].pImageInfo = &images[i];
        } else {
            buffers[i] = { VulkanCommandList::fromHandle<VkBuffer>(w.buffer.handle), w.offset, w.range ? w.range : VK_WHOLE_SIZE };
            vk[i].pBufferInfo = &buffers[i];
        }
    }
    vkUpdateDescriptorSets(m_device.device(), static_cast<uint32_t>(vk.size()), vk.data(), 0, nullptr);
}

void VulkanGpuDevice::releaseBindGroup(BindGroup group) {
    auto it = m_poolOf.find(group.handle);
    if (it == m_poolOf.end()) return;
    VkDescriptorSet vkSet = set(group);
    vkFreeDescriptorSets(m_device.device(), it->second, 1, &vkSet);
    m_poolOf.erase(it);
}

TextureRef VulkanGpuDevice::texture(const kke::Texture& texture) {
    return { VulkanCommandList::toHandle(texture.imageView()), VulkanCommandList::toHandle(texture.sampler()) };
}

} // namespace kke::rhi
