#pragma once

#include "kke/Buffer.h"
#include "kke/Mesh.h"
#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Renderer.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace kke {

class Application;

// Draws a particle liquid as one smooth surface instead of a pile of
// balls: screen-space fluid rendering (van der Laan, Green & Sainz,
// "Screen Space Fluid Rendering with Curvature Flow", 2009 — the approach
// behind NVIDIA Flex's liquids and many game fluids).
//
//   prepass():  1. particles as spheres into an offscreen target: linear
//                  view depth (R32F) + colour/glow (RGBA8), own depth test;
//               2. compute: separable bilateral blur of the depth, a fixed
//                  size in *world* units, stopping at depth jumps, so
//                  touching particles fuse and separate blobs stay apart;
//   draw():     3. full-screen pass in the main render pass: position from
//                  blurred depth, normal from its differences, lit +
//                  Fresnel + glow, real depth written so the scene still
//                  occludes it.
// Cost: one small offscreen pass + 2 blur dispatches per iteration at
// screen resolution (render at half resolution is the obvious knob for
// min-spec — see OPTIMIZATION.md backlog). Targets are recreated when the
// window size changes.
class FluidSurfaceRenderer {
public:
    explicit FluidSurfaceRenderer(Application& app);
    ~FluidSurfaceRenderer();

    struct Particle {
        glm::vec3 center;
        float radius;        // drawn radius (usually ~1.5x the simulation radius)
        glm::vec3 color;     // sRGB
        float glow = 0.0f;
    };
    struct Settings {
        float blurWorldRadius = 0.06f;   // metres: how much neighbouring particles merge
        float depthFalloff = 0.05f;      // metres: depth step that counts as a separate surface
        int blurIterations = 2;
    };
    Settings& settings() { return m_settings; }

    // Call from Module::prepass(), then draw() from Module::render().
    void prepass(const PrepassContext& ctx, const std::vector<Particle>& particles);
    void draw(const RenderContext& ctx);

private:
    struct Image { VkImage image = VK_NULL_HANDLE; VmaAllocation alloc = VK_NULL_HANDLE; VkImageView view = VK_NULL_HANDLE; };
    void createTargets(VkExtent2D extent);
    void destroyTargets();
    Image makeImage(VkExtent2D e, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void destroyImage(Image& img);

    Application& m_app;
    VkDevice m_device;
    Settings m_settings;
    VkExtent2D m_extent{ 0, 0 };
    Image m_depth, m_z, m_zTmp, m_color;
    VkRenderPass m_pass = VK_NULL_HANDLE;
    VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> m_particlePipeline, m_compositePipeline;
    // blur (compute)
    VkDescriptorSetLayout m_blurLayout = VK_NULL_HANDLE, m_sampleLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_blurAB = VK_NULL_HANDLE, m_blurBA = VK_NULL_HANDLE, m_sampleSet = VK_NULL_HANDLE;
    VkPipelineLayout m_blurPipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_blurPipeline = VK_NULL_HANDLE;
    // particle vertices, one buffer per frame in flight
    std::unique_ptr<Buffer> m_buffers[Renderer::kMaxFramesInFlight];
    size_t m_capacity[Renderer::kMaxFramesInFlight] = {};
    std::vector<Vertex> m_scratch;
    bool m_hasContent = false;
    glm::mat4 m_invProj{1.0f}, m_invView{1.0f};
};

} // namespace kke
