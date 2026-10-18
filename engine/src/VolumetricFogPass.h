#pragma once

#include "kke/Renderer.h"
#include "kke/VolumetricFog.h"

#include <glm/glm.hpp>
#include <volk.h>

namespace kke {

class VulkanDevice;

// Volumetric fog (ENG-F03, kke/VolumetricFog.h): two compute dispatches
// after the shadow pass, before the scene. shaders/volumetric_scatter.comp
// lights each cell of a grid over the view frustum (the sun with its shadow,
// the point lights) and shaders/volumetric_integrate.comp sums each column
// from the camera outwards. The lit shaders read the result this frame
// (kke::LightingBuffer binding 7, lighting_ubo.glsl fogAlong).
class VolumetricFogPass {
public:
    // The lighting set (0) and the shadow map's set (1) the scatter step reads.
    VolumetricFogPass(VulkanDevice& device, VkDescriptorSetLayout lightingLayout, VkDescriptorSetLayout shadowLayout);
    ~VolumetricFogPass();
    VolumetricFogPass(const VolumetricFogPass&) = delete;
    VolumetricFogPass& operator=(const VolumetricFogPass&) = delete;

    // Makes the grid's images for `settings` (a new size waits for the GPU).
    void prepare(const VolumetricFogSettings& settings);
    // Records both steps into cmd, outside a render pass, after the shadow
    // pass: `lighting` is this frame's lighting set, `invViewProj` the camera's.
    void record(VkCommandBuffer cmd, VkDescriptorSet lighting, VkDescriptorSet shadow, const glm::mat4& invViewProj);
    VkImageView volume() const { return m_volume.view; }
    VkSampler sampler() const { return m_linear; }
    const VolumetricFogSettings& settings() const { return m_settings; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    void destroyImages();
    VkPipeline makePipeline(const char* shader, VkPipelineLayout layout);

    VulkanDevice& m_device;
    VkSampler m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_cellLayout = VK_NULL_HANDLE, m_integrateLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_scatterPipelineLayout = VK_NULL_HANDLE, m_integratePipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_scatter = VK_NULL_HANDLE, m_integrate = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_cellSet = VK_NULL_HANDLE, m_integrateSet = VK_NULL_HANDLE;
    Image m_cells, m_volume;
    VolumetricFogSettings m_settings;
    bool m_fresh = true; // the images are UNDEFINED: no reads to wait for
};

} // namespace kke
