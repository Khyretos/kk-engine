#pragma once

#include "kke/FluidSurface.h"
#include "kke/MeltVolume.h"
#include "kke/Module.h"
#include "kke/ParticleFluid.h"
#include "kke/RigidWorld.h"
#include "kke/SphereImpostors.h"

#include <memory>
#include <vector>

namespace kke { class Application; }

namespace kke_showcase {

// kke_demo's lava station (ACTION_PLAN.md 1.4, issue #12): a spout pours
// lava (kke::ParticleFluid) into a stone basin onto a block that melts
// (kke::MeltVolume), as in melt_demo. The block cycles through ice, wax
// and aluminium; the next one drops in when the block is gone or the
// liquid budget is full. Crates thrown in and the character wading
// through push the lava aside (Jolt bodies as fluid colliders). It only
// simulates while a camera is near: a station nobody looks at costs
// nothing.
class LavaStation {
public:
    static constexpr float kHalf = 1.4f;  // basin inside, half width (m)
    static constexpr float kWall = 0.45f; // basin wall height: low enough to see the block over

    LavaStation(kke::Application& app, const glm::vec3& center);
    ~LavaStation();

    // `near` = someone is close enough to watch.
    void fixedUpdate(float dt, const kke::RigidWorld& world, bool near);
    void update();
    void prepass(const kke::PrepassContext& ctx);
    void render(const kke::RenderContext& ctx);
    void renderShadow(const kke::ShadowRenderContext& ctx);
    void next(); // the next block, fresh lava

    const glm::vec3& center() const { return m_center; }
    const char* blockName() const;
    float blockLeft() const { return m_block ? m_block->solidFraction() : 0.0f; }
    size_t particles() const { return m_fluid ? m_fluid->size() : 0; }
    size_t budget() const { return m_fluid ? m_fluid->capacity() : 0; }
    double stepMs() const { return m_stepMs; }

private:
    void reset();
    kke::Application& m_app;
    glm::vec3 m_center;
    std::unique_ptr<kke::ParticleFluid> m_fluid;
    std::unique_ptr<kke::MeltVolume> m_block;
    std::unique_ptr<kke::SphereImpostorRenderer> m_spheres;
    std::unique_ptr<kke::FluidSurfaceRenderer> m_surface;
    std::unique_ptr<kke::DynamicMeshRenderer> m_blockMesh;
    std::vector<kke::SphereImpostorRenderer::Sphere> m_sphereScratch;
    std::vector<kke::FluidSurfaceRenderer::Particle> m_surfaceScratch;
    std::vector<kke::RigidWorld::BodyBox> m_bodies; // colliders this step
    struct Capsule { glm::vec3 a, b; float r; };
    std::vector<Capsule> m_characters;
    int m_preset = 0;
    float m_emitAccum = 0.0f, m_doneFor = 0.0f;
    uint32_t m_rng = 12345;
    double m_stepMs = 0.0;
};

} // namespace kke_showcase
