#pragma once

#include "kke/ContactShadows.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <volk.h>

namespace kke {

class VulkanDevice;

// Contact shadows (ENG-F16, kke/ContactShadows.h): after the frame, one
// compute dispatch (shaders/contact_shadows.comp) marches a short way
// towards the sun through the frame's depth from each pixel and writes how
// lit it is and its view depth. The next frame's lit shaders read it
// (kke::LightingBuffer binding 16), reprojected, and multiply the sun's
// shadow with it. Any Vulkan device.
class ContactShadowPass {
public:
    explicit ContactShadowPass(VulkanDevice& device);
    ~ContactShadowPass();
    ContactShadowPass(const ContactShadowPass&) = delete;
    ContactShadowPass& operator=(const ContactShadowPass&) = delete;

    // Sizes the image for a scene of `sceneExtent` pixels: call at the start
    // of the frame, before anything reads it (a resize waits for the GPU).
    void prepare(VkExtent2D sceneExtent);
    // Records the march into cmd after Renderer::endScenePass(). `toSun`:
    // world-space direction towards the sun.
    void record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& view, const glm::mat4& proj,
                const glm::vec3& toSun, const ContactShadowSettings& settings);
    void invalidate() { m_ready = false; }
    bool ready() const { return m_ready; }
    VkImageView image() const { return m_view; } // GENERAL layout
    VkSampler sampler() const { return m_nearest; }
    const glm::mat4& viewProj() const { return m_viewProj; }
    glm::vec2 size() const { return glm::vec2(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height)); }

private:
    void destroyImage();
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    VkSampler m_nearest = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_alloc = VK_NULL_HANDLE;
    VkImageView m_view = VK_NULL_HANDLE;
    VkExtent2D m_extent{};
    bool m_fresh = true;
    bool m_ready = false;
    glm::mat4 m_viewProj{1.0f};
};

} // namespace kke
