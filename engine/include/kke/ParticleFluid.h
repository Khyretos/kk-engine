#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace kke {

struct ParticleFluidParams {
    float radius = 0.05f;             // particle radius (m); kernel h = 4 * radius
    float restDensity = 1000.0f;      // kg/m^3
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    int iterations = 2;               // density solver iterations per substep
    int substeps = 1;                 // per step() call; 2 helps fast thin streams
    float viscosityHot = 0.02f;       // XSPH factor at >= hotTemperature (0..1)
    float viscosityCold = 0.15f;      // XSPH factor at <= solidifyTemperature (keep <= ~0.15: higher makes blobs move rigidly)
    float hotTemperature = 1200.0f;   // degrees C
    float solidifyTemperature = -1e9f;// particles colder than this freeze (lava crust); default never
    float ambientTemperature = 20.0f;
    float coolingRate = 0.0f;         // fraction of (T - ambient) lost per second
    float heatDiffusion = 2.0f;       // neighbour temperature exchange per second
    float groundY = 0.0f;             // collision plane (y); set to -inf for none
    float friction = 0.02f;           // tangential damping on contact (0..1); keep low for liquids, higher = sand-like piles
    float maxSpeed = 12.0f;           // m/s cap: stops rare solver spikes becoming fountains
    glm::vec3 boundsMin{-1e9f}, boundsMax{1e9f}; // optional container walls
};

// Per-particle-material behaviour (up to 8 materials): lava and the water
// melting out of an ice block share one simulation but not one viscosity
// or freezing point. Material 0 uses ParticleFluidParams' own values
// unless set.
struct FluidMaterial {
    float viscosityHot = 0.02f;
    float viscosityCold = 0.15f;
    float hotTemperature = 1200.0f;
    float solidifyTemperature = -1e9f; // freezes below this (never by default)
};

// A particle liquid: Position Based Fluids (Macklin & Müller, "Position
// Based Fluids", SIGGRAPH 2013) — the method behind NVIDIA Flex and many
// game liquids. Stable at large time steps, no pressure explosions, and it
// "feels" right long before it is physically exact, which is the goal.
//
// Per step: gravity -> predict positions -> a few Jacobi iterations that
// push each particle's density back to rest density -> collisions ->
// velocity from the position change -> XSPH viscosity (neighbours share
// velocity: thick lava, runny water). Each particle also carries a
// temperature that diffuses between neighbours and cools toward the air;
// viscosity rises as it cools (lava crusts over and stops), and below
// `solidifyTemperature` a particle is solid: heavily damped and moving
// with its neighbours (crust), but still falling if unsupported.
//
// Performance (docs/OPTIMIZATION.md): particles are counting-sorted by grid
// cell every step, so neighbours are contiguous in memory and the
// neighbour search touches 27 small ranges — O(n), no allocations after
// warm-up, single-threaded. ~2,000 particles fit in ~3 ms on one core.
// Pure CPU and render-agnostic: unit-tested in tests/test_particle_fluid.cpp.
class ParticleFluid {
public:
    using Params = ParticleFluidParams;

    // Solid obstacles: return a signed distance (negative = inside) and the
    // outward normal at p. Particles inside are pushed out along it.
    using Collider = std::function<float(const glm::vec3& p, glm::vec3& normal)>;

    explicit ParticleFluid(const Params& params = Params{}, size_t maxParticles = 4000);

    Params& params() { return m_params; }
    const Params& params() const { return m_params; }

    // Adds a particle; returns false (and drops it) at the capacity cap —
    // the fluid's budget (docs/OPTIMIZATION.md rule 5).
    bool add(const glm::vec3& position, const glm::vec3& velocity, float temperature, uint8_t material = 0);
    void clear();
    // Removes particles for which `pred(index)` is true (order not kept).
    void removeIf(const std::function<bool(size_t)>& pred);

    void setMaterial(uint8_t id, const FluidMaterial& m) { if (id < 8) { m_materials[id] = m; m_materialSet[id] = true; } }
    void addCollider(Collider c) { m_colliders.push_back(std::move(c)); }
    void clearColliders() { m_colliders.clear(); }

    void step(float dt);

    size_t size() const { return m_pos.size(); }
    size_t capacity() const { return m_capacity; }
    const std::vector<glm::vec3>& positions() const { return m_pos; }
    const std::vector<glm::vec3>& velocities() const { return m_vel; }
    std::vector<float>& temperatures() { return m_temp; }
    const std::vector<float>& temperatures() const { return m_temp; }
    const std::vector<uint8_t>& materials() const { return m_material; }
    // Density of each particle from the last step (kg/m^3), for tests/debug.
    const std::vector<float>& densities() const { return m_density; }
    float kernelRadius() const { return m_h; }

private:
    void buildGrid(const std::vector<glm::vec3>& p);
    template <typename F> void forNeighbours(size_t i, const std::vector<glm::vec3>& p, F&& f) const;
    void collide(glm::vec3& p, const glm::vec3& prev) const;

    Params m_params;
    size_t m_capacity;
    float m_h = 0.2f, m_mass = 1.0f;
    float m_poly6 = 0.0f, m_spikyGrad = 0.0f;

    std::vector<glm::vec3> m_pos, m_vel, m_pred, m_delta;
    std::vector<float> m_temp, m_density, m_lambda;
    std::vector<uint8_t> m_material;
    std::vector<Collider> m_colliders;
    FluidMaterial m_materials[8];
    bool m_materialSet[8] = {};
    FluidMaterial materialOf(uint8_t id) const;

    // Counting-sorted spatial hash: particles of cell c are
    // m_sorted[m_cellStart[c] .. m_cellStart[c+1]).
    std::vector<uint32_t> m_cellOf, m_cellStart, m_sorted;
    std::vector<uint32_t> m_nbrStart, m_nbr;   // per-substep neighbour lists (flat)
    uint32_t m_tableSize = 0;
    std::vector<glm::vec3> m_scratchVel;
    std::vector<float> m_scratchTemp;
};

} // namespace kke
