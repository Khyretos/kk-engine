#pragma once

#include "kke/ColorLut.h"

#include <volk.h>
#include <vk_mem_alloc.h>

namespace kke {

class VulkanDevice;

// A kke::ColorLut on the GPU (ENG-F10): a size^3 3D image (RGBA16F) the
// tone map reads with texelFetch for its tetrahedral lookup. Made once per
// LUT (the call waits for the upload).
class ColorLutTexture {
public:
    ColorLutTexture(VulkanDevice& device, const ColorLut& lut);
    ~ColorLutTexture();
    ColorLutTexture(const ColorLutTexture&) = delete;
    ColorLutTexture& operator=(const ColorLutTexture&) = delete;

    VkImageView view() const { return m_view; }
    VkSampler sampler() const { return m_sampler; }
    int size() const { return m_size; }

private:
    VulkanDevice& m_device;
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_alloc = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    int m_size = 0;
};

} // namespace kke
