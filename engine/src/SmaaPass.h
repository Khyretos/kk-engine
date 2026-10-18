#pragma once

#include "kke/Renderer.h"
#include "kke/Smaa.h"

#include <volk.h>

namespace kke {

class VulkanDevice;

// Morphological anti-aliasing (ENG-F05, kke/Smaa.h): three compute steps
// over the finished scene image, before the tone map reads it
// (shaders/smaa_edges, _weights, _blend.comp). Owned by kke::Renderer.
class SmaaPass {
public:
    explicit SmaaPass(VulkanDevice& device);
    ~SmaaPass();
    SmaaPass(const SmaaPass&) = delete;
    SmaaPass& operator=(const SmaaPass&) = delete;

    // Records the three steps into cmd for the scene image `scene` (readable,
    // SHADER_READ_ONLY) of `extent` pixels; `frame` is the frame-in-flight slot.
    // Afterwards output() holds the anti-aliased image, readable by fragment shaders.
    void record(VkCommandBuffer cmd, uint32_t frame, VkImageView scene, VkExtent2D extent);
    VkImageView output() const { return m_output.view; } // GENERAL layout
    SmaaSettings& settings() { return m_settings; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    void ensureImages(VkExtent2D extent);
    void destroyImages();

    VulkanDevice& m_device;
    SmaaSettings m_settings;
    VkSampler m_nearest = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_steps[3] = { VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE }; // edges, weights, blend
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Image m_edges, m_weights, m_output;
    VkExtent2D m_extent{};
    bool m_fresh = true; // the images are UNDEFINED: no reads to wait for
};

} // namespace kke
