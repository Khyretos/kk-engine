#pragma once

#include "kke/Sky.h"

#include <volk.h>
#include <glm/glm.hpp>

#include <memory>

namespace kke {

class Application;
class Pipeline;
class Texture;

// Draws kke::Lighting::sky behind every view (shaders/sky.*): one
// full-screen triangle at the far plane, before any module renders.
// Owned by Application and made the first time a sky is switched on, so a
// game without one pays nothing. It also turns the sky into what the lit
// shaders need (SkyResolver: colours, ambient light, fog colour).
class SkyRenderer {
public:
    explicit SkyRenderer(Application& app);
    ~SkyRenderer();

    SkyRenderer(const SkyRenderer&) = delete;
    SkyRenderer& operator=(const SkyRenderer&) = delete;

    // Once per frame, before recording: resolves the sky and, when its
    // image changed, uploads the new one (waiting for the GPU to be idle:
    // switching skies is rare, a mood change, not a per-frame thing).
    const SkyEnvironment& prepare(const Sky& sky, const Fog& fog, const glm::vec3& flatAmbient);

    // Inside the scene pass, first thing for a view.
    void draw(VkCommandBuffer cmd, const glm::mat4& view, const glm::mat4& proj, VkDescriptorSet lighting);

    // The sky prefiltered for reflections (SkyEnvironment::reflections) on the
    // GPU, one mip per level, after prepare(); null while the sky has none.
    const Texture* reflections() const { return m_reflections.get(); }
    uint32_t reflectionLevels() const { return m_reflectionLevels; }

private:
    void bindImage(const Texture& texture);

    Application& m_app;
    SkyResolver m_resolver;
    const SkyEnvironment* m_env = nullptr;
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Texture> m_placeholder; // a 1x1 texture while no image sky is on
    std::unique_ptr<Texture> m_image;
    std::shared_ptr<const SkyImage> m_imageSource;
    std::unique_ptr<Texture> m_reflections;
    std::shared_ptr<const EnvMap> m_reflectionsSource; // what m_reflections was made from
    uint32_t m_reflectionLevels = 0;
    VkDescriptorSetLayout m_setLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
};

} // namespace kke
