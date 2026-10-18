#pragma once

#include "kke/Buffer.h"
#include "kke/Renderer.h"
#include "kke/ShadowRays.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <memory>

namespace kke {

struct ReflectionRays;
class VulkanDevice;

// Ray-traced sun shadows (ENG-F07, kke/ShadowRays.h): after the frame, one
// compute dispatch (shaders/shadow_rays.comp) casts a ray towards the sun
// from each pixel the setting traces, through the frame's kke::RayScene,
// and writes how lit it is and its view depth. The next frame's lit shaders
// read it (kke::LightingBuffer binding 8), reprojected; where they can't,
// the shadow map answers. Ray-query devices only.
class ShadowRayPass {
public:
    explicit ShadowRayPass(VulkanDevice& device);
    ~ShadowRayPass();
    ShadowRayPass(const ShadowRayPass&) = delete;
    ShadowRayPass& operator=(const ShadowRayPass&) = delete;

    // Sizes the image for a scene of `sceneExtent` pixels: call at the start
    // of the frame, before anything reads it (a resize waits for the GPU).
    void prepare(VkExtent2D sceneExtent);
    // Records the rays into cmd after Renderer::endScenePass(). `rays` holds
    // this frame's built scene, its materials and the sun; rays->classic
    // traces every pixel.
    void record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& view, const glm::mat4& proj,
                const ReflectionRays& rays, const ShadowRaySettings& settings);
    void invalidate() { m_ready = false; }
    bool ready() const { return m_ready; }
    VkImageView image() const { return m_image.view; } // GENERAL layout
    VkSampler sampler() const { return m_nearest; }
    const glm::mat4& viewProj() const { return m_viewProj; }
    glm::vec2 size() const { return glm::vec2(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height)); }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Slot {
        std::unique_ptr<Buffer> materials, params;
        size_t materialCapacity = 0; // RayMaterials
    };
    void destroyImage();
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    VkSampler m_nearest = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Slot m_slots[Renderer::kMaxFramesInFlight];
    Image m_image;
    VkExtent2D m_extent{};
    bool m_fresh = true;
    bool m_ready = false;
    glm::mat4 m_viewProj{1.0f};
};

} // namespace kke
