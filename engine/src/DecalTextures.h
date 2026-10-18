#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

class VulkanDevice;

// The decals' textures (ENG-F12, kke/Decals.h): one array of kSize-square
// RGBA8 sRGB layers with mips, the built-ins made in code at the start and
// assets/decals/<name>.png loaded into the next layer the first time a
// decal names it. The lit shaders sample it at kke::LightingBuffer binding 15.
class DecalTextures {
public:
    static constexpr uint32_t kSize = 256;
    static constexpr uint32_t kLayers = 32;

    explicit DecalTextures(VulkanDevice& device);
    ~DecalTextures();
    DecalTextures(const DecalTextures&) = delete;
    DecalTextures& operator=(const DecalTextures&) = delete;

    // The layer of texture `name`, loading it the first time (the GPU is waited for); -1 when it can't be found or
    // every layer is taken.
    int layerOf(const std::string& name);
    VkImageView view() const { return m_view; }
    VkSampler sampler() const { return m_sampler; }

private:
    void upload(uint32_t layer, const std::vector<uint8_t>& rgba); // kSize x kSize, its mips made here

    VulkanDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_alloc = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    uint32_t m_levels = 1;
    std::map<std::string, int> m_layers;
    uint32_t m_used = 0;
};

} // namespace kke
