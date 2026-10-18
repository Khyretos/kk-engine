#pragma once

#include "kke/Buffer.h"
#include "kke/RayScene.h"
#include "kke/Renderer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <memory>
#include <vector>

namespace kke {

class VulkanDevice;

// Rays for refraction (ENG-F02 part 4, `reflections = kke / classic`): set 3
// of the see-through surfaces' refracting pass (shaders/translucent_refract_rays.frag):
// the frame's kke::RayScene, how each instance looks (kke::RayMaterial, by
// its custom index) and the light for a hit. KKE traces only where the
// screen couldn't show what is behind the surface; classic traces every
// refracting pixel.
class RefractionRays {
public:
    explicit RefractionRays(VulkanDevice& device);
    ~RefractionRays();
    RefractionRays(const RefractionRays&) = delete;
    RefractionRays& operator=(const RefractionRays&) = delete;

    VkDescriptorSetLayout setLayout() const { return m_setLayout; }
    // This frame's set, after `scene` was built for frame slot `frame`.
    // `sunDirection` points towards the sun; `ambient` is the sky's average light.
    VkDescriptorSet update(uint32_t frame, const RayScene& scene, const std::vector<RayMaterial>& materials, const glm::mat4& viewToWorld,
                           const glm::vec3& sunDirection, const glm::vec3& sunColor, const glm::vec3& ambient, bool classic);

private:
    struct Slot {
        std::unique_ptr<Buffer> materials, params;
        size_t materialCapacity = 0; // RayMaterials
    };
    VulkanDevice& m_device;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Slot m_slots[Renderer::kMaxFramesInFlight];
};

} // namespace kke
