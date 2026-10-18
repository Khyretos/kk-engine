#pragma once

#include "kke/Occlusion.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <memory>

namespace kke {

class Buffer;
class VulkanDevice;

// Occlusion culling's depth map (ENG-F13, kke/Occlusion.h): after the 3D
// pass one compute dispatch (shaders/depth_map.glsl) writes the frame's
// farthest depth per 8x8 block into memory the CPU can read, one buffer per
// frame in flight. read() gives the map that buffer holds once its frame's
// fence has been waited for: the one drawn two frames ago. Owned by
// kke::Renderer.
class DepthMapPass {
public:
    explicit DepthMapPass(VulkanDevice& device);
    ~DepthMapPass();
    DepthMapPass(const DepthMapPass&) = delete;
    DepthMapPass& operator=(const DepthMapPass&) = delete;

    // Records the map of `depth` (DEPTH_STENCIL_READ_ONLY, `samples` samples, `extent`), drawn with
    // `viewProj`, into frame slot `frame`'s buffer.
    void record(VkCommandBuffer cmd, uint32_t frame, VkImageView depth, uint32_t samples, VkExtent2D extent, const glm::mat4& viewProj);
    // The map slot `frame` holds (call after that frame's fence wait, before record() for it), or null
    // when it holds none. Read once per frame; the same pointer until the next call.
    const DepthMap* read(uint32_t frame);
    // Forget every map (the scene changed all at once: a level load, a camera cut).
    void forget();

private:
    struct Slot {
        std::unique_ptr<Buffer> buffer;
        size_t capacity = 0; // floats
        glm::uvec2 size{0};
        glm::mat4 viewProj{1.0f};
        bool written = false;
    };
    VkPipeline pipeline(bool multisampled);

    VulkanDevice& m_device;
    VkSampler m_nearest = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Slot m_slots[Renderer::kMaxFramesInFlight];
    DepthMap m_map;
};

} // namespace kke
