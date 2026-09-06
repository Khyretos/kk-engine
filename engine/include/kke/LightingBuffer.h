#pragma once

#include "kke/Buffer.h"

#include <volk.h>
#include <glm/glm.hpp>
#include <memory>

namespace kke {

class VulkanDevice;
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
    void update(const Lighting& lighting, const glm::vec3& cameraPos, const glm::mat4& lightViewProj);

    VkDescriptorSetLayout descriptorSetLayout() const { return m_setLayout; }
    VkDescriptorSet descriptorSet() const { return m_descriptorSet; }

private:
    VulkanDevice& m_device;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> m_buffer;
};

} // namespace kke
