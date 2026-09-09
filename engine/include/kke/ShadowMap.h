#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

namespace kke {

class VulkanDevice;

// A real, working single-directional-light shadow map — the first
// concrete piece of "shadows/PBR," the one item this project's own
// README had flagged as genuinely unstarted since the multi-light
// Blinn-Phong lighting slice. Deliberately scoped narrow rather than
// generalized to every light and every object at once: this owns one
// depth-only render target, computed from the scene's key light only
// (lights[0] in kke::Lighting — see Application.h), and only objects
// that explicitly implement Module::renderShadow() actually cast one.
// A real, working single-caster proof is worth more than a half-built
// system that tries to cover every case at once and gets none of them
// fully right — see README "Immediate next slices" for what's
// deliberately still out of scope (point-light shadows, multiple
// shadow casters generalized across every module, cascaded/multiple
// shadow maps for large scenes). Soft shadow edges (3x3 PCF) are real
// and working now -- see cube.frag's own computeShadow().
//
// Standard shadow-mapping technique, nothing exotic: render the scene
// from the light's own point of view into a depth-only image, then in
// the main lighting pass, transform each fragment's world position
// into that same light-space and compare its depth against what's
// stored in the shadow map — if the fragment is meaningfully farther
// from the light than what the shadow map recorded at that spot,
// something else was closer to the light there, so this fragment is
// in shadow.
class ShadowMap {
public:
    explicit ShadowMap(VulkanDevice& device, uint32_t resolution = 2048);
    ~ShadowMap();

    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Computes a light-space view-projection matrix for a directional
    // light, framed around a scene region (center + radius) rather than
    // the whole world — a directional light's shadow needs *some*
    // finite orthographic box, and this project doesn't have a real
    // scene-bounds system yet (see README), so callers pass in a
    // reasonable, hand-picked region themselves (e.g. CubeModule passes
    // its own cube's position and a small radius covering it and the
    // ground beneath).
    static glm::mat4 computeLightViewProj(const glm::vec3& lightDirection, const glm::vec3& sceneCenter, float sceneRadius);

    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);

    VkRenderPass renderPass() const { return m_renderPass; }
    VkImageView imageView() const { return m_imageView; }
    VkSampler sampler() const { return m_sampler; }
    uint32_t resolution() const { return m_resolution; }

private:
    VulkanDevice& m_device;
    uint32_t m_resolution;

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_D32_SFLOAT;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace kke
