#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>

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
// A second ShadowMap holds point-light shadows: six faces per light that
// casts (pointFace, pointFaceViewProj; docs/GAPS.md ENG-G04). Each
// view's frustum is split into kCascades cascades, each drawn into its
// own tile of the map (see kCascades below). Soft edges are 3x3 PCF (pbr_common.glsl, computeShadow),
// which also picks the cascade.
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

    // Cascaded shadow maps: each view's frustum, out to the shadow distance,
    // is cut into this many slices, nearest first. Each slice gets its own
    // tile of the map, so near shadows get small texels and far ones still exist.
    static constexpr uint32_t kCascades = 4;

    // The whole map never grows past this many texels across: more tiles
    // (split screen) make each tile smaller instead.
    static constexpr uint32_t kMaxResolution = 4096;

    // The distance from the camera where slice i starts (i = kCascades: where
    // the last one ends, shadowDistance). The "practical" split, a blend of
    // even and logarithmic steps.
    static float cascadeSplit(uint32_t i, float nearPlane, float shadowDistance);

    // The smallest sphere around each slice of the view frustum (xyz = centre,
    // w = radius). The radius depends only on the lens, never on where the
    // camera looks, so texels keep their size as it turns.
    static std::array<glm::vec4, kCascades> cascadeSpheres(const glm::vec3& eye, const glm::vec3& forward, float tanHalfFovY, float aspect,
                                                           float nearPlane, float shadowDistance);

    // A near cascade as the far cascade's map scaled and shifted: its clip-space
    // xy = the far cascade's xy * scale (xy) + offset (zw), its depth the same.
    // The centre is snapped to the near cascade's own texels (resolution = one
    // tile), so it does not shimmer.
    static glm::vec4 cascadeScaleOffset(const glm::mat4& farViewProj, const glm::vec3& lightDirection, float farRadius,
                                        const glm::vec3& center, float radius, uint32_t resolution);

    // That cascade's light view-projection: scaleOffset applied after farViewProj.
    static glm::mat4 cascadeViewProj(const glm::mat4& farViewProj, const glm::vec4& scaleOffset);

    // Point-light shadows: each shadow-casting point light gets a cube of
    // kPointFaces faces around it, each drawn into its own tile of a second
    // map (the point map): tile = slot * kPointFaces + face (Lighting::pointShadowSlots).
    static constexpr uint32_t kPointFaces = 6;
    // Texels each face's view reaches past its 90 degrees on every side, so the
    // normal offset and the 3x3 taps near a face's edge still read that face.
    static constexpr uint32_t kPointFaceMargin = 4;
    // Texels across one point-light face.
    static constexpr uint32_t kPointTileResolution = 512;
    // Point lights that cast shadows at once: the first this many get a slot (Lighting::pointShadowSlots).
    static constexpr uint32_t kMaxPointShadows = 4;

    // The face that sees direction toPoint from the light, by its largest axis:
    // 0 +X, 1 -X, 2 +Y, 3 -Y, 4 +Z, 5 -Z.
    static uint32_t pointFace(const glm::vec3& toPoint);

    // Face `face`'s view-projection for a point light at lightPos, depth 0..1 out to range,
    // kPointFaceMargin texels of a tileResolution tile wider than 90 degrees on each side.
    static glm::mat4 pointFaceViewProj(const glm::vec3& lightPos, uint32_t face, float range, uint32_t tileResolution);

    // Tiles across the map for `tiles` tiles: the smallest square grid that holds them.
    static uint32_t gridFor(uint32_t tiles);

    // Texels across one tile for `tiles` tiles when each may be `baseTile`:
    // baseTile, or smaller so the whole map stays within kMaxResolution.
    static uint32_t tileResolutionFor(uint32_t baseTile, uint32_t tiles);

    void beginRenderPass(VkCommandBuffer cmd);
    void endRenderPass(VkCommandBuffer cmd);

    // One tile per cascade of each view (views x kCascades), in the
    // smallest square grid that holds them (gridFor); each tile is as
    // big as the constructor's resolution while the whole map stays
    // within kMaxResolution, smaller beyond that. Returns true when the
    // image was recreated, and then the descriptor set that samples it
    // must be written again.
    bool setTiles(uint32_t tiles);
    // The tile size the map grows to while it has room (EngineSettings'
    // shadowResolution). Takes effect at the next setTiles().
    void setBaseTileResolution(uint32_t texels) { m_baseTile = texels; }
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
    uint32_t m_baseTile;   // texels across a tile when the map has room (the constructor's resolution)
    uint32_t m_tile;       // texels across one tile (one cascade of one view)
    uint32_t m_grid = 1;   // tiles across (ShadowMap::gridFor)
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
