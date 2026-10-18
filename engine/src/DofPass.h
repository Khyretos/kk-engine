#pragma once

#include "kke/DepthOfField.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <memory>

namespace kke {

class Buffer;
class VulkanDevice;

// Depth of field (ENG-F10 part 3, kke/DepthOfField.h): four compute steps
// (shaders/dof.glsl) after SMAA and TAA, before bloom and the tone map: the
// auto focus, a half-size image with each pixel's circle, the blur, and the
// mix into the sharp image. Owned by kke::Renderer.
class DofPass {
public:
    explicit DofPass(VulkanDevice& device);
    ~DofPass();
    DofPass(const DofPass&) = delete;
    DofPass& operator=(const DofPass&) = delete;

    // Records the steps into cmd. `scene` (in `sceneLayout`) is the light, `depth` its depth
    // (DEPTH_STENCIL_READ_ONLY, `depthSamples` samples), `proj` this frame's projection and `dt` the
    // seconds since the last frame (for the auto focus).
    void record(VkCommandBuffer cmd, uint32_t frame, VkImageView scene, VkImageLayout sceneLayout, VkImageView depth, uint32_t depthSamples,
                VkExtent2D extent, const glm::mat4& proj, float dt);
    VkImageView output() const { return m_result.view; } // GENERAL layout
    DofSettings& settings() { return m_settings; }

    static constexpr int kTaps = 32; // gather samples per half-size pixel

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    enum Step { Focus, Half, Gather, Composite, StepCount };
    void ensureImages(VkExtent2D extent);
    void makeImage(Image& out, VkExtent2D extent);
    void destroyImages();
    VkPipeline pipeline(Step step, bool multisampled);

    VulkanDevice& m_device;
    DofSettings m_settings;
    VkSampler m_nearest = VK_NULL_HANDLE, m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[StepCount][2] = {}; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    std::unique_ptr<Buffer> m_focus; // one float: the focus now (m)
    Image m_half, m_blurred, m_result;
    VkExtent2D m_extent{};
    bool m_fresh = true;
    bool m_focusFresh = true;
};

} // namespace kke
