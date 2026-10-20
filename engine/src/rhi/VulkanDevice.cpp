#include "kke/rhi/VulkanDevice.h"

#include "kke/Texture.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

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

// R1h stub: the Coder's run replaces it.
std::unique_ptr<VulkanBindGroupLayout> VulkanBindGroupLayout::borrowed(VkDescriptorSetLayout) { throw std::logic_error("R1h: borrowed"); }
VulkanBindGroupLayout::VulkanBindGroupLayout(VkDescriptorSetLayout) {}

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
                          w.texture.computeWritten ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
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

TextureRef VulkanGpuDevice::texture(const kke::Texture& texture) { return texture.ref(); }

std::unique_ptr<Texture> VulkanGpuDevice::createTexture(const TextureDesc& desc, const void* pixels) {
    return std::make_unique<VulkanTexture>(m_device, desc, pixels);
}

namespace {

VkFormat formatOf(TextureFormat format) {
    switch (format) {
    case TextureFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::RGBA8Srgb: break;
    }
    return VK_FORMAT_R8G8B8A8_SRGB;
}

} // namespace

size_t VulkanTexture::byteSize(const TextureDesc& desc) {
    const size_t texel = desc.format == TextureFormat::RGBA16F ? 8 : 4;
    size_t bytes = 0;
    for (uint32_t level = 0; level < desc.levels; ++level)
        bytes += static_cast<size_t>(std::max(1u, desc.width >> level)) * std::max(1u, desc.height >> level) * texel;
    return bytes;
}

VulkanTexture::VulkanTexture(kke::VulkanDevice& device, const TextureDesc& desc, const void* pixels) : m_device(device) {
    const VkFormat format = formatOf(desc.format);
    const size_t texel = desc.format == TextureFormat::RGBA16F ? 8 : 4;
    // Every level in one staging buffer, one copy region per level.
    std::vector<VkBufferImageCopy> regions;
    size_t offset = 0;
    for (uint32_t level = 0; level < desc.levels; ++level) {
        const uint32_t w = std::max(1u, desc.width >> level), h = std::max(1u, desc.height >> level);
        VkBufferImageCopy r{};
        r.bufferOffset = offset;
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
        r.imageExtent = { w, h, 1 };
        regions.push_back(r);
        offset += static_cast<size_t>(w) * h * texel;
    }
    kke::Buffer staging(m_device, offset, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.upload(pixels, offset);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { desc.width, desc.height, 1 };
    imageInfo.mipLevels = desc.levels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr));

    // Staging buffer to image: transfer-dst, copy, shader-read, waiting for the queue (load time).
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = m_device.commandPool();
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_device.device(), &cmdAllocInfo, &cmd));
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    VkImageMemoryBarrier toTransferDst{};
    toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toTransferDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransferDst.image = m_image;
    toTransferDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.levels, 0, 1 };
    toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransferDst);
    vkCmdCopyBufferToImage(cmd, staging.handle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()),
                           regions.data());
    VkImageMemoryBarrier toShaderRead = toTransferDst;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShaderRead);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(m_device.graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(m_device.graphicsQueue()));
    vkFreeCommandBuffers(m_device.device(), m_device.commandPool(), 1, &cmd);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.levels, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &viewInfo, nullptr, &m_view));

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = desc.blendLevels ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = samplerInfo.addressModeW =
        desc.wrap == Wrap::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    // 8x anisotropy is near free on any GPU with the feature; 16x adds little more.
    if (desc.anisotropic && m_device.maxSamplerAnisotropy() > 1.0f) {
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = std::min(8.0f, m_device.maxSamplerAnisotropy());
    }
    samplerInfo.maxLod = static_cast<float>(desc.levels - 1);
    VK_CHECK(vkCreateSampler(m_device.device(), &samplerInfo, nullptr, &m_sampler));
}

VulkanTexture::~VulkanTexture() {
    VkDevice dev = m_device.device();
    if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
    if (m_view) vkDestroyImageView(dev, m_view, nullptr);
    if (m_image) vmaDestroyImage(m_device.allocator(), m_image, m_allocation);
}

} // namespace kke::rhi
