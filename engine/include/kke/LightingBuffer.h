#pragma once

#include "kke/Buffer.h"
#include "kke/VolumetricFog.h"
#include "kke/ClusteredLights.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace kke {

class VulkanDevice;
struct SkyEnvironment;
class Texture;
struct Lighting; // defined in Application.h — forward-declared here since
                  // update()'s signature is all this file actually needs

// Owns the real GPU-side uniform buffer + descriptor set (set 0,
// binding 0) that feeds Application::lighting()'s CPU-side data into
// every lit shader. One instance, owned by Application, shared by
// every module that draws lit geometry — see cube.vert/frag for the
// exact GLSL-side layout this mirrors byte-for-byte (all vec4-based
// fields deliberately, to sidestep std140's vec3-padding rules rather
// than fight them).
class LightingBuffer {
public:
    explicit LightingBuffer(VulkanDevice& device);
    ~LightingBuffer();

    LightingBuffer(const LightingBuffer&) = delete;
    LightingBuffer& operator=(const LightingBuffer&) = delete;

    // Uploads the current Lighting state to the GPU — call once per
    // frame, before any lit draw calls, not per-object: every module
    // that draws lit geometry shares this same one buffer and
    // descriptor set. cameraPos is threaded through here rather than
    // via push constants (already at 128 bytes with mvp+model, the
    // guaranteed-minimum limit on some hardware) — not perfectly
    // semantically "lighting" data, but a pragmatic, documented choice
    // given the real constraint, not an accident.
    // `sky` is the resolved sky (kke::SkyResolver); without one, the sky
    // is off and ambient is Lighting::ambientColor, flat.
    // `shadowTiles` and `cascadeScaleOffsets` are ShadowMap::kCascades
    // entries each: where each cascade of this view was drawn in the shadow
    // map (ShadowMap::tileRect) and how its clip-space xy follows from
    // lightViewProj, the far cascade's (ShadowMap::cascadeScaleOffset). Null
    // means one cascade covering the whole map.
    // `pointViewProjs` and `pointTiles` are ShadowMap::kMaxPointShadows *
    // ShadowMap::kPointFaces entries each (index slot * kPointFaces + face, slot from Lighting::pointShadowSlots):
    // each face's view-projection (ShadowMap::pointFaceViewProj) and its tile
    // of the point map. Null pointViewProjs means no point light casts
    // shadows this frame.
    void update(const Lighting& lighting, const glm::vec3& cameraPos, const glm::mat4& lightViewProj, const glm::mat4& viewProj,
                const SkyEnvironment* sky = nullptr, const glm::vec4* shadowTiles = nullptr, const glm::vec4* cascadeScaleOffsets = nullptr,
                const glm::mat4* pointViewProjs = nullptr, const glm::vec4* pointTiles = nullptr);

    // The sky prefiltered for reflections (set 0, binding 1; SkyRenderer::reflections)
    // and its mip count. Null means none: the shaders reflect the ambient SH
    // instead (exact for a flat ambient). Each update() writes it into that
    // frame slot's descriptor set, which the GPU is done with by then.
    void setEnvironmentMap(const Texture* map, uint32_t levels) { m_envMap = map; m_envLevels = map ? levels : 0; }

    // Screen-space GI (ENG-F01, set 0 bindings 2 and 3): the previous frame's
    // bounce light and visibility, and its view depth (both in GENERAL
    // layout), made with camera `viewProj`, `size` pixels. Null `light`: no
    // GI, the shaders use the sky alone. Written by each update() like the map.
    // Reflections (ENG-F02, set 0 bindings 4 and 5): the previous frame's
    // reflected light (a = 1 where found) and its view depth, as for GI;
    // surfaces smoother than 1 - maxRoughness mix it over the sky. Null: none.
    void setReflections(VkImageView light, VkImageView depth, VkSampler sampler, const glm::mat4& viewProj, const glm::vec2& size,
                        float maxRoughness) {
        m_reflLight = light;
        m_reflDepth = depth;
        m_reflSampler = sampler;
        m_reflViewProj = viewProj;
        m_reflSize = size;
        m_reflMaxRoughness = maxRoughness;
    }
    // Refraction (ENG-F02 part 4, set 0 binding 6): this frame's scene drawn
    // before the see-through surfaces (kke::BehindPass, GENERAL layout),
    // `size` pixels. Null: none (translucents absorb what is behind them).
    // Volumetric fog (ENG-F03, set 0 binding 7): this frame's integrated grid
    // (kke::VolumetricFogPass, GENERAL layout) made with `settings`. Null:
    // none (the analytic height fog alone).
    void setFogVolume(VkImageView volume, VkSampler sampler, const VolumetricFogSettings& settings) {
        m_fogVolume = volume;
        m_fogVolumeSampler = sampler;
        m_fogVolumeSettings = settings;
    }
    // Ray-traced sun shadows (ENG-F07, set 0 binding 8): last frame's rays
    // (kke::ShadowRayPass, GENERAL layout) cast with camera `viewProj`, `size`
    // pixels; a ray's depth may differ by `slack` of the distance and still
    // count. Null: none (the shadow map alone).
    // Many lights (ENG-F08, bindings 9-11): this frame's light buffer (`count`
    // of them) and the per-cell lists made with `grid` on a `screen`-pixel
    // scene (kke::ClusterLightPass). `all`: the shaders walk every light
    // instead (split screen, or KKE_CLUSTERS=0). Null: none.
    void setManyLights(VkBuffer lights, VkBuffer counts, VkBuffer indices, uint32_t count, const ClusterGrid& grid, const glm::vec2& screen,
                       bool all) {
        m_manyLights = lights;
        m_clusterCounts = counts;
        m_clusterIndices = indices;
        m_manyCount = count;
        m_clusterGrid = grid;
        m_clusterScreen = screen;
        m_clusterAll = all;
    }
    // Decals (ENG-F12, bindings 12-15): this frame's decal records (`count`,
    // kke::GpuDecal), their per-cell lists made with `grid` on a `screen`-pixel
    // scene (a kke::ClusterLightPass of their bounding spheres), and their
    // texture array (kke::DecalTextures). Null: none.
    void setDecals(VkBuffer decals, VkBuffer counts, VkBuffer indices, uint32_t count, const ClusterGrid& grid, const glm::vec2& screen,
                   VkImageView textures, VkSampler sampler) {
        m_decals = decals;
        m_decalCounts = counts;
        m_decalIndices = indices;
        m_decalCount = count;
        m_decalGrid = grid;
        m_decalScreen = screen;
        m_decalTextures = textures;
        m_decalSampler = sampler;
    }
    void setShadowRays(VkImageView image, VkSampler sampler, const glm::mat4& viewProj, const glm::vec2& size, float slack) {
        m_shadowRays = image;
        m_shadowRaySampler = sampler;
        m_shadowRayViewProj = viewProj;
        m_shadowRaySize = size;
        m_shadowRaySlack = slack;
    }
    void setBehind(VkImageView image, VkSampler sampler, const glm::vec2& size) {
        m_behind = image;
        m_behindSampler = sampler;
        m_behindSize = size;
    }
    void setScreenGi(VkImageView light, VkImageView depth, VkSampler sampler, const glm::mat4& viewProj, const glm::vec2& size) {
        m_giLight = light;
        m_giDepth = depth;
        m_giSampler = sampler;
        m_giViewProj = viewProj;
        m_giSize = size;
    }

    // Picks the frame-in-flight slot that update() writes and
    // descriptorSet() returns. Each slot has its own buffer: the GPU may
    // still be drawing the previous frame with its camera and shadow
    // matrices while this frame writes new ones (a phone's GPU runs a
    // frame behind; sharing one buffer showed as dark shadow patches
    // whenever the camera moved fast). Call it after the frame's fence
    // wait (Renderer::beginFrame). Standalone users can leave it at 0.
    static constexpr uint32_t kSlotCount = 2; // Renderer::kMaxFramesInFlight (checked in LightingBuffer.cpp)
    void setFrame(uint32_t frame) { m_slot = frame % kSlotCount; }

    VkDescriptorSetLayout descriptorSetLayout() const { return m_setLayout; }
    VkDescriptorSet descriptorSet() const { return m_descriptorSets[m_slot]; }

private:
    VulkanDevice& m_device;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSets[kSlotCount] = {};
    std::unique_ptr<Buffer> m_buffers[kSlotCount];
    uint32_t m_slot = 0;

    void writeEnvironment(uint32_t slot); // bindings 1-8 of that slot: m_envMap, the GI, reflection, behind, fog and shadow-ray images, or placeholders
    void createVolumePlaceholder();
    std::unique_ptr<Texture> m_placeholder; // 1x1 black, bound while there is no map
    const Texture* m_envMap = nullptr;
    uint32_t m_envLevels = 0;
    VkImageView m_giLight = VK_NULL_HANDLE, m_giDepth = VK_NULL_HANDLE;
    VkSampler m_giSampler = VK_NULL_HANDLE;
    glm::mat4 m_giViewProj{1.0f};
    glm::vec2 m_giSize{0.0f};
    VkImageView m_reflLight = VK_NULL_HANDLE, m_reflDepth = VK_NULL_HANDLE;
    VkSampler m_reflSampler = VK_NULL_HANDLE;
    glm::mat4 m_reflViewProj{1.0f};
    glm::vec2 m_reflSize{0.0f};
    float m_reflMaxRoughness = 0.3f;
    VkImageView m_behind = VK_NULL_HANDLE;
    VkSampler m_behindSampler = VK_NULL_HANDLE;
    glm::vec2 m_behindSize{0.0f};
    VkBuffer m_manyLights = VK_NULL_HANDLE, m_clusterCounts = VK_NULL_HANDLE, m_clusterIndices = VK_NULL_HANDLE;
    uint32_t m_manyCount = 0;
    ClusterGrid m_clusterGrid;
    glm::vec2 m_clusterScreen{1.0f};
    bool m_clusterAll = false;
    std::unique_ptr<Buffer> m_placeholderBuffer; // bindings 9-14 while there are no many lights or decals
    VkBuffer m_decals = VK_NULL_HANDLE, m_decalCounts = VK_NULL_HANDLE, m_decalIndices = VK_NULL_HANDLE;
    uint32_t m_decalCount = 0;
    ClusterGrid m_decalGrid;
    glm::vec2 m_decalScreen{1.0f};
    VkImageView m_decalTextures = VK_NULL_HANDLE;
    VkSampler m_decalSampler = VK_NULL_HANDLE;
    // Binding 15 while there are no decals: a 1x1 array of one layer.
    VkImage m_decalArray = VK_NULL_HANDLE;
    VmaAllocation m_decalArrayAlloc = VK_NULL_HANDLE;
    VkImageView m_decalArrayView = VK_NULL_HANDLE;
    VkSampler m_decalArraySampler = VK_NULL_HANDLE;
    VkImageView m_shadowRays = VK_NULL_HANDLE;
    VkSampler m_shadowRaySampler = VK_NULL_HANDLE;
    glm::mat4 m_shadowRayViewProj{1.0f};
    glm::vec2 m_shadowRaySize{0.0f};
    float m_shadowRaySlack = 0.03f;
    VkImageView m_fogVolume = VK_NULL_HANDLE;
    VkSampler m_fogVolumeSampler = VK_NULL_HANDLE;
    VolumetricFogSettings m_fogVolumeSettings;
    // 1x1x1, bound while there is no fog volume.
    VkImage m_volumeImage = VK_NULL_HANDLE;
    VmaAllocation m_volumeAlloc = VK_NULL_HANDLE;
    VkImageView m_volumeView = VK_NULL_HANDLE;
    VkSampler m_volumeSampler = VK_NULL_HANDLE;
};

} // namespace kke
