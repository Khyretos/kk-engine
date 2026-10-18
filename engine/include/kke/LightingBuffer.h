#pragma once

#include "kke/Buffer.h"

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

    void writeEnvironment(uint32_t slot); // bindings 1-6 of that slot: m_envMap, the GI, reflection and behind images, or m_placeholder
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
};

} // namespace kke
