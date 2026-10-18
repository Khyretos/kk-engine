#pragma once

#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <volk.h>

namespace kke {

class VulkanDevice;

// What see-through surfaces show behind them (ENG-F02 part 4, refraction):
// between Renderer::pauseScenePass() and resumeScenePass(), one compute
// dispatch (shaders/behind.comp) copies the scene drawn so far into an image
// with each pixel's distance from the camera in alpha (0 = sky). The
// translucent shaders read it this frame (kke::LightingBuffer, binding 6) at
// the spot the refracted ray leads to (kke::refractedSample).
class BehindPass {
public:
    explicit BehindPass(VulkanDevice& device);
    ~BehindPass();
    BehindPass(const BehindPass&) = delete;
    BehindPass& operator=(const BehindPass&) = delete;

    // Sizes the image for a scene of `sceneExtent` pixels: call at the start
    // of the frame, before anything reads it (a resize waits for the GPU).
    void prepare(VkExtent2D sceneExtent);
    // Records the copy into cmd while the scene pass is paused. `frame` is
    // the frame-in-flight slot. Afterwards the image is readable by fragment shaders.
    void record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& proj);
    VkImageView image() const { return m_view; }
    VkSampler sampler() const { return m_linear; }
    glm::vec2 size() const { return glm::vec2(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height)); }

private:
    void destroyImage();
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    VkSampler m_nearest = VK_NULL_HANDLE, m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_alloc = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkExtent2D m_extent{};
    bool m_fresh = true; // the image is UNDEFINED: no reads to wait for
};

} // namespace kke
