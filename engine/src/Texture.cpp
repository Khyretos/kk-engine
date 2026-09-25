#include "kke/Texture.h"
#include "kke/VulkanDevice.h"
#include "kke/VulkanCheck.h"
#include "kke/Buffer.h"
#include "kke/Log.h"

// No STB_IMAGE_IMPLEMENTATION here -- that's already defined exactly
// once, in RmlVulkanRenderInterface.cpp (confirmed via a real grep
// before adding it there). This just links against the same
// already-compiled implementation.
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace kke {

namespace {

// Mip chain on the CPU (OPTIMIZATION.md log #14). Each level is a 2x2
// box filter of the one above, averaged in *linear* light: averaging
// sRGB bytes directly darkens every mip (a black/white checker would fade
// to 0.5 sRGB = 0.21 linear instead of 0.5). Two small lookup tables keep
// it fast: 256 floats for decode, 4096 bytes for encode.
struct SrgbTables {
    float toLinear[256];
    uint8_t toSrgb[4096];
    SrgbTables() {
        for (int i = 0; i < 256; ++i) {
            float c = i / 255.0f;
            toLinear[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i < 4096; ++i) {
            float l = i / 4095.0f;
            float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
            toSrgb[i] = static_cast<uint8_t>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f));
        }
    }
};

void downsample(const uint8_t* src, uint32_t sw, uint32_t sh, uint8_t* dst, uint32_t dw, uint32_t dh) {
    static const SrgbTables t;
    for (uint32_t y = 0; y < dh; ++y) {
        uint32_t y0 = std::min(y * 2, sh - 1), y1 = std::min(y * 2 + 1, sh - 1);
        for (uint32_t x = 0; x < dw; ++x) {
            uint32_t x0 = std::min(x * 2, sw - 1), x1 = std::min(x * 2 + 1, sw - 1);
            const uint8_t* p[4] = { src + (y0 * sw + x0) * 4, src + (y0 * sw + x1) * 4, src + (y1 * sw + x0) * 4, src + (y1 * sw + x1) * 4 };
            uint8_t* o = dst + (y * dw + x) * 4;
            for (int c = 0; c < 3; ++c) {
                float l = (t.toLinear[p[0][c]] + t.toLinear[p[1][c]] + t.toLinear[p[2][c]] + t.toLinear[p[3][c]]) * 0.25f;
                o[c] = t.toSrgb[static_cast<int>(l * 4095.0f + 0.5f)];
            }
            o[3] = static_cast<uint8_t>((p[0][3] + p[1][3] + p[2][3] + p[3][3] + 2) / 4);
        }
    }
}

} // namespace

Texture::Texture(VulkanDevice& device, const std::string& filePath) : m_device(device) {
    int width = 0, height = 0, channels = 0;
    // Forcing 4 channels (RGBA), same reasoning as
    // RmlVulkanRenderInterface::LoadTexture: matches this class's own
    // fixed VK_FORMAT_R8G8B8A8_SRGB exactly.
    stbi_uc* pixels = stbi_load(filePath.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        throw std::runtime_error("kke::Texture: failed to load '" + filePath + "' (" +
                                  (stbi_failure_reason() ? stbi_failure_reason() : "unknown reason") + ")");
    }
    createFromPixels(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    stbi_image_free(pixels);
}

Texture::Texture(VulkanDevice& device, const uint8_t* rgbaPixels, uint32_t width, uint32_t height) : m_device(device) {
    createFromPixels(rgbaPixels, width, height);
}

void Texture::createFromPixels(const uint8_t* rgbaPixels, uint32_t width, uint32_t height) {
    // Whole mip chain in one staging buffer, one copy region per level.
    uint32_t mipLevels = 1;
    for (uint32_t m = std::max(width, height); m > 1; m >>= 1) ++mipLevels;
    std::vector<VkBufferImageCopy> regions;
    std::vector<uint8_t> chain;
    {
        uint32_t w = width, h = height;
        chain.assign(rgbaPixels, rgbaPixels + static_cast<size_t>(w) * h * 4);
        size_t offset = 0;
        for (uint32_t level = 0; level < mipLevels; ++level) {
            VkBufferImageCopy r{};
            r.bufferOffset = offset;
            r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1 };
            r.imageExtent = { w, h, 1 };
            regions.push_back(r);
            if (level + 1 == mipLevels) break;
            uint32_t nw = std::max(1u, w / 2), nh = std::max(1u, h / 2);
            size_t next = offset + static_cast<size_t>(w) * h * 4;
            chain.resize(next + static_cast<size_t>(nw) * nh * 4);
            downsample(chain.data() + offset, w, h, chain.data() + next, nw, nh);
            offset = next;
            w = nw;
            h = nh;
        }
    }
    VkDeviceSize dataSize = chain.size();

    // Same real staging-buffer-upload-then-GPU-only-image pattern
    // already proven in RmlVulkanRenderInterface::createTextureFromPixels
    // — not copied verbatim (this class is deliberately independent,
    // see its own header comment), but the same well-understood Vulkan
    // sequence: staging buffer, GPU image, transition to transfer-dst,
    // copy, transition to shader-read.
    Buffer staging(m_device, dataSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.upload(chain.data(), dataSize);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = { width, height, 1 };
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(m_device.allocator(), &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr));

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
    toTransferDst.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
    toTransferDst.srcAccessMask = 0;
    toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &toTransferDst);

    vkCmdCopyBufferToImage(cmd, staging.handle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    VkImageMemoryBarrier toShaderRead{};
    toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShaderRead.image = m_image;
    toShaderRead.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
    toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                          0, 0, nullptr, 0, nullptr, 1, &toShaderRead);

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
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
    VK_CHECK(vkCreateImageView(m_device.device(), &viewInfo, nullptr, &m_imageView));

    // LINEAR + REPEAT -- real defaults for a material texture, not
    // RmlVulkanRenderInterface's own CLAMP_TO_BORDER (appropriate
    // there for UI images that should never tile; wrong here, where a
    // texture mapped across a mesh's UVs commonly needs to repeat).
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    // Trilinear: smooth between mips. No anisotropy yet (it's an optional
    // device feature; worth enabling where present, see OPTIMIZATION.md).
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    VK_CHECK(vkCreateSampler(m_device.device(), &samplerInfo, nullptr, &m_sampler));
}

Texture::~Texture() {
    VkDevice dev = m_device.device();
    if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
    if (m_imageView) vkDestroyImageView(dev, m_imageView, nullptr);
    if (m_image) vmaDestroyImage(m_device.allocator(), m_image, m_allocation);
}

} // namespace kke
