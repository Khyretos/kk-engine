#pragma once

#include "kke/Renderer.h"
#include "kke/MotionBlur.h"

#include <glm/glm.hpp>
#include <volk.h>

namespace kke {

class VulkanDevice;

// Motion blur from the camera's movement (ENG-F10 part 4, kke/MotionBlur.h):
// one compute dispatch (shaders/motion_blur.glsl) after depth of field,
// before bloom and the tone map. Owned by kke::Renderer.
class MotionBlurPass {
public:
    explicit MotionBlurPass(VulkanDevice& device);
    ~MotionBlurPass();
    MotionBlurPass(const MotionBlurPass&) = delete;
    MotionBlurPass& operator=(const MotionBlurPass&) = delete;

    // Records the step into cmd. `current` (in `currentLayout`) is this frame's
    // scene light, `depth` its depth (DEPTH_STENCIL_READ_ONLY, `depthSamples`
    // samples); `reproject` takes this frame's NDC and depth to last frame's
    // clip space and `proj` is this frame's projection. `moved` false (the
    // first frame, a cut) leaves the image sharp.
    void record(VkCommandBuffer cmd, uint32_t frame, VkImageView current, VkImageLayout currentLayout, VkImageView depth, uint32_t depthSamples,
                VkExtent2D extent, const glm::mat4& reproject, const glm::mat4& proj, bool moved);
    VkImageView output() const { return m_result.view; } // GENERAL layout
    MotionBlurSettings& settings() { return m_settings; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    void ensureImage(VkExtent2D extent);
    void destroyImage();
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    MotionBlurSettings m_settings;
    VkSampler m_nearest = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Image m_result;
    VkExtent2D m_extent{};
    uint32_t m_frameNumber = 0; // moves the gather's noise each frame
    bool m_fresh = true;
};

} // namespace kke
