#pragma once

#include "kke/MeltVolume.h"
#include "kke/Module.h"
#include "kke/ParticleFluid.h"
#include "kke/FluidSurface.h"
#include "kke/SphereImpostors.h"

#include <memory>
#include <string>
#include <vector>

namespace kke_melt {

// Pour lava on a block and watch it melt. The block's material decides
// what happens: ice melts into water that runs off (and chills the lava
// into crust), wax and chocolate soften and slump and re-solidify as they
// cool, aluminium needs real heat and glows as it goes. Built from engine
// pieces: kke::ParticleFluid (the liquids), kke::MeltVolume (the solid),
// kke::SphereImpostorRenderer / DynamicMeshRenderer (drawing).
class MeltDemoModule : public kke::Module {
public:
    const char* name() const override { return "MeltDemo"; }
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void prepass(const kke::PrepassContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

    void reset();

    struct BlockPreset {
        const char* name;
        float meltingPoint, startTemperature, heatCapacity, meltRate, conduction;
        glm::vec3 solidColor, liquidColor;
        kke::FluidMaterial liquid;
        bool incandescent;  // glows when hot (metal)
    };

private:
    kke::Application* m_app = nullptr;
    std::unique_ptr<kke::ParticleFluid> m_fluid;
    std::unique_ptr<kke::MeltVolume> m_block;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::DynamicMeshRenderer> m_blockMesh, m_ground;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;
    std::unique_ptr<kke::FluidSurfaceRenderer> m_surface;
    std::vector<kke::FluidSurfaceRenderer::Particle> m_surfaceScratch;
    bool m_smoothSurface = true;   // screen-space fluid surface vs. raw particles
    int m_preset = 0;
    bool m_pouring = true;
    float m_pourRate = 120.0f;      // particles per second
    float m_lavaTemperature = 1200.0f;
    float m_emitAccum = 0.0f;
    uint32_t m_rng = 12345;
    double m_fluidMs = 0.0, m_meltMs = 0.0;
    size_t m_meshTris = 0;
};

} // namespace kke_melt
