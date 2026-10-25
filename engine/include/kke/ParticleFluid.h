#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <utility>

namespace kke {

class TaskPool;

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

// Finds the fluid's neighbours somewhere other than this CPU thread
// (kke::FluidGpu: on the GPU). The fluid hands over its particles in cell
// order and the counting-sorted grid; the answer is every slot's
// neighbours within sqrt(radius2), in the order the CPU finds them,
// `stride` words per slot.
class FluidNeighbourSearch {
public:
    struct Query {
        const glm::vec3* positions = nullptr; // per slot (cell order)
        size_t count = 0;
        const uint32_t* cellStart = nullptr;  // tableSize + 1 entries
        uint32_t tableSize = 0;               // a power of two
        float invCellSize = 0.0f;
        float radius2 = 0.0f;
    };
    virtual ~FluidNeighbourSearch() = default;
    // Fills list (count * stride words: slot s's neighbours start at
    // s * stride) and counts (one per slot). False: the CPU must search
    // this time (no answer in time, or a slot had more than `stride`
    // neighbours; the search may then raise stride for next time).
    virtual bool search(const Query& q, uint32_t& stride, std::vector<uint32_t>& list, std::vector<uint32_t>& counts) = 0;
    // Also search on the CPU and count differences (testing; slow).
    virtual bool checkAgainstCpu() const { return false; }
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
// Performance (docs/OPTIMIZATION.md #18, #44): particles are counting-sorted
// by grid cell every substep and the solver runs on copies in that order,
// so a cell's particles sit side by side in memory and the neighbour
// search walks 27 small contiguous ranges — O(n), no allocations after
// warm-up. Every pass is per particle, so with setTaskPool() each one is
// split over the pool's threads (the results do not depend on the thread
// count). Pure CPU and render-agnostic: unit-tested in
// tests/test_particle_fluid.cpp.
class ParticleFluid {
public:
    using Params = ParticleFluidParams;

    // Solid obstacles: return a signed distance (negative = inside) and the
    // outward normal at p. Particles inside are pushed out along it.
    // With a task pool set, colliders are called from several threads at
    // once: they must only read shared state.
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

    // Splits each step's passes over the pool's threads (null: one thread,
    // the default). The pool must outlive the fluid or be reset first.
    void setTaskPool(TaskPool* pool) { m_pool = pool; }

    // Neighbour search elsewhere (kke::FluidGpu::create(app.device()); null:
    // on the CPU, the default). Used from 1,024 particles up, and only while
    // it is the faster of the two: the CPU's search is timed every 64th
    // substep, as kke::ClothGpu does. Its lists match the CPU's but for a
    // rare pair right at the kernel's edge (float rounding).
    void setNeighbourSearch(std::shared_ptr<FluidNeighbourSearch> search) { m_search = std::move(search); }
    struct SearchStats {
        uint64_t elsewhere = 0;  // substeps whose lists came from the search set above
        uint64_t declined = 0;   // ...that it handed back to the CPU
        uint64_t checks = 0, differed = 0; // checkAgainstCpu(): searches compared, and how many differed
        double elsewhereMs = 0.0, cpuMs = 0.0; // averages, as the step waited for them (0 = not timed yet)
        bool onCpu = false;      // the CPU is the faster here, so it searches
    };
    const SearchStats& searchStats() const { return m_searchStats; }

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
    int bucketsAround(const glm::ivec3& c, uint32_t (&out)[27]) const;
    void findNeighbours(size_t begin, size_t end, std::vector<uint32_t>& list);
    void buildNeighbourLists();
    void buildNeighbourListsOnCpu();
    void collide(glm::vec3& p, const glm::vec3& prev) const;

    static constexpr size_t kGrain = 256; // particles per task, at least

    Params m_params;
    size_t m_capacity;
    float m_h = 0.2f, m_mass = 1.0f;
    float m_poly6 = 0.0f, m_spikyGrad = 0.0f;

    // Per particle, in the order callers index.
    std::vector<glm::vec3> m_pos, m_vel, m_predIn;
    std::vector<float> m_temp, m_density;
    std::vector<uint8_t> m_material;
    // Per slot (cell order) while a substep runs.
    std::vector<glm::vec3> m_pred, m_sPos, m_sVel, m_delta;
    std::vector<float> m_sTemp, m_sDensity, m_lambda;
    std::vector<uint8_t> m_sMat;
    std::vector<Collider> m_colliders;
    FluidMaterial m_materials[8];
    bool m_materialSet[8] = {};
    FluidMaterial materialOf(uint8_t id) const;

    // Counting-sorted spatial hash: slots of cell bucket c are
    // m_cellStart[c] .. m_cellStart[c+1]; slot s holds particle m_sorted[s].
    std::vector<uint32_t> m_cellOf, m_cellStart, m_sorted, m_cursor;
    // Per-substep neighbour lists (flat, slot indices): slot s's are
    // m_nbr[m_nbrStart[s] .. + m_nbrCount[s]).
    std::vector<uint32_t> m_nbrStart, m_nbrCount, m_nbr;
    std::vector<std::vector<uint32_t>> m_chunkLists; // one per task while lists are built
    std::vector<uint32_t> m_chunkBase;
    TaskPool* m_pool = nullptr;
    std::shared_ptr<FluidNeighbourSearch> m_search;
    SearchStats m_searchStats;
    uint64_t m_searchCalls = 0;
    uint32_t m_searchStride = 64;
    int m_searchDeclinedInARow = 0;
    std::vector<uint32_t> m_checkList, m_checkStart, m_checkCount; // checkAgainstCpu() scratch
    uint32_t m_tableSize = 0;
    std::vector<glm::vec3> m_scratchVel;
    std::vector<float> m_scratchTemp;
};

} // namespace kke
