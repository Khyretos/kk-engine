#include "DecalTextures.h"

#include "kke/Buffer.h"
#include "kke/CookedFile.h"
#include "kke/Decals.h"
#include "kke/Log.h"
#include "kke/TextureMips.h"
#include "kke/VulkanCheck.h"
#include "kke/VulkanDevice.h"

// The implementation is compiled once, in RmlVulkanRenderInterface.cpp.
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// One-shot commands, waited for (loading a decal texture is rare: at the start and the first time a name is used).
template <typename Record> void submitNow(VulkanDevice& device, Record record) {
    VkCommandBufferAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandPool = device.commandPool();
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device.device(), &cai, &cmd));
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    record(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(device.graphicsQueue(), 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(device.graphicsQueue()));
    vkFreeCommandBuffers(device.device(), device.commandPool(), 1, &cmd);
}

// `src` (w x h RGBA8) resampled to `size` square, bilinear.
std::vector<uint8_t> resample(const uint8_t* src, int w, int h, uint32_t size) {
    std::vector<uint8_t> out(static_cast<size_t>(size) * size * 4);
    for (uint32_t j = 0; j < size; ++j)
        for (uint32_t i = 0; i < size; ++i) {
            const float x = (static_cast<float>(i) + 0.5f) * static_cast<float>(w) / static_cast<float>(size) - 0.5f;
            const float y = (static_cast<float>(j) + 0.5f) * static_cast<float>(h) / static_cast<float>(size) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, w - 1), y0 = std::clamp(static_cast<int>(std::floor(y)), 0, h - 1);
            const int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const float fx = std::clamp(x - static_cast<float>(x0), 0.0f, 1.0f), fy = std::clamp(y - static_cast<float>(y0), 0.0f, 1.0f);
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int xx, int yy) { return static_cast<float>(src[(static_cast<size_t>(yy) * static_cast<size_t>(w) + static_cast<size_t>(xx)) * 4 + static_cast<size_t>(c)]); };
                const float v = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
                out[(static_cast<size_t>(j) * size + i) * 4 + static_cast<size_t>(c)] = static_cast<uint8_t>(std::lround(v));
            }
        }
    return out;
}

} // namespace

DecalTextures::DecalTextures(VulkanDevice& device) : m_device(device) {
    VkDevice dev = device.device();
    m_levels = static_cast<uint32_t>(std::log2(static_cast<float>(kSize))) + 1;
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.extent = { kSize, kSize, 1 };
    ii.mipLevels = m_levels;
    ii.arrayLayers = kLayers;
    ii.format = VK_FORMAT_R8G8B8A8_SRGB;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;
    VK_CHECK(vmaCreateImage(device.allocator(), &ii, &ai, &m_image, &m_alloc, nullptr));
    // Every layer cleared to transparent, all ready to be read.
    submitNow(device, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_levels, 0, kLayers };
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        const VkClearColorValue clear{ { 0.0f, 0.0f, 0.0f, 0.0f } };
        vkCmdClearColorImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &b.subresourceRange);
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    });
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = m_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vi.format = ii.format;
    vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_levels, 0, kLayers };
    VK_CHECK(vkCreateImageView(dev, &vi, nullptr, &m_view));
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE;
    VK_CHECK(vkCreateSampler(dev, &si, nullptr, &m_sampler));
    for (const std::string& name : decalBuiltIns()) layerOf(name);
}

DecalTextures::~DecalTextures() {
    VkDevice dev = m_device.device();
    if (m_sampler) vkDestroySampler(dev, m_sampler, nullptr);
    if (m_view) vkDestroyImageView(dev, m_view, nullptr);
    if (m_image) vmaDestroyImage(m_device.allocator(), m_image, m_alloc);
}

int DecalTextures::layerOf(const std::string& name) {
    if (const auto it = m_layers.find(name); it != m_layers.end()) return it->second;
    std::vector<uint8_t> rgba = decalTexture(name, static_cast<int>(kSize));
    if (rgba.empty()) {
        // assets/decals/<name>.png (through kke::cooked, so cooked art works too).
        std::vector<uint8_t> bytes;
        const std::string path = "assets/decals/" + name + ".png";
        if (!cooked::readAssetFile(path, bytes, nullptr)) {
            log::get("Decals")->warn("no decal texture '{}' (a built-in or {})", name, path);
            m_layers[name] = -1;
            return -1;
        }
        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &channels, 4);
        if (!pixels) {
            log::get("Decals")->warn("{}: {}", path, stbi_failure_reason() ? stbi_failure_reason() : "can't be read");
            m_layers[name] = -1;
            return -1;
        }
        rgba = resample(pixels, w, h, kSize);
        stbi_image_free(pixels);
    }
    if (m_used >= kLayers) {
        log::get("Decals")->warn("decal texture '{}': all {} layers are taken", name, kLayers);
        return -1;
    }
    const int layer = static_cast<int>(m_used++);
    upload(static_cast<uint32_t>(layer), rgba);
    m_layers[name] = layer;
    return layer;
}

void DecalTextures::upload(uint32_t layer, const std::vector<uint8_t>& rgba) {
    // The whole chain on the CPU (colour averaged in linear light), then one copy per level.
    std::vector<uint8_t> chain(rgba);
    std::vector<VkBufferImageCopy> regions;
    uint32_t size = kSize;
    size_t offset = 0;
    for (uint32_t level = 0; level < m_levels; ++level) {
        VkBufferImageCopy r{};
        r.bufferOffset = offset;
        r.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1 };
        r.imageExtent = { size, size, 1 };
        regions.push_back(r);
        if (level + 1 == m_levels) break;
        const uint32_t next = std::max(1u, size / 2);
        std::vector<uint8_t> smaller(static_cast<size_t>(next) * next * 4);
        downsampleRgba8Srgb(chain.data() + offset, size, size, smaller.data(), next, next);
        offset += static_cast<size_t>(size) * size * 4;
        chain.insert(chain.end(), smaller.begin(), smaller.end());
        size = next;
    }
    Buffer staging(m_device, chain.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
    staging.upload(chain.data(), chain.size());
    // Frames in flight may be sampling other layers: wait for them before this layer changes layout.
    vkDeviceWaitIdle(m_device.device());
    submitNow(m_device, [&](VkCommandBuffer cmd) {
        VkImageMemoryBarrier b{};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = m_image;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, m_levels, layer, 1 };
        b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        vkCmdCopyBufferToImage(cmd, staging.handle(), m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()),
                               regions.data());
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    });
}

} // namespace kke
