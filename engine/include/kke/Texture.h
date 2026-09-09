#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <string>
#include <cstdint>

namespace kke {

class VulkanDevice;

// A real, general-purpose 2D texture for the 3D rendering pipeline —
// the first real consumer of stb_image outside RmlUi's own
// RmlVulkanRenderInterface (which has its own, separate texture
// creation code; deliberately not shared with this class, to avoid
// risking a working, already-verified RmlUi code path while building
// something new — see this class's own git history/README entry for
// the reasoning). Owns everything needed to bind and sample it:
// image, view, and a real sampler (LINEAR filtering, REPEAT wrap —
// standard defaults for a material texture, as opposed to
// RmlVulkanRenderInterface's own CLAMP_TO_EDGE-appropriate UI
// textures).
//
// Two ways to create one: from a real image file on disk (decoded via
// stb_image, forcing RGBA8 to match the fixed VK_FORMAT_R8G8B8A8_UNORM
// this always uses), or from raw in-memory RGBA8 pixels directly (used
// for a solid-color placeholder texture — see kke::Application's own
// default 1x1 white texture, the "untextured" stand-in every pipeline
// sharing cube.frag needs bound regardless of whether that specific
// object has a real texture of its own).
class Texture {
public:
    Texture(VulkanDevice& device, const std::string& filePath);
    Texture(VulkanDevice& device, const uint8_t* rgbaPixels, uint32_t width, uint32_t height);
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView imageView() const { return m_imageView; }
    VkSampler sampler() const { return m_sampler; }

private:
    void createFromPixels(const uint8_t* rgbaPixels, uint32_t width, uint32_t height);

    VulkanDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace kke
