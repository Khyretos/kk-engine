#pragma once

#include "kke/Renderer.h"
#include "kke/ScreenGi.h"

#include <glm/glm.hpp>
#include <volk.h>

namespace kke {

class VulkanDevice;

// The GPU side of kke::screenGi (ENG-F01): after the scene pass, one
// compute dispatch over a half-resolution image reads the frame's depth and
// light (shaders/screen_gi.comp) and writes each pixel's bounce light and
// visibility, plus its view depth. The next frame's lit shaders read them
// (kke::LightingBuffer, binding 2 and 3), reprojected with this frame's
// camera: light reaches surfaces one frame late, every frame clean on its own.
class ScreenGiPass {
public:
    explicit ScreenGiPass(VulkanDevice& device);
    ~ScreenGiPass();
    ScreenGiPass(const ScreenGiPass&) = delete;
    ScreenGiPass& operator=(const ScreenGiPass&) = delete;

    // Sizes the images for a scene of `sceneExtent` pixels: call at the start
    // of the frame, before anything reads them (a resize waits for the GPU).
    void prepare(VkExtent2D sceneExtent);
    // Records the pass into cmd, after Renderer::endScenePass() (the scene's
    // images are readable then). `frame` is the frame-in-flight slot.
    void record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& view, const glm::mat4& proj,
                const ScreenGiSettings& settings);
    // Nothing recorded this frame (GI off, split screen): the next frame
    // must not read what is there.
    void invalidate() { m_ready = false; }
    // The images hold a frame's result, made with viewProj().
    bool ready() const { return m_ready; }
    VkImageView light() const { return m_light.view; }
    VkImageView depth() const { return m_depth.view; }
    VkSampler sampler() const { return m_linear; }
    const glm::mat4& viewProj() const { return m_viewProj; }
    glm::vec2 size() const { return glm::vec2(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height)); }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    void ensureImages(VkExtent2D full);
    void destroyImages();
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    VkSampler m_nearest = VK_NULL_HANDLE, m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Image m_light, m_depth;
    VkExtent2D m_extent{};
    bool m_fresh = true; // the images are UNDEFINED: no reads to wait for
    bool m_ready = false;
    glm::mat4 m_viewProj{1.0f};
};

} // namespace kke
