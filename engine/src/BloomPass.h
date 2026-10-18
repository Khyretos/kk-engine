#pragma once

#include "kke/Bloom.h"
#include "kke/Renderer.h"

#include <volk.h>

#include <vector>

namespace kke {

class VulkanDevice;

// Bloom (ENG-F10, kke/Bloom.h): compute halvings of the scene light
// (shaders/bloom_down.comp) and the build back up (bloom_up.comp), before
// the tone map, which mixes the result in. Owned by kke::Renderer.
class BloomPass {
public:
    explicit BloomPass(VulkanDevice& device);
    ~BloomPass();
    BloomPass(const BloomPass&) = delete;
    BloomPass& operator=(const BloomPass&) = delete;

    // Records the chain into cmd for `scene` (in `sceneLayout`) of `extent`
    // pixels; `frame` is the frame-in-flight slot. Afterwards output() is
    // readable by fragment shaders.
    void record(VkCommandBuffer cmd, uint32_t frame, VkImageView scene, VkImageLayout sceneLayout, VkExtent2D extent);
    VkImageView output() const; // GENERAL layout, half the scene's size
    VkSampler sampler() const { return m_linear; }
    BloomSettings& settings() { return m_settings; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent2D extent{};
    };
    void ensureImages(VkExtent2D sceneExtent);
    void destroyImages();
    VkPipeline makePipeline(const char* shader, VkPipelineLayout layout);

    VulkanDevice& m_device;
    BloomSettings m_settings;
    VkSampler m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_downLayout = VK_NULL_HANDLE, m_upLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_downPipelineLayout = VK_NULL_HANDLE, m_upPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_down = VK_NULL_HANDLE, m_up = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_firstDown[Renderer::kMaxFramesInFlight] = {}; // reads the frame's scene
    std::vector<VkDescriptorSet> m_downSets, m_upSets;               // level i reads level i - 1; up i reads up i + 1 and down i
    std::vector<Image> m_downImages, m_upImages;
    VkExtent2D m_sceneExtent{};
    uint32_t m_levels = 0;
    bool m_fresh = true;
};

} // namespace kke
