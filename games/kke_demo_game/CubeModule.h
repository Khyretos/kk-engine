#pragma once

#include "kke/Module.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"
#include "kke/Texture.h"

#include <memory>

namespace kke_demo {

// This is the module to copy when starting a new gameplay system: it owns
// its own GPU resources (mesh + pipeline), updates its own state each
// frame, and draws itself — nothing about it is special-cased by the
// engine. See the README's "Adding a module" section for the walkthrough.
class CubeModule : public kke::Module {
public:
    const char* name() const override { return "Cube"; }

    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

private:
    std::unique_ptr<kke::Pipeline> m_pipeline;
    // A real, separate pipeline for the shadow pass — see
    // shadow.vert/frag: same vertex layout as the main pipeline (so it
    // binds the exact same kke::Mesh unmodified) but a completely
    // different, much simpler set of shaders/state (no descriptor sets,
    // no fragment output, a different render pass).
    std::unique_ptr<kke::Pipeline> m_shadowPipeline;
    std::unique_ptr<kke::Mesh> m_mesh;
    // A real, generated procedural texture (checkerboard) — see
    // kke::Texture and CubeModule.cpp's own comment on why generated
    // rather than loaded from a file for this first real material-
    // texture consumer. Owns its own small descriptor pool/set,
    // separate from kke::Application's own default-white-texture pool,
    // since this is a genuinely different, real texture no other
    // module shares.
    std::unique_ptr<kke::Texture> m_texture;
    VkDescriptorPool m_texturePool = VK_NULL_HANDLE;
    VkDescriptorSet m_textureDescriptorSet = VK_NULL_HANDLE;
    // Stashed during init() specifically so shutdown() (which, like
    // every Module::shutdown(), takes no parameters -- see Module.h)
    // has a real VkDevice to destroy m_texturePool with.
    VkDevice m_device = VK_NULL_HANDLE;

    bool m_spinning = true;
    float m_spinSpeedDegPerSec = 45.0f;
    float m_accumulatedAngle = 0.0f;
    glm::vec3 m_spinAxis{0.3f, 1.0f, 0.0f};

    // Real, adjustable PBR material properties -- not fixed constants.
    // Defaults chosen to look like a reasonable dielectric (plastic-ish)
    // material out of the box, not the extreme, least-representative
    // corner of the model. See cube.frag's own comment on why these
    // get clamped away from the true 0.0/1.0 extremes before use.
    float m_metallic = 0.1f;
    float m_roughness = 0.4f;
};

} // namespace kke_demo
