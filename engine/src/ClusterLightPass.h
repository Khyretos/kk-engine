#pragma once

#include "kke/Buffer.h"
#include "kke/ClusteredLights.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <memory>
#include <vector>

namespace kke {

class VulkanDevice;

// Many lights (ENG-F08, kke/ClusteredLights.h): the lights past the 16 key
// ones go into a storage buffer each frame, and one compute dispatch
// (shaders/cluster_lights.comp) lists per cell of the view the ones that
// touch it. The lit shaders read all three buffers (kke::LightingBuffer
// bindings 9-11). Buffers per frame in flight.
class ClusterLightPass {
public:
    static constexpr uint32_t kMaxLights = 1024;
    struct GpuLight {
        glm::vec4 positionRange;  // xyz = world position, w = reach
        glm::vec4 colorIntensity; // rgb = colour, a = intensity
    };

    explicit ClusterLightPass(VulkanDevice& device);
    ~ClusterLightPass();
    ClusterLightPass(const ClusterLightPass&) = delete;
    ClusterLightPass& operator=(const ClusterLightPass&) = delete;

    // This frame's lights (at most kMaxLights are used) into frame slot `frame`'s buffer.
    void upload(uint32_t frame, const std::vector<GpuLight>& lights);
    // Lists them per cell for camera `view` / `proj`, outside any render pass, before the scene.
    void record(VkCommandBuffer cmd, uint32_t frame, const glm::mat4& view, const glm::mat4& proj);
    VkBuffer lights(uint32_t frame) const { return m_slots[frame % Renderer::kMaxFramesInFlight].lights->handle(); }
    VkBuffer counts(uint32_t frame) const { return m_slots[frame % Renderer::kMaxFramesInFlight].counts->handle(); }
    VkBuffer indices(uint32_t frame) const { return m_slots[frame % Renderer::kMaxFramesInFlight].indices->handle(); }
    uint32_t count(uint32_t frame) const { return m_slots[frame % Renderer::kMaxFramesInFlight].count; }
    const ClusterGrid& grid() const { return m_grid; }

private:
    struct Slot {
        std::unique_ptr<Buffer> lights, counts, indices;
        uint32_t count = 0;
    };
    VulkanDevice& m_device;
    ClusterGrid m_grid;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Slot m_slots[Renderer::kMaxFramesInFlight];
};

} // namespace kke
