#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kke {

class TaskPool;
class MpmWaterGpu;

struct MpmWaterParams {
    // The tank: water stays inside this box, with a wall on every side
    // (leave the top high above the water for an open tank).
    glm::vec3 boundsMin{-1.0f, 0.0f, -1.0f}, boundsMax{1.0f, 2.0f, 1.0f};
    float cellSize = 0.05f;    // grid spacing (m); fill() puts 2 x 2 x 2 particles in a cell
    float density = 1000.0f;   // kg/m^3: what a body floating in it feels
    // How fast pressure travels (m/s). Real water's 1,500 m/s would need a
    // thousand substeps a frame; at 12 a metre of water is squeezed about
    // 7% at the bottom, which reads as water. Higher: stiffer, more substeps.
    float soundSpeed = 12.0f;
    float viscosity = 0.0f;    // kinematic (m^2/s): 0 for water (the method's own smoothing is plenty), ~0.01 for syrup
    // How hard a stretched particle may pull on its neighbours, as the
    // pressure of that much squeeze (1 - J): a little holds the surface
    // together, more makes water stringy. 0 = never pulls.
    float tension = 0.01f;
    float wallFriction = 0.0f; // 0 = walls slip, 1 = water sticks to them
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    float maxSpeed = 15.0f;    // m/s cap (a body hitting it hard can't fling water faster)
    float cfl = 0.4f;          // a substep moves a particle at most this many cells (stability)
    int maxSubsteps = 40;      // per step(); past it, substeps get longer and maxSpeed holds things together
    size_t maxNodes = 8u << 20; // grid budget: the constructor throws past it
};

// A body in the water: boxes and spheres the water pushes and is pushed
// by (crates, barrels, a boat's hull as a box, the tank's pillars). The
// caller fills these in before step() (kke::WaterBodyLink copies them from
// a RigidWorld) and reads deltaVelocity after it.
struct MpmWaterBody {
    bool sphere = false;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 halfExtents{0.5f};   // box; a sphere's radius is halfExtents.x
    glm::vec3 velocity{0.0f}, angularVelocity{0.0f}; // world axes, rad/s
    float mass = 0.0f;             // kg; 0 = the water can't move it (walls, kinematic bodies)
    float friction = 0.0f;         // 0 = water slides along it, 1 = drags it along
    uint32_t user = 0;             // the caller's id (a RigidWorld::BodyId)
    // After step(): what the water did to it over the step, as velocity
    // changes (gravity left out: the body's own physics adds that).
    glm::vec3 deltaVelocity{0.0f}, deltaAngularVelocity{0.0f};
};

// Water as particles on a grid: the Moving Least Squares Material Point
// Method (Hu et al., "A Moving Least Squares Material Point Method with
// Displacement Discontinuity and Two-Way Rigid Body Coupling", SIGGRAPH
// 2018; docs/RESEARCH.md), with APIC transfers (each particle carries its
// velocity's gradient, so swirls survive) and a weakly compressible fluid.
// Where kke::ParticleFluid (Position Based Fluids) is for lava and small
// pours, this is for water in bulk: dam breaks, waves sloshing in a tank,
// splashes, and bodies that float, bob and sink in it.
//
// Per substep: particles -> grid (mass and momentum, plus each particle's
// pressure and swirl), grid velocities (gravity, walls, bodies: water
// moving into a body is stopped there and the body gets that momentum),
// bodies (their share of the substep), grid -> particles (new velocity,
// its gradient, volume). Nothing searches for neighbours: the grid is the
// neighbourhood, which is what makes it suit a GPU.
//
// Explicit, so a substep is short (cfl cells of travel, and pressure
// crossing at most cfl cells); step() takes as many as the frame needs
// (about 14 a 60 Hz frame at the defaults).
//
// On the CPU every pass is split over a TaskPool and the result does not
// depend on the thread count (particles write the grid in blocks, four
// colours of them one after another). On the GPU (setGpu; kke::MpmWaterGpu) the
// particles stay on the GPU and each step reads back positions and
// velocities. Pure C++ without a GPU: tests/test_mpm_water.cpp.
class MpmWater {
public:
    using Params = MpmWaterParams;
    using Body = MpmWaterBody;

    explicit MpmWater(const Params& params = Params{});
    ~MpmWater();
    MpmWater(const MpmWater&) = delete;
    MpmWater& operator=(const MpmWater&) = delete;

    const Params& params() const { return m_params; }
    // Changing the tank or the cell size needs a new MpmWater; these can change any time.
    void setSoundSpeed(float c) { m_params.soundSpeed = c; }
    void setViscosity(float nu) { m_params.viscosity = nu; }
    void setGravity(const glm::vec3& g) { m_params.gravity = g; }

    // One particle (clamped into the tank).
    void add(const glm::vec3& position, const glm::vec3& velocity = glm::vec3(0.0f));
    // Water filling min..max: 2 x 2 x 2 particles a cell, jittered a
    // little so they don't line up. Returns how many it added.
    size_t fill(const glm::vec3& min, const glm::vec3& max, const glm::vec3& velocity = glm::vec3(0.0f));
    void clear();
    // Removes particles for which pred(index) is true (order not kept).
    void removeIf(const std::function<bool(size_t)>& pred);

    std::vector<Body>& bodies() { return m_bodies; }
    const std::vector<Body>& bodies() const { return m_bodies; }
    static constexpr size_t kMaxBodies = 64;

    void setTaskPool(TaskPool* pool) { m_pool = pool; }
    // Steps on the GPU from now on (null: the CPU). The particles move to
    // the GPU at the next step; setGpu(nullptr) brings them back first.
    void setGpu(std::shared_ptr<MpmWaterGpu> gpu);
    bool onGpu() const { return m_gpu != nullptr; }

    void step(float dt);

    size_t size() const { return m_x.size(); }
    float particleMass() const { return m_particleMass; } // kg
    const std::vector<glm::vec3>& positions() const { return m_x; }
    const std::vector<glm::vec3>& velocities() const { return m_v; }
    // Each particle's volume against its rest volume (< 1: squeezed).
    // On the GPU: as of the last download (stepOnCpu, removeIf, setGpu).
    const std::vector<float>& volumes() const { return m_J; }

    struct Stats {
        int substeps = 0;          // in the last step
        double stepMs = 0.0;       // the last step, as the caller waited for it
        float maxSpeed = 0.0f;     // fastest particle after it (m/s)
        uint64_t gpuSteps = 0, gpuLate = 0; // steps on the GPU, and ones it didn't finish in time (skipped)
    };
    const Stats& stats() const { return m_stats; }

    // The grid this water uses (for the GPU and tests).
    glm::ivec3 gridSize() const { return m_dims; }
    glm::vec3 gridOrigin() const { return m_origin; }

    // One step on the CPU whatever setGpu says, from the GPU's state if
    // that has it (KKE_MPM_GPU_CHECK compares the two). The GPU gets the
    // result at its next step.
    void stepOnCpu(float dt);

    // KKE_MPM_GPU_CHECK: one step on both, from the same state, keeping
    // the GPU's. How far apart the particles end up (float rounding and
    // the GPU's fixed-point sums: well under a millimetre a step).
    struct GpuCheck {
        bool done = false;  // false: no GPU (or no water): stepped normally
        size_t particles = 0;
        float maxDifference = 0.0f, meanDifference = 0.0f; // m
        double gpuMs = 0.0, cpuMs = 0.0;
    };
    GpuCheck checkGpuStep(float dt);

private:
    friend class MpmWaterGpu;

    // A body during a step: moved and pushed substep by substep.
    struct Proxy {
        glm::vec3 position, halfExtents, velocity, angularVelocity, invInertia; // invInertia: body axes
        glm::quat rotation;
        float invMass, friction;
        bool sphere;
        glm::vec3 dv{0.0f}, dw{0.0f};
    };

    int substepsFor(float dt) const;
    void beginStep();
    void endStep();
    void substep(float dt);
    void integrateBodies(float dt, const glm::vec3* impulse, const glm::vec3* angularImpulse);
    void stepCpu(float dt, int substeps);

    Params m_params;
    float m_dx, m_invDx, m_particleMass;
    glm::vec3 m_origin; // grid node (0, 0, 0)
    glm::ivec3 m_dims;
    size_t m_nodes;

    // Per particle (caller's order).
    std::vector<glm::vec3> m_x, m_v;
    std::vector<glm::mat3> m_C;
    std::vector<float> m_J;
    std::vector<Body> m_bodies;
    std::vector<Proxy> m_proxies;

    // Per substep scratch.
    std::vector<glm::ivec3> m_base;
    std::vector<glm::mat3> m_affine;
    std::vector<uint32_t> m_order, m_slabStart, m_slabCursor, m_colourBlocks; // particles by block (P2G)
    std::vector<float> m_speed;
    std::vector<int32_t> m_hitBody;     // the body a particle was pushed out of (-1: none), and what it gave it
    std::vector<glm::vec3> m_hitImpulse, m_hitAngular;
    std::vector<glm::vec4> m_grid;      // xyz = momentum, then velocity; w = mass (particle masses)
    std::vector<glm::vec3> m_layerImpulse; // per z layer of the box x body: 2 vec3 (linear, angular)
    std::vector<glm::vec3> m_bodyImpulse;  // per body: 2 vec3, summed over the layers
    glm::ivec3 m_lo{0}, m_hi{-1};       // the nodes this substep touches

    TaskPool* m_pool = nullptr;
    std::shared_ptr<MpmWaterGpu> m_gpu;
    size_t m_gpuValid = 0;              // particles [0, m_gpuValid) are current on the GPU
    bool m_gpuOwns = false;             // the GPU has newer particle state than the CPU arrays' C and J
    uint32_t m_rng = 0x9e3779b9u;
    Stats m_stats;
    float m_lastMaxSpeed = 0.0f;
};

} // namespace kke
