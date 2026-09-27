#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include "kke/Pipeline.h"

namespace kke {

class VulkanDevice;

// A real, working single-directional-light shadow map — the first
// concrete piece of "shadows/PBR," the one item this project's own
// docs/HISTORY.md had flagged as genuinely unstarted since the multi-light
// Blinn-Phong lighting slice. Deliberately scoped narrow rather than
// generalized to every light and every object at once: this owns one
// depth-only render target, computed from the scene's key light only
// (lights[0] in kke::Lighting — see Application.h), and only objects
// that explicitly implement Module::renderShadow() actually cast one.
// A real, working single-caster proof is worth more than a half-built
// system that tries to cover every case at once and gets none of them
// fully right — see docs/HISTORY.md "Immediate next slices" for what's
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
    // scene-bounds system yet (see docs/HISTORY.md), so callers pass in a
    // reasonable, hand-picked region themselves (e.g. CubeModule passes
    // its own cube's position and a small radius covering it and the
    // ground beneath).
    //
    // With a shadowMapResolution, the centre is snapped to whole shadow
    // texels along the light's axes: a region that follows the camera
    // then slides in texel steps, so shadow edges stay put instead of
    // shimmering as the camera moves (docs/RENDERING_PRINCIPLES.md §5).
    static glm::mat4 computeLightViewProj(const glm::vec3& lightDirection, const glm::vec3& sceneCenter, float sceneRadius,
                                          uint32_t shadowMapResolution = 0);

    // Starting config for a shadow-caster pipeline: no culling (thin and
    // open meshes still cast), and slope-scaled depth bias so the lit
    // side of a surface doesn't shadow itself (acne) without the large
    // constant bias that detaches shadows from their casters.
    static PipelineConfig casterConfig();

    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);

    // Several views (split screen): one tile of the map per view, each
    // framed on its own camera, so no view's shadows depend on another's
    // camera. 1 = the whole map, 2..4 = a 2 x 2 grid of tiles, each as
    // big as the map was (the image grows; true when it was recreated, and
    // the descriptor set that samples it must be written again).
    bool setTiles(uint32_t tiles);
    uint32_t tiles() const { return m_grid * m_grid; }
    // Where tile `tile` is in the map: x, y (top left) and width, height
    // in 0..1 texture coordinates.
    glm::vec4 tileRect(uint32_t tile) const;
    // Inside the render pass: draw into tile `tile` from here on.
    void beginTile(VkCommandBuffer cmd, uint32_t tile);

    VkRenderPass renderPass() const { return m_renderPass; }
    VkImageView imageView() const { return m_imageView; }
    VkSampler sampler() const { return m_sampler; }
    uint32_t resolution() const { return m_resolution; }   // the whole map, in texels
    uint32_t tileResolution() const { return m_tile; }     // one tile (one view's shadows)

private:
    void createTarget();
    void destroyTarget();

    VulkanDevice& m_device;
    uint32_t m_tile;       // texels across one view's tile
    uint32_t m_grid = 1;   // tiles across (1 or 2)
    uint32_t m_resolution; // texels across the whole map: m_tile * m_grid

    VkImage m_image = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    VkImageView m_imageView = VK_NULL_HANDLE;
    VkFormat m_format = VK_FORMAT_D32_SFLOAT;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
};

} // namespace kke
