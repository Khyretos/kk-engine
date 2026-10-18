#pragma once

#include "kke/Reflections.h"
#include "kke/Renderer.h"

#include "kke/Buffer.h"

#include <glm/glm.hpp>
#include <volk.h>

#include <memory>
#include <vector>

namespace kke {

class RayScene;
struct RayMaterial;
class VulkanDevice;

// What ray tracing needs beyond the screen (ENG-F02): the scene built this
// frame and how each instance looks (an instance's custom index is its
// material's index), the camera's inverse view, the sun and sky that light
// a hit, and how many rays may be cast this frame.
struct ReflectionRays {
    RayScene* scene = nullptr;
    const std::vector<RayMaterial>* materials = nullptr; // per instance (kke::RayMaterial)
    glm::mat4 viewToWorld{1.0f};
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f}; // towards the sun
    glm::vec3 sunColor{0.0f};                  // colour x intensity
    glm::vec3 ambient{0.0f};                   // the sky's average light
    uint32_t budget = 0;
    bool classic = false; // trace every smooth pixel at once, no screen-space march (classic ray tracing)
    bool built = false;   // the scene's top level is built for this frame already (refraction built it first)
};

// What smooth surfaces reflect, found in screen space (ENG-F02): after the
// scene pass, one compute dispatch over a half-resolution image
// (shaders/reflect.comp) marches each smooth surface's reflected ray through
// the frame's depth and writes the light where it meets something, plus the
// pixel's view depth. The next frame's lit shaders read them
// (kke::LightingBuffer, bindings 4 and 5) over the prefiltered sky,
// reprojected with this frame's camera, as kke::ScreenGiPass does for GI.
class ReflectionPass {
public:
    explicit ReflectionPass(VulkanDevice& device);
    ~ReflectionPass();
    ReflectionPass(const ReflectionPass&) = delete;
    ReflectionPass& operator=(const ReflectionPass&) = delete;

    // Sizes the images for a scene of `sceneExtent` pixels: call at the start
    // of the frame, before anything reads them (a resize waits for the GPU).
    void prepare(VkExtent2D sceneExtent);
    // Records the pass into cmd, after Renderer::endScenePass() (the scene's
    // images are readable then). `frame` is the frame-in-flight slot. With
    // `rays` (KKE ray tracing, a ray-query device) rays the march leaves
    // unsettled go on through rays->scene, built here first.
    void record(VkCommandBuffer cmd, uint32_t frame, const Renderer::SceneImages& scene, const glm::mat4& view, const glm::mat4& proj,
                const ReflectionSettings& settings, const ReflectionRays* rays = nullptr);
    // Nothing recorded this frame (reflections off, split screen): the next frame
    // must not read what is there.
    void invalidate() { m_ready = false; }
    // The images hold a frame's result, made with viewProj().
    bool ready() const { return m_ready; }
    VkImageView light() const { return m_light.view; } // rgb = reflected light, a = 1 where found
    VkImageView depth() const { return m_depth.view; }
    VkSampler sampler() const { return m_linear; }
    const glm::mat4& viewProj() const { return m_viewProj; }
    glm::vec2 size() const { return glm::vec2(static_cast<float>(m_extent.width), static_cast<float>(m_extent.height)); }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    void ensureImages(VkExtent2D full);
    void destroyImages();
    VkPipeline pipeline(bool multisampled);
    // The ray-query variant (shaders/ray_reflect*.comp): its own set layout, made on first use.
    void createRayPath();
    VkPipeline rayPipeline(bool multisampled);
    struct RaySlot {
        std::unique_ptr<Buffer> materials, budget, params;
        size_t materialCapacity = 0; // RayMaterials
    };

    VulkanDevice& m_device;
    VkSampler m_nearest = VK_NULL_HANDLE, m_linear = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE }; // single-sampled depth, multisampled
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_sets[Renderer::kMaxFramesInFlight] = {};
    Image m_light, m_depth;
    VkExtent2D m_extent{};
    bool m_fresh = true; // the images are UNDEFINED: no reads to wait for
    bool m_ready = false;
    glm::mat4 m_viewProj{1.0f};
    VkDescriptorSetLayout m_raySetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_rayLayout = VK_NULL_HANDLE;
    VkPipeline m_rayPipelines[2] = { VK_NULL_HANDLE, VK_NULL_HANDLE };
    VkDescriptorPool m_rayPool = VK_NULL_HANDLE;
    VkDescriptorSet m_raySets[Renderer::kMaxFramesInFlight] = {};
    RaySlot m_raySlots[Renderer::kMaxFramesInFlight];
};

} // namespace kke
