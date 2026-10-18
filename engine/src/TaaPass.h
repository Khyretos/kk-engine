#pragma once

#include "kke/Renderer.h"
#include "kke/Taa.h"

#include <glm/glm.hpp>
#include <volk.h>

namespace kke {

class VulkanDevice;

// Temporal anti-aliasing, an option over the clean image (ENG-F05 part 2,
// kke/Taa.h): one compute dispatch (shaders/taa.comp) mixes this frame into
// the reprojected history, after SMAA, before the tone map. Two history
// images take turns being read and written. Owned by kke::Renderer.
class TaaPass {
public:
    explicit TaaPass(VulkanDevice& device);
    ~TaaPass();
    TaaPass(const TaaPass&) = delete;
    TaaPass& operator=(const TaaPass&) = delete;

    // Records the step into cmd. `current` (in `currentLayout`) is this frame's
    // scene light, `depth` its depth (DEPTH_STENCIL_READ_ONLY, `depthSamples`
    // samples); `reproject` takes this frame's NDC and depth to last frame's
    // clip space. `historyValid` false starts over (first frame, a resize, a cut).
    void record(VkCommandBuffer cmd, uint32_t frame, VkImageView current, VkImageLayout currentLayout, VkImageView depth, uint32_t depthSamples,
                VkExtent2D extent, const glm::mat4& reproject, bool historyValid);
    VkImageView output() const { return m_history[m_written].view; } // GENERAL layout
    TaaSettings& settings() { return m_settings; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    bool ensureImages(VkExtent2D extent); // true when they were made anew
    void destroyImages();
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    TaaSettings m_settings;
    VkSampler m_nearest = VK_NULL_HANDLE, m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Image m_history[2];
    uint32_t m_written = 0; // which history image holds the latest result
    VkExtent2D m_extent{};
    bool m_fresh = true;
};

} // namespace kke
