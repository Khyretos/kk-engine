#pragma once

#include "kke/FluidSurface.h"
#include "kke/Module.h"
#include "kke/MpmWater.h"
#include "kke/MpmWaterGpu.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"
#include "kke/TaskPool.h"
#include "kke/WaterBodyLink.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kke {
class RigidBodyModule;
}

namespace kke_water {

// A glass-fronted tank of water: a dam breaks, the water sloshes from wall
// to wall, crates float, bob and get carried along, a stone one sinks, and
// a paddle makes waves. Built from engine pieces: kke::MpmWater (MLS-MPM
// water, on the GPU when there is one), kke::WaterBodyLink (the crates are
// Jolt bodies and the water pushes them and is pushed back),
// kke::FluidSurfaceRenderer in clear-water mode (see-through, tinted by
// depth, foam where it churns).
class WaterDemoModule : public kke::Module {
public:
    const char* name() const override { return "WaterDemo"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void fixedUpdate(const kke::FixedUpdateContext& ctx) override;
    void update(const kke::UpdateContext& ctx) override;
    void prepass(const kke::PrepassContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;

    void reset();
    void dropCrate(bool stone);
    void makeWave();

private:
    struct Crate {
        kke::RigidWorld::BodyId body;
        glm::vec3 halfExtents, color;
    };
    void defineInput();
    void readInput();
    void buildPanel();
    void buildTank();
    void clearCrates();

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_bodies = nullptr;
    std::unique_ptr<kke::TaskPool> m_pool;
    std::shared_ptr<kke::MpmWaterGpu> m_gpu;
    std::unique_ptr<kke::MpmWater> m_water;
    kke::WaterBodyLink m_link;
    std::vector<Crate> m_crates;
    kke::RigidWorld::BodyId m_paddle = kke::RigidWorld::kNoBody;
    float m_waveTime = -1.0f;   // seconds into the paddle's stroke (< 0: resting)

    std::unique_ptr<kke::FluidSurfaceRenderer> m_surface;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::DynamicMeshRenderer> m_tank, m_cube;
    std::vector<kke::FluidSurfaceRenderer::Particle> m_surfaceScratch;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;

    float m_cell = 0.0f;          // the water's grid spacing (KKE_WATER_CELL; by default finer on a GPU)
    bool m_smoothSurface = true;  // clear water vs. the raw particles
    float m_viscosity = 0.0f;
    float m_stiffness = 12.0f;    // the water's sound speed (m/s): higher = less squeeze and bounce, more substeps
    float m_cohesion = 0.01f;     // how hard stretched water pulls itself together (0 = none)
    float m_smoothing = 1.2f;     // the surface blur, in cells: lower = more separate drops, higher = one smooth mass
    bool m_check = false;         // KKE_MPM_GPU_CHECK
    uint32_t m_rng = 2024;
    uint64_t m_steps = 0;
    double m_stepMs = 0.0;        // smoothed, for the panel
    kke::MpmWater::GpuCheck m_lastCheck;
};

} // namespace kke_water
