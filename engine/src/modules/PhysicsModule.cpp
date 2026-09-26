#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Log.h"
#include "kke/Application.h"
#include "kke/VulkanCheck.h"
#include "kke/BenchmarkReport.h"
#include "kke/VulkanDevice.h"
#include "kke/VoronoiFracture.h"

#include <imgui.h>
#include <AMD_FEMFX.h>
#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <algorithm>
#include <array>
#include <map>
#include <chrono>
#include <SDL3/SDL.h>
#if defined(__linux__)
#include <sched.h>
#endif

// FEMFX declares these extern (FEMFXCommon.h) and expects the
// application to define them — an allocator hook, the same pattern as
// the task system callbacks below. Deliberately at GLOBAL scope, not
// inside namespace AMD or kke — matching exactly how FEMFX's own header
// declares them (verified directly: qualifying these as AMD:: produced
// a real compile error, "should have been declared inside 'AMD'",
// which is what confirmed the declaration is global, not guessed).
void* FmAlignedMalloc(size_t size, size_t alignment) {
    // std::aligned_alloc requires size to be a multiple of alignment —
    // a real C++17 requirement, not FEMFX's own. FEMFX always passes a
    // type's natural alignment as the second argument, so rounding size
    // up is correct and harmless regardless of what's being allocated.
    size_t roundedSize = ((size + alignment - 1) / alignment) * alignment;
    #if defined(_WIN32)
    return _aligned_malloc(roundedSize, alignment); // no std::aligned_alloc in the Windows C runtime
#else
    return std::aligned_alloc(alignment, roundedSize);
#endif
}

void FmAlignedFree(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

namespace kke {

namespace {

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// A real thread pool, replacing the earlier synchronous stand-in —
// verified standalone (a separate test program, same scene/tet setup
// as this file's own defaults) before being wired in here, same
// discipline as everything else in this class's history.
//
// Design, and why: FEMFX indexes a per-worker scratch buffer array
// directly by whatever GetTaskSystemWorkerIndex() returns — confirmed
// by reading FEMFXSimulate.cpp directly, not assumed:
// scene->threadTempMemoryBuffer->buffers[workerIndex]. That array is
// sized to EXACTLY numWorkerThreads, not numWorkerThreads+1 (confirmed
// in FEMFXThreadTempMemory.cpp: numBuffers = params.numWorkerThreads).
// Two threads returning the same index would race on the same memory.
// This pool reserves index 0 for the main thread itself, permanently,
// and gives real pool threads indices 1..N-1 — every possible caller,
// whichever thread FEMFX's own internal task-chaining ends up running
// work on, has a stable, unique index for as long as that thread
// exists.
//
// A real, non-obvious bug found and fixed while verifying this
// standalone: with numWorkers==1 (a real, legitimate configuration —
// it's what every prior verified run in this class's history used),
// the pool creates zero real worker threads. Queuing a task in that
// configuration deadlocks forever, since nothing would ever service
// the queue — hit as an actual hang under `timeout`, not anticipated.
// Fixed by falling back to synchronous inline execution specifically
// when numWorkers<=1, matching the exact behavior the old stand-in
// always had for that case.
class ThreadPool {
public:
    explicit ThreadPool(int totalWorkers) : m_numWorkers(totalWorkers) {
        tl_workerIndex = 0; // main thread claims index 0, permanently
        for (int i = 1; i < totalWorkers; ++i) {
            m_threads.emplace_back([this, i] {
                tl_workerIndex = i;
                workerLoop();
            });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stopping = true;
        }
        m_cv.notify_all();
        for (auto& t : m_threads) t.join();
    }

    int numWorkers() const { return m_numWorkers; }

    void submit(std::function<void()> task) {
        if (m_numWorkers <= 1) {
            // See the class comment — this is the deadlock fix, not
            // an optimization: with zero pool threads, queuing would
            // starve forever.
            task();
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_tasks.push(std::move(task));
        }
        m_cv.notify_one();
    }

    static thread_local int tl_workerIndex;

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
                if (m_stopping && m_tasks.empty()) return;
                task = std::move(m_tasks.front());
                m_tasks.pop();
            }
            task();
        }
    }

    int m_numWorkers;
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stopping = false;
};
thread_local int ThreadPool::tl_workerIndex = -1;

ThreadPool* g_pool = nullptr; // set in PhysicsModule::init(), read by the callbacks below
std::unique_ptr<ThreadPool> g_poolOwner; // actual owner — see PhysicsModule::init()/shutdown()

// Real synchronization now — not a no-op, since tasks can genuinely
// still be pending when something waits on them (unlike the old
// stand-in, where everything had already run synchronously by the
// time anyone called Wait).
struct SyncEvent {
    std::mutex mutex;
    std::condition_variable cv;
    bool triggered = false;
};

int GetTaskSystemNumThreads() { return g_pool->numWorkers(); }
int GetTaskSystemWorkerIndex() { return ThreadPool::tl_workerIndex; }

void SubmitAsyncTask(const char* /*name*/, AMD::FmTaskFuncCallback func, void* data,
                      int32_t begin, int32_t end) {
    g_pool->submit([func, data, begin, end] { func(data, begin, end); });
}

AMD::FmSyncEvent* CreateSyncEvent() {
    return reinterpret_cast<AMD::FmSyncEvent*>(new SyncEvent());
}

void DestroySyncEvent(AMD::FmSyncEvent* event) {
    delete reinterpret_cast<SyncEvent*>(event);
}

void WaitForSyncEvent(AMD::FmSyncEvent* event) {
    auto* e = reinterpret_cast<SyncEvent*>(event);
    std::unique_lock<std::mutex> lock(e->mutex);
    e->cv.wait(lock, [e] { return e->triggered; });
}

void TriggerSyncEvent(AMD::FmSyncEvent* event) {
    auto* e = reinterpret_cast<SyncEvent*>(event);
    {
        std::lock_guard<std::mutex> lock(e->mutex);
        e->triggered = true;
    }
    e->cv.notify_all();
}

// A small fixed palette so multiple spawned objects are visually
// distinguishable from each other, cycled by handle — not tied to
// material in any way yet (that would need real per-material color
// mapping, future work), just enough to tell objects apart on screen.
const glm::vec3 kColorPalette[] = {
    {0.45f, 0.30f, 0.15f}, // wood
    {0.55f, 0.55f, 0.60f}, // stone-ish grey
    {0.70f, 0.15f, 0.10f}, // brick-ish red
    {0.20f, 0.45f, 0.25f}, // mossy green
};

// File scope, not local to init() — render() needs it too, for both
// the ground and every spawned object's own push constants. A real
// bug during this file's own first lighting-support pass: this used
// to be declared inside init() only, which compiled fine there but
// left render() unable to see it at all.
struct PhysicsPushConstants { glm::mat4 model; float metallic; float roughness; };
struct ShadowPushConstants { glm::mat4 lightViewProj; glm::mat4 model; };

} // namespace

// The actual scale mismatch, found empirically rather than assumed: the
// physics scene uses real units (gravity=9.88 m/s^2, an object falling
// from 5 units up, landing on a 100-unit-wide floor) — all correct and
// necessary for the simulation to behave physically. The generic
// kke_demo_game's camera orbits at a distance of 3.5 units around a
// *unit* cube (see OrbitCameraModule), so rendering physics objects at
// their true scale there filled nearly the entire view with one flat
// color — not a rendering bug, just two very different scales sharing
// one camera. That's exactly why renderScale is a constructor
// parameter (m_renderScale), not a fixed constant here anymore: a demo
// with its own camera suited to real physics scale (see
// games/physics_demo) can pass 1.0 and skip this workaround entirely.
// The simulation itself is completely untouched by this either way —
// it only ever affects render-time positions.
// A ball made from a grid box: every vertex of a cells^3 cube grid is
// mapped onto the ball with the standard "spherified cube" formula
// (x' = x * sqrt(1 - y^2/2 - z^2/2 + y^2 z^2 / 3), and so on), which is
// smooth and one-to-one, so every tetrahedron stays valid (positive
// volume) — just stretched near the cube's former corners. Cheaper and
// simpler than true sphere tetrahedralization, and good enough
// for a bouncing rubber ball.
namespace {
// kke::Material -> FmTetMaterialParams, the actual bridge.
AMD::FmTetMaterialParams toFemfx(const Material& material) {
    AMD::FmTetMaterialParams p;
    p.restDensity = material.density;
    p.youngsModulus = material.stiffness;
    p.poissonsRatio = material.poissonsRatio;
    p.plasticYieldThreshold = material.plasticYieldThreshold;
    p.plasticCreep = material.plasticCreep;
    p.fractureStressThreshold = material.fractureStressThreshold;
    return p;
}

// xorshift, [0,1): portable, so a seed places things the same everywhere.
struct Rng01 {
    uint32_t s;
    explicit Rng01(uint32_t seed) : s(seed ? seed : 1u) {}
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s >> 8) * (1.0f / 16777216.0f); }
};
} // namespace

TetMeshData PhysicsModule::buildSphere(int cells, float radius) {
    TetMeshData mesh = buildGridBox(cells, cells, cells, 2.0f, 2.0f, 2.0f); // unit cube [-1,1]^3
    for (glm::vec3& v : mesh.vertices) {
        glm::vec3 s = v * v;
        glm::vec3 p(v.x * std::sqrt(std::max(0.0f, 1.0f - s.y / 2.0f - s.z / 2.0f + s.y * s.z / 3.0f)),
                    v.y * std::sqrt(std::max(0.0f, 1.0f - s.z / 2.0f - s.x / 2.0f + s.z * s.x / 3.0f)),
                    v.z * std::sqrt(std::max(0.0f, 1.0f - s.x / 2.0f - s.y / 2.0f + s.x * s.y / 3.0f)));
        v = p * radius;
    }
    return mesh;
}

TetMeshData PhysicsModule::buildGridBox(int cellsX, int cellsY, int cellsZ, float sizeX, float sizeY, float sizeZ) {
    // Real per-axis cell counts and dimensions, not a cube-only
    // generator with scale hacked on afterward -- see this function's
    // own header comment for why one general function serves every
    // new scene's shape needs.
    TetMeshData box;
    int dimX = cellsX + 1, dimY = cellsY + 1, dimZ = cellsZ + 1;
    auto vertIndex = [&](int x, int y, int z) { return (z * dimY + y) * dimX + x; };
    for (int z = 0; z < dimZ; ++z) {
        for (int y = 0; y < dimY; ++y) {
            for (int x = 0; x < dimX; ++x) {
                // Centered on the origin on every axis (not just
                // resting on Y=0) -- the caller's own spawn position
                // is what actually places this in the world, so this
                // shape's own local origin should be its geometric
                // center, the natural point to rotate or offset it
                // around later (e.g. the car scene orienting it toward
                // a wall).
                box.vertices.push_back({
                    (static_cast<float>(x) / cellsX - 0.5f) * sizeX,
                    (static_cast<float>(y) / cellsY - 0.5f) * sizeY,
                    (static_cast<float>(z) / cellsZ - 0.5f) * sizeZ,
                });
            }
        }
    }
    for (int cz = 0; cz < cellsZ; ++cz) {
        for (int cy = 0; cy < cellsY; ++cy) {
            for (int cx = 0; cx < cellsX; ++cx) {
                uint32_t v0 = vertIndex(cx, cy, cz), v1 = vertIndex(cx + 1, cy, cz);
                uint32_t v2 = vertIndex(cx + 1, cy + 1, cz), v3 = vertIndex(cx, cy + 1, cz);
                uint32_t v4 = vertIndex(cx, cy, cz + 1), v5 = vertIndex(cx + 1, cy, cz + 1);
                uint32_t v6 = vertIndex(cx + 1, cy + 1, cz + 1), v7 = vertIndex(cx, cy + 1, cz + 1);
                box.tets.push_back({v0, v1, v2, v6});
                box.tets.push_back({v0, v2, v3, v6});
                box.tets.push_back({v0, v3, v7, v6});
                box.tets.push_back({v0, v7, v4, v6});
                box.tets.push_back({v0, v4, v5, v6});
                box.tets.push_back({v0, v5, v1, v6});
            }
        }
    }
    return box;
}

void PhysicsModule::init(Application& app) {
    m_app = &app;

    // Real thread count now, not hardcoded to 1 — std::thread::
    // hardware_concurrency() can legitimately return 0 (meaning
    // "unknown," per the standard, not "zero cores"), so that's
    // explicitly guarded rather than silently produced as numWorkers=0
    // (which the pool wasn't designed for and shouldn't need to be).
    // On a genuinely single-core machine this correctly resolves to 1,
    // which the pool's own fallback path (see ThreadPool::submit)
    // handles as synchronous execution, not a degraded/broken
    // multithreaded path.
    unsigned int hwThreads = std::thread::hardware_concurrency();
#if defined(__linux__)
    // hardware_concurrency() reports every core in the machine, ignoring
    // CPU affinity — so a process pinned to one core (taskset, a
    // container CPU limit, or the min-spec emulation in
    // PERFORMANCE_NOTES.md) would still start one worker per core, all
    // fighting over the same core. Count the cores we may actually use.
    cpu_set_t affinity;
    if (sched_getaffinity(0, sizeof(affinity), &affinity) == 0) {
        hwThreads = static_cast<unsigned int>(CPU_COUNT(&affinity));
    }
#endif
    int numWorkers = (hwThreads == 0) ? 1 : static_cast<int>(hwThreads);
    // The resource governor's share of those (all of them only with
    // "use everything"); KKE_PHYSICS_THREADS still overrides.
    numWorkers = std::max(1, std::min(numWorkers, app.resourceBudget().workerThreads));
    m_hardwareThreads = std::thread::hardware_concurrency();
    if (const char* forced = std::getenv("KKE_PHYSICS_THREADS")) {
        int n = std::atoi(forced);
        if (n > 0) numWorkers = n;
    }
    m_workerThreads = numWorkers;
    g_poolOwner = std::make_unique<ThreadPool>(numWorkers);
    g_pool = g_poolOwner.get();
    log::get(name())->info("Task system: {} worker(s) (hardware_concurrency={})", numWorkers, hwThreads);

    // Sized for kMaxObjects (see PhysicsModule.h) plus the one ground
    // rigid body — generous-but-modest numbers, not derived from first
    // principles, verified empirically by actually spawning up to the
    // cap via renderUi()'s button rather than assumed correct.
    AMD::FmSceneSetupParams sceneParams;
    // Capacities are per *scene*, and FEMFX counts every fracture piece
    // as its own tet mesh with its own contacts. These used to be sized
    // for kMaxObjects single tetrahedra (maxTetMeshes=64, maxSceneVerts=
    // 272, 512 contacts) — one Glass Sheet alone has 147 verts and
    // shatters into ~60 pieces, so after the first break FEMFX was
    // silently dropping pieces, contacts and constraint-solver verts.
    // Dropped contacts are the "falls through the floor" symptom from
    // BUGS.md BUG-005, just further out. FEMFX reports every limit it
    // hits (FmWarningsReport) — fixedUpdate() now logs those, so if any
    // of these are still too small, the log says which one.
    // Proportions follow AMD's own TestScenes.h, scaled down.
    sceneParams.maxTetMeshBuffers = kMaxObjects;
    sceneParams.maxTetMeshes = kMaxScenePieces;
    // Rigid bodies are ragdoll limbs (see createRagdoll()); the ground is
    // FEMFX's built-in floor plane, not a body -- see the ground comment below.
    sceneParams.maxRigidBodies = kMaxRigidBodies;
    sceneParams.maxDistanceContacts = 65536;
    sceneParams.maxVolumeContacts = 8192;
    sceneParams.maxVolumeContactVerts = 131072;
    sceneParams.maxDeformationConstraints = kMaxObjects * 64;
    sceneParams.maxGlueConstraints = kMaxRigidBodies * 2;
    sceneParams.maxPlaneConstraints = 0;
    sceneParams.maxRigidBodyAngleConstraints = kMaxRigidBodies;
    sceneParams.maxBroadPhasePairs = 16384;
    sceneParams.maxRigidBodyBroadPhasePairs = 4096;
    sceneParams.maxSceneVerts = 65536;
    sceneParams.maxTetMeshBufferFeatures = 4096;
    sceneParams.numWorkerThreads = numWorkers;
    sceneParams.rigidBodiesExternal = false;
    sceneParams.maxConstraintSolverDataSize = AMD::FmEstimateSceneConstraintSolverDataSize(sceneParams);
    log::get(name())->info("FEMFX constraint solver memory: {:.1f} MB", sceneParams.maxConstraintSolverDataSize / (1024.0 * 1024.0));

    m_scene = AMD::FmCreateScene(sceneParams);
    if (!m_scene) {
        throw std::runtime_error("FmCreateScene returned null");
    }

    AMD::FmTaskSystemCallbacks callbacks;
    callbacks.SetCallbacks(
        GetTaskSystemNumThreads,
        GetTaskSystemWorkerIndex,
        SubmitAsyncTask,
        CreateSyncEvent,
        DestroySyncEvent,
        WaitForSyncEvent,
        TriggerSyncEvent
    );
    AMD::FmSetSceneTaskSystemCallbacks(m_scene, callbacks);
    // Ragdoll limbs don't collide with each other (see createRagdoll()).
    AMD::FmSetGroupsCanCollide(m_scene, kRagdollCollisionGroup, kRagdollCollisionGroup, false);

    // --- The ground: FEMFX's own built-in scene collision plane, not a
    // rigid body. FmSceneControlParams::collisionPlanes defaults to a
    // floor at y=0 (every other side open), which is exactly the ground
    // this demo wants. This used to be a 100x1x100 kinematic box rigid
    // body on top of that plane, and it had two real costs:
    //   - FEMFX treats a rigid body as always-awake, and its contact
    //     with a sleeping tet mesh woke that mesh again on the very next
    //     step. So nothing resting on the ground could ever stay asleep —
    //     measured: ~480 of 491 debris pieces awake after 15 s, even
    //     though ~95% of them were moving slower than 0.05 m/s.
    //   - Every piece paid a box-vs-mesh contact test every step.
    // With the plane alone, settled scenes sleep completely and the
    // physics step for the same 475-piece pile drops from ~95 ms to
    // ~0.2 ms (1-core min-spec emulation, see PERFORMANCE_NOTES.md).
    // Objects rest at the same height as before (0.0020 above y=0).
    {
        AMD::FmSceneControlParams controlParams = AMD::FmGetSceneControlParams(*m_scene);
        controlParams.collisionPlanes.minY = 0.0f;
        AMD::FmSetSceneControlParams(m_scene, controlParams);
    }

    // --- Shared render resources (see PhysicsModule.h for why these
    // are shared across every spawned object, not per-object). Reuses
    // the existing cube shaders directly — they just transform a
    // position by an MVP matrix and output a flat color, which is
    // exactly what a physics-driven mesh needs too, no new shaders
    // required.
    {
        PipelineConfig config;
        // Back faces culled. FEMFX exterior faces come out counter-
        // clockwise seen from outside (checked by hand on a grid tet:
        // face 0's normal points away from corner 0), which is Vulkan's
        // front face here (see BUG-040: the projection's Y flip and the
        // framebuffer's Y-down cancel). Culling was off since an old
        // "TEMPORARY DIAGNOSTIC", so every face was shaded twice and
        // the inside of a piece showed through its cracks.
        config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        config.cullMode = VK_CULL_MODE_BACK_BIT;
        config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PhysicsPushConstants) };
        config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout(), app.shadowMapSetLayout(), app.materialTextureSetLayout() };
        m_pipeline = std::make_unique<Pipeline>(
            app.device(), app.renderer().renderPass(),
            "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);
    }

    // Real shadow casting for spawned objects — same minimal
    // shadow.vert/frag pipeline pattern already proven in CubeModule
    // (see PhysicsModule.h's own comment on why the ground plane is
    // deliberately excluded from this).
    {
        PipelineConfig shadowConfig;
        shadowConfig.cullMode = VK_CULL_MODE_NONE;
        shadowConfig.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstants) };
        m_shadowPipeline = std::make_unique<Pipeline>(
            app.device(), app.shadowMap().renderPass(),
            "shaders/shadow.vert.spv", "shaders/shadow.frag.spv", shadowConfig);
    }

    m_vkDevice = app.device().device();

    // A real material texture library — five distinct, procedurally
    // generated textures, one per kke::MaterialGridModule preset (see
    // Material.h's own comment on textureId, and this class's own
    // header for the full account). Generated, not loaded from files,
    // for the same reason CubeModule's own checkerboard was: no
    // external asset dependency, and each pattern is simple enough to
    // write directly. A tiny deterministic hash stands in for noise
    // (no external RNG dependency needed for a repeatable, good-enough
    // speckle/grain pattern) — not cryptographically meaningful, just
    // needs to look irregular.
    {
        constexpr uint32_t kTexSize = 64;
        auto hash = [](uint32_t x, uint32_t y) -> float {
            uint32_t h = x * 374761393u + y * 668265263u;
            h = (h ^ (h >> 13)) * 1274126177u;
            h ^= (h >> 16);
            return static_cast<float>(h & 0xFFFFu) / 65535.0f;
        };

        auto makeTexture = [&](auto pixelFn) {
            std::vector<uint8_t> pixels(kTexSize * kTexSize * 4);
            for (uint32_t y = 0; y < kTexSize; ++y) {
                for (uint32_t x = 0; x < kTexSize; ++x) {
                    glm::vec3 c = pixelFn(x, y);
                    size_t idx = (static_cast<size_t>(y) * kTexSize + x) * 4;
                    pixels[idx + 0] = static_cast<uint8_t>(std::clamp(c.r, 0.0f, 1.0f) * 255.0f);
                    pixels[idx + 1] = static_cast<uint8_t>(std::clamp(c.g, 0.0f, 1.0f) * 255.0f);
                    pixels[idx + 2] = static_cast<uint8_t>(std::clamp(c.b, 0.0f, 1.0f) * 255.0f);
                    pixels[idx + 3] = 255;
                }
            }
            MaterialTexture mt;
            mt.texture = std::make_unique<Texture>(app.device(), pixels.data(), kTexSize, kTexSize);
            m_materialTextures.push_back(std::move(mt));
        };

        // 0: Wood -- horizontal brown grain bands with irregular edges.
        makeTexture([&](uint32_t x, uint32_t y) {
            float band = std::sin((y + hash(x, 0) * 6.0f) * 0.9f) * 0.5f + 0.5f;
            float grain = hash(x, y) * 0.15f;
            float v = 0.35f + band * 0.25f + grain;
            return glm::vec3(v, v * 0.55f, v * 0.25f);
        });
        // 1: Stone -- gray with blotchy speckle noise.
        makeTexture([&](uint32_t x, uint32_t y) {
            float n = hash(x / 4, y / 4) * 0.5f + hash(x, y) * 0.5f;
            float v = 0.45f + n * 0.3f;
            return glm::vec3(v, v, v);
        });
        // 2: Iron -- light gray with diagonal brushed-metal streaks.
        makeTexture([&](uint32_t x, uint32_t y) {
            float streak = std::sin(static_cast<float>(x + y) * 0.8f) * 0.06f;
            float n = hash(x, y) * 0.08f;
            float v = 0.65f + streak + n;
            return glm::vec3(v, v, v * 1.02f);
        });
        // 3: Rubber -- dark, mostly flat with subtle noise.
        makeTexture([&](uint32_t x, uint32_t y) {
            float v = 0.08f + hash(x, y) * 0.05f;
            return glm::vec3(v, v, v);
        });
        // 4: Glass -- light, smooth, faint blue tint gradient.
        makeTexture([&](uint32_t x, uint32_t y) {
            float grad = static_cast<float>(y) / kTexSize * 0.1f;
            return glm::vec3(0.82f + grad, 0.88f + grad, 0.92f + grad);
        });
        // 5: Lava -- bright orange/red with irregular yellow-hot
        // streaks, for the "Scene: Lava Melt" button's own falling
        // lava chunks (see that button's own comment for the full
        // account of what this scene actually simulates and why).
        makeTexture([&](uint32_t x, uint32_t y) {
            float n = hash(x, y);
            float hotSpot = std::sin(static_cast<float>(x) * 0.5f + static_cast<float>(y) * 0.3f) * 0.5f + 0.5f;
            float heat = std::clamp(hotSpot * 0.6f + n * 0.4f, 0.0f, 1.0f);
            // Interpolates from a deep red base toward bright yellow-
            // white at the hottest points, not a flat orange -- real
            // lava photos show this same red-to-yellow gradient
            // wherever it's actively glowing hottest.
            return glm::vec3(0.9f, 0.25f + heat * 0.55f, heat * 0.15f);
        });

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(m_materialTextures.size()) };
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = static_cast<uint32_t>(m_materialTextures.size());
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(m_vkDevice, &poolInfo, nullptr, &m_materialTexturePool));

        VkDescriptorSetLayout textureLayout = app.materialTextureSetLayout();
        for (auto& mt : m_materialTextures) {
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_materialTexturePool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &textureLayout;
            VK_CHECK(vkAllocateDescriptorSets(m_vkDevice, &allocInfo, &mt.descriptorSet));

            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imageInfo.imageView = mt.texture->imageView();
            imageInfo.sampler = mt.texture->sampler();

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = mt.descriptorSet;
            write.dstBinding = 0;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            write.pImageInfo = &imageInfo;
            vkUpdateDescriptorSets(m_vkDevice, 1, &write, 0, nullptr);
        }
    }

    m_groundMesh = std::make_unique<Mesh>(Mesh::createCube(app.device()));

    // The API dogfoods itself: the demo's starting object(s) are
    // created by calling the same public spawnTetrahedron() anyone
    // else (including renderUi()'s own button) would call, not
    // through separate one-off setup code. Spread out at varied
    // positions/heights when spawning more than one, so multiple
    // objects don't all land stacked in the exact same spot.
    Material woodMaterial;
    woodMaterial.density = 700.0f;
    woodMaterial.stiffness = 1.0e7f;
    woodMaterial.poissonsRatio = 0.3f;
    woodMaterial.plasticYieldThreshold = 0.0f;
    woodMaterial.fractureStressThreshold = 1.0e8f; // high — this one isn't meant to fracture

    for (int i = 0; i < m_initialObjectCount; ++i) {
        float x = (i % 3 - 1) * 2.5f;
        float z = (i / 3) * 2.5f;
        float y = 5.0f + i * 1.5f; // staggered heights so they don't all land at once
        spawnTetrahedron(glm::vec3(x, y, z), woodMaterial);
    }

    log::get(name())->info("FEMFX scene created (ground + {} object(s), cap={})", m_objects.size(), kMaxObjects);

    m_timingWindowStart = nowSeconds();
    if (const char* bench = std::getenv("KKE_PHYSICS_BENCH")) {
        m_benchTicks = std::strtoull(bench, nullptr, 10);
        if (m_benchTicks) log::get(name())->info("BENCH: scripted benchmark enabled, {} ticks", m_benchTicks);
    }
}

PhysicsModule::ObjectHandle PhysicsModule::spawnTetMesh(const TetMeshData& mesh, const glm::vec3& position, const Material& material) {
    return spawnTetMeshInternal(mesh, position, material, /*enableFracture=*/false);
}

PhysicsModule::ObjectHandle PhysicsModule::spawnFracturableTetMesh(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                                                     const glm::vec3& initialVelocity) {
    return spawnTetMeshInternal(mesh, position, material, /*enableFracture=*/true, initialVelocity);
}

PhysicsModule::ObjectHandle PhysicsModule::spawnPlasticTetMesh(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                                                 const glm::vec3& initialVelocity) {
    return spawnTetMeshInternal(mesh, position, material, /*enableFracture=*/false, initialVelocity, /*enablePlasticity=*/true);
}

PhysicsModule::ObjectHandle PhysicsModule::spawnTetMeshWithOptions(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                                                     const TetSpawnOptions& options) {
    if (options.fracture && !options.chunkOfTet.empty()) {
        if (options.chunkOfTet.size() != mesh.tets.size()) {
            log::get(name())->error("spawnTetMeshWithOptions: {} chunk ids for {} tets", options.chunkOfTet.size(), mesh.tets.size());
            return kInvalidHandle;
        }
        return spawnBreakable(mesh, position, material, options);
    }
    if (!options.tetFlags.empty() && options.tetFlags.size() != mesh.tets.size()) {
        log::get(name())->error("spawnTetMeshWithOptions: {} tet flags for {} tets", options.tetFlags.size(), mesh.tets.size());
        return kInvalidHandle;
    }
    ObjectHandle h = spawnTetMeshInternal(mesh, position, material, options.fracture, options.velocity, options.plastic,
                                          options.tetFlags.empty() ? nullptr : &options.tetFlags, options.drawOnlyCracks,
                                          options.armFractureAfterSeconds);
    if (h != kInvalidHandle) {
        SpawnedTet& obj = *m_objects[h];
        if (options.vertexUVs.size() == mesh.vertices.size()) obj.vertexUVs = options.vertexUVs;
        if (options.tetStrength.size() == mesh.tets.size()) {
            obj.tetStrength = options.tetStrength;
            // Not arming: the real thresholds apply now (arming applies
            // them itself, see fixedUpdate()).
            if (options.fracture && !obj.armPending) {
                AMD::FmTetMaterialParams p = toFemfx(material);
                const float base = p.fractureStressThreshold;
                for (uint32_t t = 0; t < obj.numTets; ++t) {
                    if (obj.tetStrength[t] == 1.0f) continue;
                    p.fractureStressThreshold = base * obj.tetStrength[t];
                    AMD::FmUpdateTetMaterialParams(m_scene, obj.tetMesh, t, p);
                }
            }
        }
        obj.textureSet = m_app->textureSet(options.texturePath); // shared engine cache
        if (obj.textureSet) obj.color = glm::vec3(1.0f); // the texture carries the color
    }
    return h;
}

// ---------------------------------------------------------------- breakables
// See PhysicsModule.h, struct Breakable, for why these don't use FEMFX's
// own fracture, and kke/BreakGraph.h for the bookkeeping.

namespace {
// Parts never fracture inside FEMFX: an unreachable threshold. Fracture
// stays *enabled* only because that is when FEMFX computes the per-tet
// stress KKE reads (FmGetTetMaxStress, see external/FEMFX/KKE_FORK.md).
constexpr float kNoFemfxFracture = 1.0e20f;
} // namespace

PhysicsModule::ObjectHandle PhysicsModule::spawnBreakable(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                                          const TetSpawnOptions& options) {
    const ObjectHandle bh = m_nextHandle++;
    Breakable& b = m_breakables[bh];
    b.mesh = mesh;
    b.origin = position;
    b.graph = BreakGraph(mesh, options.chunkOfTet, options.tetStrength);
    b.graph.arm(material.fractureStressThreshold);
    b.material = material;
    b.plastic = options.plastic;
    b.drawOnlyCracks = options.drawOnlyCracks;
    b.vertexUVs = options.vertexUVs.size() == mesh.vertices.size() ? options.vertexUVs : std::vector<glm::vec2>{};
    b.textureSet = m_app->textureSet(options.texturePath);
    // Textured (an image, or the material's own texture): shown as is,
    // not tinted by the debug palette.
    b.color = (b.textureSet || material.textureId >= 0) ? glm::vec3(1.0f) : kColorPalette[bh % (sizeof(kColorPalette) / sizeof(kColorPalette[0]))];
    const size_t nt = mesh.tets.size();
    b.restInverse.resize(nt);
    for (uint32_t t = 0; t < nt; ++t) {
        const auto& id = mesh.tets[t];
        glm::mat3 dm(mesh.vertices[id[1]] - mesh.vertices[id[0]], mesh.vertices[id[2]] - mesh.vertices[id[0]], mesh.vertices[id[3]] - mesh.vertices[id[0]]);
        b.restInverse[t] = std::fabs(glm::determinant(dm)) > 1e-18f ? glm::inverse(dm) : glm::mat3(1.0f);
    }
    b.partOfTet.assign(nt, kInvalidHandle);
    b.localOfTet.assign(nt, 0);
    if (options.armFractureAfterSeconds > 0.0f) {
        b.armPending = true;
        b.settleStress.assign(nt, 0.0f);
        b.armMaxTicks = std::max(20u, static_cast<uint32_t>(options.armFractureAfterSeconds * 60.0f));
    }
    std::vector<uint32_t> all(nt);
    for (uint32_t t = 0; t < nt; ++t) all[t] = t;
    if (!spawnBreakablePart(bh, all, kInvalidHandle, options.velocity)) {
        m_breakables.erase(bh);
        return kInvalidHandle;
    }
    return bh;
}

PhysicsModule::ObjectHandle PhysicsModule::spawnBreakablePart(ObjectHandle bh, const std::vector<uint32_t>& tets, ObjectHandle from,
                                                              const glm::vec3& velocity) {
    Breakable& b = m_breakables.at(bh);
    // Sub-mesh: the tets keep their corner order (so face numbers match
    // the baked mesh), vertices renumbered.
    TetMeshData sub;
    std::unordered_map<uint32_t, uint32_t> local;
    std::vector<uint32_t> bakedVert;
    sub.tets.reserve(tets.size());
    for (uint32_t t : tets) {
        std::array<uint32_t, 4> ids{};
        for (int k = 0; k < 4; ++k) {
            uint32_t v = b.mesh.tets[t][k];
            auto [it, fresh] = local.emplace(v, static_cast<uint32_t>(sub.vertices.size()));
            if (fresh) { sub.vertices.push_back(b.mesh.vertices[v]); bakedVert.push_back(v); }
            ids[k] = it->second;
        }
        sub.tets.push_back(ids);
    }
    Material m = b.material;
    m.fractureStressThreshold = kNoFemfxFracture;
    ObjectHandle h = spawnTetMeshInternal(sub, b.origin, m, true, velocity, b.plastic, nullptr, b.drawOnlyCracks, 0.0f);
    if (h == kInvalidHandle) return kInvalidHandle;
    SpawnedTet& part = *m_objects[h];
    part.breakable = bh;
    part.bornTick = m_physicsTick;
    part.bakedTetOf = tets;
    part.bakedVertOf = bakedVert;
    part.textureSet = b.textureSet;
    part.color = b.color;
    if (!b.vertexUVs.empty()) {
        part.vertexUVs.resize(bakedVert.size());
        for (size_t v = 0; v < bakedVert.size(); ++v) part.vertexUVs[v] = b.vertexUVs[bakedVert[v]];
    }
    // Crack faces are the ones that were inside the *whole* object.
    part.originalExterior.resize(tets.size());
    for (size_t t = 0; t < tets.size(); ++t) part.originalExterior[t] = b.graph.originalExterior(tets[t]);
    for (size_t t = 0; t < tets.size(); ++t) {
        b.partOfTet[tets[t]] = h;
        b.localOfTet[tets[t]] = static_cast<uint32_t>(t);
    }
    b.parts.push_back(h);

    // Splitting off: start exactly where the old part's vertices are,
    // moving as they were - nothing pops or stops.
    auto fit = m_objects.find(from);
    if (fit != m_objects.end()) {
        const SpawnedTet& old = *fit->second;
        std::unordered_map<uint32_t, uint32_t> oldLocal;
        for (uint32_t v = 0; v < old.bakedVertOf.size(); ++v) oldLocal.emplace(old.bakedVertOf[v], v);
        for (uint32_t v = 0; v < bakedVert.size(); ++v) {
            auto o = oldLocal.find(bakedVert[v]);
            if (o == oldLocal.end()) continue;
            AMD::FmSetVertPosition(m_scene, part.tetMesh, v, AMD::FmGetVertPosition(*old.tetMesh, o->second));
            AMD::FmSetVertVelocity(m_scene, part.tetMesh, v, AMD::FmGetVertVelocity(*old.tetMesh, o->second));
        }
    }
    return h;
}

void PhysicsModule::splitBreakablePart(ObjectHandle bh, ObjectHandle ph) {
    Breakable& b = m_breakables.at(bh);
    auto pit = m_objects.find(ph);
    if (pit == m_objects.end()) return;
    const std::vector<uint32_t> tets = pit->second->bakedTetOf; // copy: the part goes away below
    auto groups = b.graph.groups(tets, [&](uint32_t t) { return b.partOfTet[t] == ph; });
    pit->second->breakPending = -1;
    if (groups.size() < 2) return; // cracked, but still one piece
    {
        const AMD::FmVector3 lo = AMD::FmGetMinPosition(*pit->second->tetMesh), hi = AMD::FmGetMaxPosition(*pit->second->tetMesh);
        BreakEvent e;
        e.position = glm::vec3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f) * m_renderScale;
        e.material = b.material;
        e.newPieces = uint32_t(groups.size() - 1);
        e.size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * m_renderScale;
        m_frameBreaks.push_back(e);
    }
    for (const auto& g : groups) {
        ObjectHandle h = spawnBreakablePart(bh, g, ph, glm::vec3(0.0f));
        if (h != kInvalidHandle) m_objects[h]->breakGrace = 8; // kBreakGrace, see updateBreakables()
    }
    ++b.breaks;
    removeObject(ph);
    log::get(name())->info("breakable {} split: {} broken borders, now {} pieces", bh, b.graph.brokenBorderCount(), b.parts.size());
}

void PhysicsModule::enforceDebrisBudget() {
    if (!m_debrisBudget) return;
    size_t pieces = 0;
    for (const auto& [bh, b] : m_breakables) pieces += b.parts.size() > 1 ? b.parts.size() : 0; // whole objects aren't debris
    while (pieces > m_debrisBudget) {
        ObjectHandle victim = kInvalidHandle;
        uint64_t oldest = ~0ull;
        bool victimAsleep = false;
        for (const auto& [bh, b] : m_breakables) {
            if (b.parts.size() < 2) continue;
            for (ObjectHandle ph : b.parts) {
                const SpawnedTet& p = *m_objects[ph];
                const bool asleep = AMD::FmIsTetMeshSleeping(*p.tetMesh);
                // Sleeping beats awake; then oldest.
                if (victim == kInvalidHandle || (asleep && !victimAsleep) || (asleep == victimAsleep && p.bornTick < oldest)) {
                    victim = ph;
                    oldest = p.bornTick;
                    victimAsleep = asleep;
                }
            }
        }
        if (victim == kInvalidHandle) break;
        removeObject(victim); // embedded render points of it collapse to nothing (deformEmbedded)
        ++m_debrisRemoved;
        --pieces;
    }
}

void PhysicsModule::updateBreakables() {
    for (auto& [bh, b] : m_breakables) {
        if (b.armPending) {
            // Settle, then arm (TetSpawnOptions::armFractureAfterSeconds):
            // each border's threshold = material x strength + 1.25x the
            // stress it carried at rest, so only an impact breaks it.
            ++b.armAge;
            bool settled = true;
            for (ObjectHandle ph : b.parts) {
                const SpawnedTet& part = *m_objects[ph];
                settled &= AMD::FmIsTetMeshSleeping(*part.tetMesh);
                for (uint32_t t = 0; t < part.bakedTetOf.size(); ++t) {
                    uint32_t bt = part.bakedTetOf[t];
                    // A decaying peak (x0.9 per tick): the stress of the
                    // last ~20 ticks counts, the landing spike (5M on a
                    // crate settling 3 mm, against 1e5 thresholds) is
                    // forgotten by the time it arms. A plain peak made
                    // such props unbreakable.
                    b.settleStress[bt] = std::max(b.settleStress[bt] * 0.9f, AMD::FmGetTetMaxStress(*part.tetMesh, t));
                }
            }
            if (b.armAge < 45 || (!settled && b.armAge < b.armMaxTicks)) continue;
            b.graph.arm(b.material.fractureStressThreshold, b.settleStress);
            std::vector<float> sorted;
            for (size_t t = 0; t < b.settleStress.size(); ++t) if (b.graph.isBorderTet(static_cast<uint32_t>(t))) sorted.push_back(b.settleStress[t]);
            std::sort(sorted.begin(), sorted.end());
            auto pct = [&](float q) { return sorted.empty() ? 0.0f : sorted[std::min(sorted.size() - 1, static_cast<size_t>(q * sorted.size()))]; };
            b.armPending = false;
            b.settleStress = {};
            log::get(name())->info("breakable {} armed after {} ticks: border resting stress median {:.0f}, p90 {:.0f}, max {:.0f}; base threshold {:.0f}", bh,
                                   b.armAge, pct(0.5f), pct(0.9f), sorted.empty() ? 0.0f : sorted.back(), b.material.fractureStressThreshold);
            continue;
        }
        // Two timings, both measured with tools/physics_lab ("shoot"):
        //  - a part that gets overloaded splits kBreakWindow ticks later,
        //    collecting every border overloaded meanwhile: an impact
        //    builds up over a few steps, and splitting on its first
        //    touch broke only the contact point;
        //  - a freshly split part can't break for kBreakGrace ticks: the
        //    swap itself (pieces released, touching along their new
        //    faces) spikes the stress, and without a pause every break
        //    cascaded into every piece - all-or-nothing. With both, the
        //    damage grows with the hit (a 0.8 m stone crate: 1 piece at
        //    12 m/s, 7 at 18, all 19 at 30).
        constexpr int kBreakWindow = 2, kBreakGrace = 8;
        std::vector<ObjectHandle> overloaded;
        for (ObjectHandle ph : b.parts) {
            SpawnedTet& part = *m_objects[ph];
            if (part.breakGrace > 0) { --part.breakGrace; continue; }
            if (AMD::FmIsTetMeshSleeping(*part.tetMesh) && part.breakPending < 0) continue; // stress only changes while awake
            bool any = false;
            auto same = [&](uint32_t t) { return b.partOfTet[t] == ph; };
            for (uint32_t t = 0; t < part.bakedTetOf.size(); ++t) {
                uint32_t bt = part.bakedTetOf[t];
                if (b.graph.isBorderTet(bt)) any |= b.graph.report(bt, AMD::FmGetTetMaxStress(*part.tetMesh, t), same);
            }
            if (any && part.breakPending < 0) part.breakPending = kBreakWindow;
            if (part.breakPending == 0) overloaded.push_back(ph);
            if (part.breakPending > 0) --part.breakPending;
        }
        for (ObjectHandle ph : overloaded) splitBreakablePart(bh, ph);
    }
}

PhysicsModule::ObjectHandle PhysicsModule::spawnTetMeshInternal(const TetMeshData& mesh, const glm::vec3& position, const Material& material, bool enableFracture,
                                                                  const glm::vec3& initialVelocity, bool enablePlasticity,
                                                                  const std::vector<uint16_t>* tetFlags, bool drawOnlyCracks,
                                                                  float armFractureAfterSeconds) {
    if (m_objects.size() >= kMaxObjects) {
        log::get(name())->warn("spawnTetMesh: at cap ({}), ignoring", kMaxObjects);
        return kInvalidHandle;
    }
    if (mesh.vertices.empty() || mesh.tets.empty()) {
        log::get(name())->error("spawnTetMesh: mesh has no vertices or no tets");
        return kInvalidHandle;
    }
    for (const auto& tet : mesh.tets) {
        for (uint32_t idx : tet) {
            if (idx >= mesh.vertices.size()) {
                log::get(name())->error("spawnTetMesh: tet references vertex {}, but mesh only has {} vertices",
                                         idx, mesh.vertices.size());
                return kInvalidHandle;
            }
        }
    }

    auto obj = std::make_unique<SpawnedTet>();
    obj->fracturable = enableFracture;
    obj->plastic = enablePlasticity;
    const uint32_t numVerts = static_cast<uint32_t>(mesh.vertices.size());
    const uint32_t numTets = static_cast<uint32_t>(mesh.tets.size());
    obj->numVerts = numVerts;
    obj->numTets = numTets;

    obj->restPositions.resize(numVerts);
    for (uint32_t i = 0; i < numVerts; ++i) {
        const glm::vec3& v = mesh.vertices[i];
        obj->restPositions[i] = AMD::FmInitVector3(position.x + v.x, position.y + v.y, position.z + v.z);
    }

    obj->tetVertIds.resize(numTets);
    for (uint32_t i = 0; i < numTets; ++i) {
        const auto& t = mesh.tets[i];
        obj->tetVertIds[i].ids[0] = t[0];
        obj->tetVertIds[i].ids[1] = t[1];
        obj->tetVertIds[i].ids[2] = t[2];
        obj->tetVertIds[i].ids[3] = t[3];
    }

    // Real per-vertex incident-tet lists, not the single hardcoded
    // "every vertex belongs to tet 0" case from before — for each tet,
    // every one of its 4 vertices gets that tet's index added to its
    // own incident list. This is what makes a real multi-tet mesh
    // behave as one connected deformable body rather than a pile of
    // disconnected tets that happen to share vertex positions.
    obj->vertIncidentTets.resize(numVerts);
    for (uint32_t t = 0; t < numTets; ++t) {
        for (int j = 0; j < 4; ++j) {
            obj->vertIncidentTets[obj->tetVertIds[t].ids[j]].Add(t);
        }
    }

    // Real fracture support, when requested — confirmed against AMD's
    // own vendored sample code (external/FEMFX/samples/common/
    // TestScenes.cpp), not guessed: FmComputeTetMeshBufferBounds
    // itself COMPUTES per-tet fracture group data into these two
    // arrays when given real (non-null) output pointers, sized to
    // numTets by the caller beforehand. The earlier version of this
    // function always passed nullptr for both, which is exactly why
    // enableFracture was false everywhere in this whole codebase up
    // to this point — passing enableFracture=true without also
    // providing these arrays would have left FEMFX's own fracture
    // bookkeeping empty and done nothing.
    if (enableFracture) {
        obj->fractureGroupCounts.resize(numTets);
        obj->tetFractureGroupIds.resize(numTets);
    }

    // Per-tet flags (fracture patterns: which faces may crack — see
    // kke/FracturePattern.h). FEMFX needs them twice: here, to bound how
    // many pieces fracture can create (fewer when faces are locked), and
    // on the tets themselves after init (below). Must match.
    if (tetFlags) obj->tetFlags = *tetFlags;
    AMD::FmTetMeshBufferBounds bounds;
    AMD::FmComputeTetMeshBufferBounds(
        &bounds,
        enableFracture ? obj->fractureGroupCounts.data() : nullptr,
        enableFracture ? obj->tetFractureGroupIds.data() : nullptr,
        obj->vertIncidentTets.data(), obj->tetVertIds.data(),
        obj->tetFlags.empty() ? nullptr : obj->tetFlags.data(), numVerts, numTets, enableFracture);

    obj->maxVerts = bounds.maxVerts;
    // FEMFX doesn't expose a direct "maxTets" in FmTetMeshBufferBounds
    // (see AMD_FEMFX.h — it bounds verts/faces/vert-adjacency directly,
    // since those are what its own internal buffers size against, not
    // tets as a separate quantity) — this project's own conservative
    // estimate for "largest a fracturable object's tet count could
    // reasonably read as post-fracture" is the same numTets it started
    // with: fracture splits existing tets into separate pieces, it
    // doesn't create new ones, so the total across every current piece
    // can't exceed the original count.
    obj->maxTets = numTets;

    AMD::FmTetMeshBufferSetupParams meshParams;
    meshParams.numVerts = bounds.numVerts;
    meshParams.numTets = bounds.numTets;
    meshParams.numVertIncidentTets = bounds.numVertIncidentTets;
    meshParams.maxVertAdjacentVerts = bounds.maxVertAdjacentVerts;
    meshParams.maxVerts = bounds.maxVerts;
    meshParams.maxExteriorFaces = bounds.maxExteriorFaces;
    meshParams.maxTetMeshes = bounds.maxTetMeshes;
    meshParams.collisionGroup = 0;
    meshParams.enablePlasticity = enablePlasticity;
    meshParams.enableFracture = enableFracture;
    meshParams.isKinematic = false;

    obj->tetMeshBuffer = AMD::FmCreateTetMeshBuffer(
        meshParams,
        enableFracture ? obj->fractureGroupCounts.data() : nullptr,
        enableFracture ? obj->tetFractureGroupIds.data() : nullptr,
        &obj->tetMesh);
    if (!obj->tetMeshBuffer || !obj->tetMesh) {
        log::get(name())->error("spawnTetMesh: FmCreateTetMeshBuffer failed");
        return kInvalidHandle;
    }

    AMD::FmMatrix3 identity = AMD::FmInitMatrix3(
        AMD::FmInitVector3(1.0f, 0.0f, 0.0f),
        AMD::FmInitVector3(0.0f, 1.0f, 0.0f),
        AMD::FmInitVector3(0.0f, 0.0f, 1.0f));
    AMD::FmInitVertState(obj->tetMesh, obj->restPositions.data(), identity, AMD::FmInitVector3(0.0f), 1.0f,
                          AMD::FmInitVector3(initialVelocity.x, initialVelocity.y, initialVelocity.z));

    AMD::FmTetMaterialParams femfxMaterial = toFemfx(material);
    if (enableFracture && armFractureAfterSeconds > 0.0f) {
        // Unbreakable until armed (see TetSpawnOptions); fixedUpdate()
        // applies the real threshold after the delay.
        obj->armedParams = femfxMaterial;
        obj->armPending = true;
        obj->settleStress.assign(numTets, 0.0f);
        obj->armMaxTicks = std::max(20u, static_cast<uint32_t>(armFractureAfterSeconds * 60.0f));
        femfxMaterial.fractureStressThreshold = 1.0e12f;
    }

    AMD::FmInitTetState(obj->tetMesh, obj->tetVertIds.data(), femfxMaterial);
    AMD::FmComputeMeshConstantMatrices(obj->tetMesh);

    // A FRESH set of arrays for FmInitConnectivity, not the same
    // vertIncidentTets already passed to FmComputeTetMeshBufferBounds
    // above — that call takes a non-const pointer and, verified by
    // testing, appears to consume/mutate them internally: reusing the
    // same arrays crashed immediately in standalone testing, building
    // a fresh set fixed it. See PhysicsModule.h's class comment for
    // the real, gdb-traced crash this call fixes.
    std::vector<AMD::FmArray<uint32_t>> connectivityVertIncidentTets(numVerts);
    for (uint32_t t = 0; t < numTets; ++t) {
        for (int j = 0; j < 4; ++j) {
            connectivityVertIncidentTets[obj->tetVertIds[t].ids[j]].Add(t);
        }
    }
    if (!AMD::FmInitConnectivity(obj->tetMesh, connectivityVertIncidentTets.data())) {
        log::get(name())->error("spawnTetMesh: FmInitConnectivity failed");
        AMD::FmDestroyTetMeshBuffer(obj->tetMeshBuffer);
        return kInvalidHandle;
    }
    // NOT calling FmFinishConnectivityFromVertIncidentTets() here —
    // FmInitConnectivity already calls it internally. Doing so a
    // second time was the real cause of a since-fixed crash — see the
    // class comment's debugging account, point 3.
    AMD::FmSetMassesFromRestDensities(obj->tetMesh, 0.0f);

    int initResult = AMD::FmFinishTetMeshInit(obj->tetMesh);
    if (initResult != 0) {
        log::get(name())->error("spawnTetMesh: FmFinishTetMeshInit failed with code {}", initResult);
        AMD::FmDestroyTetMeshBuffer(obj->tetMeshBuffer);
        return kInvalidHandle;
    }

    for (uint32_t t = 0; t < obj->tetFlags.size(); ++t) {
        if (obj->tetFlags[t]) AMD::FmSetTetFlags(obj->tetMesh, t, obj->tetFlags[t]);
    }

    // Data for embedded render meshes: which faces were outside at spawn
    // (a face used by only one tet), and each tet's inverse rest edges.
    obj->drawOnlyCracks = drawOnlyCracks;
    obj->restInverse.resize(numTets);
    for (uint32_t t = 0; t < numTets; ++t) {
        const auto& ids = mesh.tets[t];
        glm::mat3 dm(mesh.vertices[ids[1]] - mesh.vertices[ids[0]], mesh.vertices[ids[2]] - mesh.vertices[ids[0]],
                     mesh.vertices[ids[3]] - mesh.vertices[ids[0]]);
        obj->restInverse[t] = std::fabs(glm::determinant(dm)) > 1e-18f ? glm::inverse(dm) : glm::mat3(1.0f);
    }
    if (drawOnlyCracks) {
        std::map<std::array<uint32_t, 3>, int> faceUse;
        auto key = [&](uint32_t t, int f) {
            std::array<uint32_t, 3> k{};
            int j = 0;
            for (int i = 0; i < 4; ++i) if (i != f) k[j++] = mesh.tets[t][i];
            std::sort(k.begin(), k.end());
            return k;
        };
        for (uint32_t t = 0; t < numTets; ++t) for (int f = 0; f < 4; ++f) ++faceUse[key(t, f)];
        obj->originalExterior.assign(numTets, 0);
        for (uint32_t t = 0; t < numTets; ++t)
            for (int f = 0; f < 4; ++f)
                if (faceUse[key(t, f)] == 1) obj->originalExterior[t] |= static_cast<uint8_t>(1u << f);
    }

    AMD::FmEnableSelfCollision(obj->tetMesh, false);
    // Sleeping ON. This used to be disabled for every object, and
    // fracture pieces inherit their parent's flags, so every shard of
    // every break kept being fully simulated forever — total cost only
    // ever went up. FEMFX's own defaults (max speed < 2.0, average speed
    // < 0.15 for 20 consecutive steps) put settled objects and debris to
    // sleep; a collision with something awake wakes their island again.
    // This is the "Sleeping" state from PERFORMANCE_NOTES.md's RayFire/
    // Chaos research, and FEMFX already had it built in.
    AMD::FmEnableSleeping(m_scene, obj->tetMesh, true);

    obj->sceneBufferId = AMD::FmAddTetMeshBufferToScene(m_scene, obj->tetMeshBuffer);
    obj->material = material;

    // Sized from FEMFX's own maxExteriorFaces bound — for a fracturable
    // object that already accounts for every interior face that could
    // become exterior as it breaks, so the buffer never needs to grow.
    // See SpawnedTet's own comment for why only exterior faces are drawn.
    obj->maxRenderVerts = bounds.maxExteriorFaces * 3;
    for (auto& vb : obj->vertexBuffers) {
        vb = std::make_unique<Buffer>(
            m_app->device(), sizeof(Vertex) * obj->maxRenderVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    }
    obj->color = kColorPalette[m_nextHandle % (sizeof(kColorPalette) / sizeof(kColorPalette[0]))];

    ObjectHandle handle = m_nextHandle++;
    m_objects[handle] = std::move(obj);
    return handle;
}

PhysicsModule::ObjectHandle PhysicsModule::spawnPatternedBox(const glm::ivec3& cells, const glm::vec3& size, const glm::vec3& position,
                                                               const Material& material, int pattern, float chunkSize, int cellsPerCluster,
                                                               const glm::vec3& velocity, float armSeconds, const glm::vec3* impactPoint) {
    TetMeshData box = buildGridBox(cells.x, cells.y, cells.z, size.x, size.y, size.z);
    FractureSeedOptions o;
    o.pattern = static_cast<FracturePattern>(pattern);
    o.chunkSize = chunkSize;
    o.seed = fractureSeed(m_fractureWorldSeed, static_cast<uint32_t>(m_nextHandle));
    o.cellsPerCluster = cellsPerCluster;
    // Glass: the star centres somewhere near the middle (it lands flat,
    // so there's no single impact point to aim for).
    o.hasImpactPoint = true;
    Rng01 r(o.seed);
    o.impactPoint = impactPoint ? *impactPoint : glm::vec3((r.next() - 0.5f) * size.x * 0.4f, 0.0f, (r.next() - 0.5f) * size.z * 0.4f);
    BakedFracture baked = bakeFracture(box, o);
    TetSpawnOptions opts;
    opts.fracture = true;
    opts.velocity = velocity;
    opts.chunkOfTet = baked.cut.chunkOfTet;
    opts.armFractureAfterSeconds = armSeconds;
    opts.tetStrength = std::move(baked.cut.tetStrength);
    ObjectHandle h = spawnTetMeshWithOptions(baked.cut.mesh, position, material, opts);
    log::get(name())->info("patterned box {}: {} tets, {} pieces ({}), seed {}", h, baked.cut.mesh.tets.size(), baked.pieces,
                           fracturePatternName(o.pattern), o.seed);
    return h;
}

PhysicsModule::ObjectHandle PhysicsModule::spawnFracturableBox(const glm::ivec3& cells, const glm::vec3& size, const glm::vec3& center,
                                                                 const Material& material, float yawDegrees, const glm::vec3& velocity) {
    TetMeshData box = buildGridBox(cells.x, cells.y, cells.z, size.x, size.y, size.z);
    if (yawDegrees != 0.0f) {
        glm::mat3 r = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0, 1, 0)));
        for (glm::vec3& v : box.vertices) v = r * v;
    }
    return spawnFracturableTetMesh(box, center, material, velocity);
}

bool PhysicsModule::deformEmbedded(ObjectHandle handle, const TetEmbedding& embedding, const std::vector<glm::vec3>& restNormals,
                                   std::vector<glm::vec3>& outPositions, std::vector<glm::vec3>& outNormals) const {
    auto bit = m_breakables.find(handle);
    if (bit != m_breakables.end()) {
        // Tets live in whichever part holds them now.
        const Breakable& b = bit->second;
        const size_t nt = b.mesh.tets.size();
        struct TetNow { glm::vec3 x[4]; glm::mat3 normal; bool valid = false; };
        static thread_local std::vector<TetNow> cacheB;
        cacheB.assign(nt, TetNow{});
        const size_t n = embedding.tet.size();
        outPositions.resize(n);
        outNormals.resize(n);
        for (size_t i = 0; i < n; ++i) {
            uint32_t t = std::min<uint32_t>(embedding.tet[i], static_cast<uint32_t>(nt - 1));
            TetNow& c = cacheB[t];
            if (!c.valid) {
                c.valid = true;
                auto pit = m_objects.find(b.partOfTet[t]);
                if (pit == m_objects.end()) {
                    for (auto& v : c.x) v = glm::vec3(0.0f);
                    c.normal = glm::mat3(1.0f);
                } else {
                    const AMD::FmTetMesh& mesh = *pit->second->tetMesh;
                    AMD::FmTetVertIds ids = AMD::FmGetTetVertIds(mesh, b.localOfTet[t]);
                    for (int k = 0; k < 4; ++k) {
                        AMD::FmVector3 p = AMD::FmGetVertPosition(mesh, ids.ids[k]);
                        c.x[k] = glm::vec3(p.x, p.y, p.z) * m_renderScale;
                    }
                    glm::mat3 f = glm::mat3(c.x[1] - c.x[0], c.x[2] - c.x[0], c.x[3] - c.x[0]) * b.restInverse[t];
                    c.normal = std::fabs(glm::determinant(f)) > 1e-12f ? glm::transpose(glm::inverse(f)) : glm::mat3(1.0f);
                }
            }
            const glm::vec4& w = embedding.weights[i];
            outPositions[i] = c.x[0] * w.x + c.x[1] * w.y + c.x[2] * w.z + c.x[3] * w.w;
            glm::vec3 nrm = i < restNormals.size() ? c.normal * restNormals[i] : glm::vec3(0, 1, 0);
            float len = glm::length(nrm);
            outNormals[i] = len > 1e-12f ? nrm / len : glm::vec3(0, 1, 0);
        }
        return true;
    }
    auto it = m_objects.find(handle);
    if (it == m_objects.end()) return false;
    const SpawnedTet& obj = *it->second;
    // Per-tet current corners and normal matrix, computed once per used
    // tet (a prop has ~300 tets and ~1-3k vertices sharing them).
    struct TetNow { glm::vec3 x[4]; glm::mat3 normal; bool valid = false; };
    static thread_local std::vector<TetNow> cache;
    cache.assign(obj.numTets, TetNow{});
    auto tetNow = [&](uint32_t t) -> const TetNow& {
        TetNow& c = cache[t];
        if (c.valid) return c;
        c.valid = true;
        uint32_t localTet = 0, meshIdx = 0;
        const AMD::FmTetMesh* piece = AMD::FmGetTetMeshContainingTet(&localTet, &meshIdx, *obj.tetMeshBuffer, t);
        if (!piece) {
            for (auto& v : c.x) v = glm::vec3(0.0f);
            c.normal = glm::mat3(1.0f);
            return c;
        }
        AMD::FmTetVertIds ids = AMD::FmGetTetVertIds(*piece, localTet);
        for (int k = 0; k < 4; ++k) {
            AMD::FmVector3 p = AMD::FmGetVertPosition(*piece, ids.ids[k]);
            c.x[k] = glm::vec3(p.x, p.y, p.z) * m_renderScale;
        }
        // Deformation gradient F = Ds * Dm^-1; normals transform by F^-T.
        glm::mat3 f = glm::mat3(c.x[1] - c.x[0], c.x[2] - c.x[0], c.x[3] - c.x[0]) * obj.restInverse[t];
        c.normal = std::fabs(glm::determinant(f)) > 1e-12f ? glm::transpose(glm::inverse(f)) : glm::mat3(1.0f);
        return c;
    };
    const size_t n = embedding.tet.size();
    outPositions.resize(n);
    outNormals.resize(n);
    for (size_t i = 0; i < n; ++i) {
        uint32_t t = std::min<uint32_t>(embedding.tet[i], obj.numTets - 1);
        const TetNow& c = tetNow(t);
        const glm::vec4& w = embedding.weights[i];
        outPositions[i] = c.x[0] * w.x + c.x[1] * w.y + c.x[2] * w.z + c.x[3] * w.w;
        glm::vec3 nrm = i < restNormals.size() ? c.normal * restNormals[i] : glm::vec3(0, 1, 0);
        float len = glm::length(nrm);
        outNormals[i] = len > 1e-12f ? nrm / len : glm::vec3(0, 1, 0);
    }
    return true;
}

bool PhysicsModule::isObjectAsleep(ObjectHandle handle) const {
    auto bit = m_breakables.find(handle);
    if (bit != m_breakables.end()) {
        for (ObjectHandle p : bit->second.parts)
            if (!isObjectAsleep(p)) return false;
        return true;
    }
    auto it = m_objects.find(handle);
    if (it == m_objects.end()) return true;
    const uint32_t numPieces = AMD::FmGetNumTetMeshes(*it->second->tetMeshBuffer);
    for (uint32_t m = 0; m < numPieces; ++m) {
        const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*it->second->tetMeshBuffer, m);
        if (piece && !AMD::FmIsTetMeshSleeping(*piece)) return false;
    }
    return true;
}

uint32_t PhysicsModule::pieceCount(ObjectHandle handle) const {
    auto bit = m_breakables.find(handle);
    if (bit != m_breakables.end()) return static_cast<uint32_t>(bit->second.parts.size());
    auto it = m_objects.find(handle);
    return it == m_objects.end() ? 0 : AMD::FmGetNumTetMeshes(*it->second->tetMeshBuffer);
}

PhysicsModule::ObjectHandle PhysicsModule::spawnTetrahedron(const glm::vec3& position, const Material& material) {
    // The exact single-tetrahedron shape this class's whole
    // verification history (gdb-traced crashes and their fixes,
    // sustained stability runs, the render-scale debugging saga) was
    // built and checked against — now just one call into the general
    // path, not separate code. See the class comment for why keeping
    // this thin wrapper, rather than deleting it in favor of callers
    // building their own TetMeshData, is deliberate: it keeps that
    // whole verification history actually covering spawnTetMesh(),
    // not sitting next to it as an untested special case.
    TetMeshData mesh;
    mesh.vertices = { {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f} };
    mesh.tets = { {0, 1, 2, 3} };
    return spawnTetMesh(mesh, position, material);
}

void PhysicsModule::removeObject(ObjectHandle handle) {
    auto bit = m_breakables.find(handle);
    if (bit != m_breakables.end()) {
        std::vector<ObjectHandle> parts = bit->second.parts;
        for (ObjectHandle p : parts) removeObject(p);
        m_breakables.erase(handle);
        return;
    }
    auto it = m_objects.find(handle);
    if (it == m_objects.end()) {
        return; // no-op, not an error — see the header's own doc comment
    }
    if (it->second->breakable != kInvalidHandle) {
        auto b = m_breakables.find(it->second->breakable);
        if (b != m_breakables.end()) {
            auto& parts = b->second.parts;
            parts.erase(std::remove(parts.begin(), parts.end(), handle), parts.end());
        }
    }
    AMD::FmRemoveTetMeshBufferFromScene(m_scene, it->second->sceneBufferId);
    AMD::FmDestroyTetMeshBuffer(it->second->tetMeshBuffer);
    m_objects.erase(it);
}

void PhysicsModule::fixedUpdate(const FixedUpdateContext& ctx) {
    if (m_benchTicks) benchTick(ctx.tickIndex);
    if (m_breakTestTicks >= 0 && m_breakTestTicks-- == 0) {
        Material iron;
        iron.density = 7800.0f; iron.stiffness = 2.0e7f; iron.poissonsRatio = 0.3f;
        iron.fractureStressThreshold = 1.0e12f; iron.metallic = 0.9f; iron.roughness = 0.35f; iron.textureId = 2;
        for (const glm::vec3& t : m_breakTestTargets)
            spawnTetMeshInternal(buildSphere(3, 0.18f), t + glm::vec3(0.0f, 2.5f, 0.0f), iron, true, glm::vec3(0.0f, -9.0f, 0.0f));
        // The wall: thrown at from the front, not dropped on.
        spawnTetMeshInternal(buildSphere(3, 0.18f), m_breakTestWallTarget + glm::vec3(0.0f, 0.0f, 2.5f), iron, true, glm::vec3(0.0f, 0.0f, -14.0f));
    }
    // KKE_PHYSICS_SCENES=brick,glass,... spawns demo scenes on the first
    // tick (screenshots, sharing a setup, reproducing a report).
    if (!m_startScenesDone) {
        m_startScenesDone = true;
        if (const char* list = std::getenv("KKE_PHYSICS_SCENES")) {
            std::string all(list);
            size_t start = 0;
            while (start <= all.size()) {
                size_t end = all.find(',', start);
                std::string sceneName = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
                static const std::pair<const char*, Scene> kNames[] = {
                    { "glass", Scene::GlassSheet }, { "brick", Scene::Brick }, { "ball", Scene::RubberBall }, { "car", Scene::CarCrash },
                    { "lava", Scene::LavaMelt }, { "cube", Scene::FracturableCube }, { "plastic", Scene::PlasticCube },
                    { "breaktest", Scene::BreakTest },
                };
                for (const auto& [n, sc] : kNames) if (sceneName == n) spawnScene(sc);
                if (end == std::string::npos) break;
                start = end + 1;
            }
        }
    }

    double stepStart = nowSeconds();
    AMD::FmUpdateScene(m_scene, ctx.fixedDt);
    ++m_physicsTick;
    updateBreakables();
    enforceDebrisBudget();
    double stepMs = (nowSeconds() - stepStart) * 1000.0;
    for (auto& [handle, obj] : m_objects) {
        if (!obj->armPending) continue;
        ++obj->armAge;
        // Peak stress per tet over the whole settling window, not just the
        // last sample: FEMFX may put an object to sleep before it has even
        // landed (it slept 1 cm above the floor in one test), and a stale
        // sample armed it too weakly.
        for (uint32_t t = 0; t < obj->numTets; ++t)
            obj->settleStress[t] = std::max(obj->settleStress[t] * 0.9f, AMD::FmGetTetMaxStress(*obj->tetMesh, t)); // decaying peak, see updateBreakables()
        const bool settled = AMD::FmIsTetMeshSleeping(*obj->tetMesh);
        if (obj->armAge < 45 || (!settled && obj->armAge < obj->armMaxTicks)) continue;
        // Arm relative to the settled state (see TetSpawnOptions).
        constexpr float kRestStressFactor = 1.25f;
        float restMax = 0.0f;
        for (uint32_t t = 0; t < obj->numTets; ++t) {
            AMD::FmTetMaterialParams p = obj->armedParams;
            if (t < obj->tetStrength.size()) p.fractureStressThreshold *= obj->tetStrength[t];
            p.fractureStressThreshold += kRestStressFactor * obj->settleStress[t];
            AMD::FmUpdateTetMaterialParams(m_scene, obj->tetMesh, t, p);
            restMax = std::max(restMax, obj->settleStress[t]);
        }
        obj->armPending = false;
        obj->settleStress = {};
        log::get(name())->info("object {} armed after {} ticks ({}): settling stress max {:.0f}, base threshold {:.0f}", handle, obj->armAge,
                               settled ? "settled" : "deadline", restMax, obj->armedParams.fractureStressThreshold);
    }
    m_timing.ticks++;
    m_timing.stepMsTotal += stepMs;
    m_timing.stepMsMax = std::max(m_timing.stepMsMax, stepMs);
    // FEMFX's own record of any capacity it ran out of this step (see
    // init()'s comment on scene capacities). Collected here, logged once
    // a second by publishTimingWindow(), then cleared.
    AMD::FmWarningsReport& warnings = AMD::FmGetSceneWarningsReportRef(m_scene);
    if (warnings.flags.val) {
        m_lastWarningFlags |= warnings.flags.val;
        warnings.flags.val = 0;
    }
    if (m_benchTicks && !m_benchDone) {
        m_benchStepMsTotal += stepMs;
        m_benchStepMsMax = std::max(m_benchStepMsMax, stepMs);
        m_benchStepSamples.push_back(stepMs);
    }

    // Once a second (at 60 Hz): log changes, catch runaways. Only
    // *changes* are logged — the old version repeated every object's
    // piece count every second, which buried everything else in the log.
    if (!m_objects.empty() && ctx.tickIndex % 60 == 0) {
        std::vector<ObjectHandle> runaways;
        for (auto& [handle, obj] : m_objects) {
            const uint32_t numPieces = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
            if (obj->fracturable && numPieces > obj->loggedPieces && obj->loggedPieces > 0) {
                const AMD::FmVector3 lo = AMD::FmGetMinPosition(*obj->tetMesh), hi = AMD::FmGetMaxPosition(*obj->tetMesh);
                BreakEvent e;
                e.position = glm::vec3((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f) * m_renderScale;
                e.material = obj->material;
                e.newPieces = numPieces - obj->loggedPieces;
                e.size = std::max({hi.x - lo.x, hi.y - lo.y, hi.z - lo.z}) * m_renderScale;
                m_frameBreaks.push_back(e);
            }
            if (obj->fracturable && numPieces != obj->loggedPieces) {
                log::get(name())->info("object {} has split into {} pieces", handle, numPieces);
                obj->loggedPieces = numPieces;
            }
            // Runaway guard: FEMFX can blow up on badly conditioned input
            // (BUG-043: a crate flew to 15,000 km). One exploded object
            // would otherwise stay awake forever, costing CPU and waking
            // anything it touches. Anything non-finite or beyond 10 km is
            // removed and reported.
            for (uint32_t m = 0; m < numPieces; ++m) {
                const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
                if (!piece) continue;
                AMD::FmVector3 lo = AMD::FmGetMinPosition(*piece), hi = AMD::FmGetMaxPosition(*piece);
                float extent = std::max({ std::fabs(lo.x), std::fabs(lo.y), std::fabs(lo.z), std::fabs(hi.x), std::fabs(hi.y), std::fabs(hi.z) });
                if (!std::isfinite(extent) || extent > 1.0e4f) { runaways.push_back(handle); break; }
            }
            // Plasticity check for the demo's plastic cube (vertices 0 and
            // 1 start 1.0 apart; a different length at rest = permanent
            // dent). Debug level: useful when tuning, noise otherwise.
            if (obj->plastic && obj->numVerts >= 2) {
                AMD::FmVector3 p0 = AMD::FmGetVertPosition(*obj->tetMesh, 0);
                AMD::FmVector3 p1 = AMD::FmGetVertPosition(*obj->tetMesh, 1);
                float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
                log::get(name())->debug("plastic object {} vert0-vert1 distance: {:.4f} (started at 1.0000)", handle, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        }
        for (ObjectHandle h : runaways) {
            log::get(name())->warn("object {} exploded (non-finite or > 10 km away) and was removed - see BUGS.md BUG-043", h);
            removeObject(h);
        }
    }
}

void PhysicsModule::prepareRenderData(uint32_t frameIndex) {
    if (m_preparedFrame == m_frameCounter) return;
    m_preparedFrame = m_frameCounter;
    double start = nowSeconds();

    m_awakeObjects = 0;
    m_renderedFaces = 0;
    m_totalPieces = 0;
    m_awakePieces = 0;
    static thread_local std::vector<glm::vec3> simPositions;
    static thread_local std::vector<glm::vec3> restPositions;

    for (auto& [handle, obj] : m_objects) {
        const uint32_t numPieces = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        m_totalPieces += numPieces;

        bool anyAwake = obj->cpuVersion == 0; // never built yet: build once regardless
        for (uint32_t m = 0; m < numPieces; ++m) {
            const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (piece && !AMD::FmIsTetMeshSleeping(*piece)) {
                anyAwake = true;
                ++m_awakePieces;
            }
        }
        obj->asleep = !anyAwake;

        if (anyAwake) {
            ++m_awakeObjects;
            obj->cpuVerts.clear();
            bool truncated = false;
            // Every piece goes into the same buffer back to back and is
            // drawn with one call. The old path uploaded each fracture
            // piece to offset 0 of the same buffer and recorded a draw
            // per piece — but the GPU only runs those draws after the
            // CPU has finished all the uploads, so every draw saw the
            // *last* piece's data. A shattered object rendered as one
            // piece drawn N times plus stale leftovers.
            // Crack-only objects: which original tet each piece-local tet
            // was, so faces that were on the original surface (drawn by the
            // embedded render mesh instead) can be skipped.
            static thread_local std::vector<std::vector<uint32_t>> bufferTetOf;
            if (obj->drawOnlyCracks) {
                bufferTetOf.assign(numPieces, {});
                for (uint32_t t = 0; t < obj->numTets; ++t) {
                    uint32_t localTet = 0, meshIdx = 0;
                    if (!AMD::FmGetTetMeshContainingTet(&localTet, &meshIdx, *obj->tetMeshBuffer, t) || meshIdx >= numPieces) continue;
                    auto& map = bufferTetOf[meshIdx];
                    if (map.size() <= localTet) map.resize(localTet + 1, UINT32_MAX);
                    map[localTet] = t;
                }
            }
            for (uint32_t m = 0; m < numPieces; ++m) {
                const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
                if (!piece) continue;
                const uint32_t numVerts = AMD::FmGetNumVerts(*piece);
                const uint32_t numFaces = AMD::FmGetNumExteriorFaces(*piece);
                if (numVerts == 0 || numFaces == 0) continue;

                simPositions.resize(numVerts);
                restPositions.resize(numVerts);
                for (uint32_t i = 0; i < numVerts; ++i) {
                    AMD::FmVector3 p = AMD::FmGetVertPosition(*piece, i);
                    simPositions[i] = glm::vec3(p.x, p.y, p.z) * m_renderScale;
                    AMD::FmVector3 r = AMD::FmGetVertRestPosition(*piece, i);
                    restPositions[i] = glm::vec3(r.x, r.y, r.z);
                }

                for (uint32_t f = 0; f < numFaces; ++f) {
                    if (obj->cpuVerts.size() + 3 > obj->maxRenderVerts) { truncated = true; break; } // see below
                    uint32_t tetId = 0, faceId = 0;
                    AMD::FmGetExteriorFace(&tetId, &faceId, *piece, f);
                    if (obj->drawOnlyCracks) {
                        const auto& map = bufferTetOf[m];
                        uint32_t bt = tetId < map.size() ? map[tetId] : UINT32_MAX;
                        if (bt == UINT32_MAX || (obj->originalExterior[bt] & (1u << faceId))) continue;
                    }
                    AMD::FmTetVertIds ids = AMD::FmGetTetVertIds(*piece, tetId);
                    // FEMFX's own face numbering (FmGetFaceVertIds in
                    // FEMFXTetMeshConnectivity.h) — the same winding the
                    // old hardcoded 4-face table used.
                    const uint32_t ia = ids.ids[3 - faceId], ib = ids.ids[(5 - faceId) % 4], ic = ids.ids[(faceId + 2) % 4];
                    glm::vec3 a = simPositions[ia];
                    glm::vec3 b = simPositions[ib];
                    glm::vec3 c = simPositions[ic];
                    glm::vec3 n = glm::cross(b - a, c - a);
                    float len = glm::length(n);
                    n = len > 1e-12f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
                    // Box-projected UVs from REST positions (BUGS.md
                    // BUG-036): project onto the plane most facing this
                    // face in the object's undeformed shape. Texture flows
                    // continuously across a whole side, stays glued to the
                    // material as it moves/bends, and crack faces get it too.
                    // (Per-triangle 0..1 UVs made every intact box look
                    // like a mosaic of shards before anything broke.)
                    const glm::vec3 ra = restPositions[ia], rb = restPositions[ib], rc = restPositions[ic];
                    glm::vec3 rn = glm::abs(glm::cross(rb - ra, rc - ra));
                    auto project = [&](const glm::vec3& r) {
                        constexpr float kTexelsPerMeter = 1.5f; // texture repeats every ~0.67 m
                        if (rn.x >= rn.y && rn.x >= rn.z) return glm::vec2(r.z, r.y) * kTexelsPerMeter;
                        if (rn.y >= rn.z) return glm::vec2(r.x, r.z) * kTexelsPerMeter;
                        return glm::vec2(r.x, r.y) * kTexelsPerMeter;
                    };
                    glm::vec2 ua = project(ra), ub = project(rb), uc = project(rc);
                    if (!obj->vertexUVs.empty() && obj->drawOnlyCracks) {
                        // Per-vertex UVs of the original tet corners (same
                        // corner order in every piece).
                        const auto& ov = obj->tetVertIds[bufferTetOf[m][tetId]].ids;
                        ua = obj->vertexUVs[ov[3 - faceId]];
                        ub = obj->vertexUVs[ov[(5 - faceId) % 4]];
                        uc = obj->vertexUVs[ov[(faceId + 2) % 4]];
                    }
                    // Fresh crack faces (inside the object before it
                    // broke) are a little darker: RayFire's "inner
                    // material" - broken edges read as broken.
                    glm::vec3 color = obj->color;
                    if (!obj->originalExterior.empty() && tetId < obj->originalExterior.size() && obj->breakable != kInvalidHandle &&
                        !(obj->originalExterior[tetId] & (1u << faceId)))
                        color *= 0.72f;
                    obj->cpuVerts.push_back({ a, color, n, ua });
                    obj->cpuVerts.push_back({ b, color, n, ub });
                    obj->cpuVerts.push_back({ c, color, n, uc });
                }
            }
            if (truncated) {
                // Should be impossible (maxExteriorFaces is FEMFX's own
                // upper bound including fracture); logged rather than
                // silently truncated if it ever does happen.
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    log::get(name())->warn("object {} hit its render capacity ({} verts) -- some faces not drawn", handle, obj->maxRenderVerts);
                }
            }
            ++obj->cpuVersion;
        }

        if (obj->gpuVersion[frameIndex] != obj->cpuVersion && !obj->cpuVerts.empty()) {
            obj->vertexBuffers[frameIndex]->upload(obj->cpuVerts.data(), obj->cpuVerts.size() * sizeof(Vertex));
            obj->gpuVersion[frameIndex] = obj->cpuVersion;
        }
        m_renderedFaces += static_cast<uint32_t>(obj->cpuVerts.size() / 3);
    }

    m_pendingPrepMs += (nowSeconds() - start) * 1000.0;
}

void PhysicsModule::renderShadow(const ShadowRenderContext& ctx) {
    // Same frame's data as render() — prepareRenderData() runs from
    // whichever of the two is called first (this one, in Application's
    // frame loop), so shadows no longer lag a frame behind the objects.
    prepareRenderData(ctx.frameIndex);

    m_shadowPipeline->bind(ctx.cmd);
    ShadowPushConstants pc{ ctx.lightViewProj, glm::mat4(1.0f) };
    vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    for (auto& [handle, obj] : m_objects) {
        if (obj->cpuVerts.empty() || obj->gpuVersion[ctx.frameIndex] != obj->cpuVersion) continue;
        VkBuffer buffers[] = { obj->vertexBuffers[ctx.frameIndex]->handle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
        vkCmdDraw(ctx.cmd, static_cast<uint32_t>(obj->cpuVerts.size()), 1, 0, 0);
    }
}

void PhysicsModule::render(const RenderContext& ctx) {
    if (!m_pipeline) {
        return;
    }
    double prepStart = nowSeconds();
    struct PrepTimer {
        PhysicsModule* self; double start;
        ~PrepTimer() {
            double now = nowSeconds();
            double ms = (now - start) * 1000.0 + self->m_pendingPrepMs;
            self->m_pendingPrepMs = 0.0;
            self->m_timing.frames++;
            self->m_timing.renderPrepMsTotal += ms;
            if (self->m_benchTicks && !self->m_benchDone && self->m_benchStartSeconds > 0.0) {
                self->m_benchFrames++;
                self->m_benchRenderPrepMsTotal += ms;
            }
            if (now - self->m_timingWindowStart >= 1.0) self->publishTimingWindow(now);
        }
    } prepTimer{ this, prepStart };

    m_pipeline->bind(ctx.cmd);
    VkDescriptorSet sets[] = { ctx.lightingDescriptorSet, ctx.shadowMapDescriptorSet, ctx.defaultMaterialTextureDescriptorSet };
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                             0, 3, sets, 0, nullptr);

    // Resolves a spawned object's own material to its real texture's
    // descriptor set (see the material texture library built in
    // init()), falling back to the shared default white texture for
    // any material that doesn't set a valid textureId — the same
    // "unset/out-of-range means untextured" convention Material.h's
    // own textureId field documents. Rebinding just set 2 per object
    // (firstSet=2, count=1) rather than all three sets again — 0 and 1
    // (lighting, shadow) never change between objects in this loop.
    auto bindMaterialTexture = [&](const Material& material, VkDescriptorSet textureOverride) {
        VkDescriptorSet textureSet = ctx.defaultMaterialTextureDescriptorSet;
        if (textureOverride) {
            textureSet = textureOverride;
        } else if (material.textureId >= 0 && material.textureId < static_cast<int>(m_materialTextures.size())) {
            textureSet = m_materialTextures[material.textureId].descriptorSet;
        }
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                                 2, 1, &textureSet, 0, nullptr);
    };

    // Ground plane — a unit cube scaled to a visually-proportionate
    // size for whatever camera this demo uses, positioned to match
    // the real physics ground's location. Deliberately NOT the full
    // 100-unit width of the actual physics collision volume: at
    // renderScale=1.0 (see games/physics_demo), a camera close enough
    // to clearly frame a handful of falling objects is also close
    // enough that a full 100-unit floor fills the entire view with one
    // flat color — confirmed by bisection (disabling just this draw
    // fixed it), the same class of issue found and fixed earlier in
    // this class's history at a different scale. The *collision*
    // volume the physics ground rigid body actually uses is untouched
    // by this — this only ever affects what gets drawn.
    {
        // Width and thickness computed BEFORE the translation, not
        // after — a real, reported bug (objects visually resting well
        // above the floor) was exactly this ordering mistake: the old
        // code translated the box to a fixed y=-0.5*renderScale first,
        // then scaled its thickness down via the clamp below, leaving
        // the box's TOP surface wherever that fixed translate minus
        // half the (now much thinner) scaled thickness landed — at
        // renderScale=1.0 that's y=-0.475, not y=0, a visible ~0.475
        // unit gap between the floor an object actually rests on
        // (real physics surface, y=0.002) and where the floor was
        // drawn. Fixed by computing the thickness first and deriving
        // the translation FROM it, so the rendered top surface is
        // always exactly y=0 regardless of how the clamp scales it.
        if (m_drawGround) {
        float groundWidth = glm::clamp(100.0f * m_renderScale, 0.5f, 10.0f);
        float groundThickness = glm::clamp(0.05f * m_renderScale, 0.0005f, 0.15f);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f * groundThickness, 0.0f));
        // A thin *visual* slab, not matching the physics ground's full
        // 1-unit collision thickness — purely so a flat floor reads as
        // a floor rather than a thick block from any angle.
        model = glm::scale(model, glm::vec3(groundWidth, groundThickness, groundWidth));
        // A matte, non-metallic ground plane (concrete/stone-like) --
        // a fixed, reasonable default rather than something read from
        // a Material, since the ground isn't spawned through the same
        // Material-driven path every other object here is.
        PhysicsPushConstants pc{ model, 0.0f, 0.9f };
        vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        m_groundMesh->bind(ctx.cmd);
        m_groundMesh->draw(ctx.cmd);
        }
    }

    if (m_showRagdollBodies) {
        vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(), 2, 1,
                                 &ctx.defaultMaterialTextureDescriptorSet, 0, nullptr);
        m_groundMesh->bind(ctx.cmd);
        std::vector<glm::mat4> transforms;
        for (auto& [handle, rd] : m_ragdolls) {
            ragdollBodyTransforms(handle, transforms);
            for (size_t i = 0; i < transforms.size(); ++i) {
                glm::mat4 m = glm::scale(transforms[i], rd.halfExtents[i] * 2.0f * m_renderScale);
                m[3] = glm::vec4(glm::vec3(transforms[i][3]) * m_renderScale, 1.0f);
                PhysicsPushConstants pc{ m, 0.0f, 0.6f };
                vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
                m_groundMesh->draw(ctx.cmd);
            }
        }
    }

    // Every spawned object — each one's simulated vertex positions are
    // already in world space, so the model matrix is identity for all
    // of them; see the class comment for why this is the simplest
    // possible render bridge, not a real skinning one. metallic/
    // roughness, unlike model, genuinely differ per-object now (see
    // Material.h) -- pushed inside the loop below, once per object,
    // rather than once here outside it.

    prepareRenderData(ctx.frameIndex);
    for (auto& [handle, obj] : m_objects) {
        if (obj->cpuVerts.empty() || obj->gpuVersion[ctx.frameIndex] != obj->cpuVersion) continue;
        bindMaterialTexture(obj->material, obj->textureSet);
        PhysicsPushConstants pc{ glm::mat4(1.0f), obj->material.metallic, obj->material.roughness };
        vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkBuffer buffers[] = { obj->vertexBuffers[ctx.frameIndex]->handle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
        vkCmdDraw(ctx.cmd, static_cast<uint32_t>(obj->cpuVerts.size()), 1, 0, 0);
    }
    ++m_frameCounter;
}

PhysicsModule::RagdollHandle PhysicsModule::createRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity) {
    if (!m_scene || desc.bodies.empty()) return 0;
    size_t used = 0;
    for (auto& [h, rd] : m_ragdolls) used += rd.bodies.size();
    if (used + desc.bodies.size() > kMaxRigidBodies) {
        log::get(name())->warn("createRagdoll: would exceed {} rigid bodies", kMaxRigidBodies);
        return 0;
    }
    RagdollInstance rd;
    for (const RagdollBody& b : desc.bodies) {
        glm::quat q = glm::quat_cast(glm::mat3(b.transform));
        AMD::FmRigidBodySetupParams params;
        params.state.pos = AMD::FmInitVector3(b.transform[3].x, b.transform[3].y, b.transform[3].z);
        params.state.quat = AMD::FmInitQuat(q.x, q.y, q.z, q.w);
        params.state.vel = AMD::FmInitVector3(initialVelocity.x, initialVelocity.y, initialVelocity.z);
        params.halfDimX = b.halfExtents.x;
        params.halfDimY = b.halfExtents.y;
        params.halfDimZ = b.halfExtents.z;
        params.mass = b.mass;
        params.bodyInertiaTensor = AMD::FmComputeBodyInertiaTensorForBox(b.halfExtents.x, b.halfExtents.y, b.halfExtents.z, b.mass);
        params.collisionGroup = static_cast<uint8_t>(kRagdollCollisionGroup);
        AMD::FmRigidBody* body = AMD::FmCreateRigidBody(params);
        if (!body) {
            log::get(name())->error("createRagdoll: FmCreateRigidBody failed for '{}'", b.name);
            continue;
        }
        uint32_t id = AMD::FmAddRigidBodyToScene(m_scene, body);
        AMD::FmEnableSleeping(m_scene, body, true);
        rd.bodies.push_back(body);
        rd.bodyIds.push_back(id);
        rd.halfExtents.push_back(b.halfExtents);
    }
    if (rd.bodies.size() != desc.bodies.size()) {
        for (size_t i = 0; i < rd.bodies.size(); ++i) {
            AMD::FmRemoveRigidBodyFromScene(m_scene, rd.bodyIds[i]);
            AMD::FmDestroyRigidBody(rd.bodies[i]);
        }
        return 0;
    }
    auto bodySpace = [&](int body, glm::vec3 worldPoint) {
        const glm::mat4& t = desc.bodies[body].transform;
        return glm::transpose(glm::mat3(t)) * (worldPoint - glm::vec3(t[3]));
    };
    for (const RagdollJoint& j : desc.joints) {
        // Ball joint: pin the same world point on both bodies. Settings
        // follow AMD's own car sample (TestScenes.cpp): full correction.
        AMD::FmGlueConstraintSetupParams glue;
        glue.bufferIdA = rd.bodyIds[j.bodyA];
        glue.bufferIdB = rd.bodyIds[j.bodyB];
        glm::vec3 a = bodySpace(j.bodyA, j.anchor), b = bodySpace(j.bodyB, j.anchor);
        glue.posBodySpaceA[0] = a.x; glue.posBodySpaceA[1] = a.y; glue.posBodySpaceA[2] = a.z; glue.posBodySpaceA[3] = 1.0f;
        glue.posBodySpaceB[0] = b.x; glue.posBodySpaceB[1] = b.y; glue.posBodySpaceB[2] = b.z; glue.posBodySpaceB[3] = 1.0f;
        glue.kVelCorrection = 1.0f;
        glue.kPosCorrection = 1.0f;
        rd.glueIds.push_back(AMD::FmAddGlueConstraintToScene(m_scene, glue));
        if (j.hinge) {
            AMD::FmRigidBodyAngleConstraintSetupParams hinge;
            hinge.objectIdA = rd.bodyIds[j.bodyA];
            hinge.objectIdB = rd.bodyIds[j.bodyB];
            glm::vec3 axA = glm::transpose(glm::mat3(desc.bodies[j.bodyA].transform)) * j.hingeAxis;
            glm::vec3 axB = glm::transpose(glm::mat3(desc.bodies[j.bodyB].transform)) * j.hingeAxis;
            hinge.axisBodySpaceA = AMD::FmInitVector3(axA.x, axA.y, axA.z);
            hinge.axisBodySpaceB = AMD::FmInitVector3(axB.x, axB.y, axB.z);
            hinge.kVelCorrection = 1.0f;
            hinge.kPosCorrection = 1.0f;
            hinge.frictionCoeff = 0.4f;
            rd.hingeIds.push_back(AMD::FmAddRigidBodyAngleConstraintToScene(m_scene, hinge));
        }
    }
    RagdollHandle handle = m_nextRagdoll++;
    log::get(name())->info("ragdoll {}: {} bodies, {} joints ({} hinges)", handle, rd.bodies.size(), rd.glueIds.size(), rd.hingeIds.size());
    m_ragdolls[handle] = std::move(rd);
    return handle;
}

void PhysicsModule::destroyRagdoll(RagdollHandle handle) {
    auto it = m_ragdolls.find(handle);
    if (it == m_ragdolls.end() || !m_scene) return;
    RagdollInstance& rd = it->second;
    for (uint32_t id : rd.hingeIds) AMD::FmRemoveRigidBodyAngleConstraintFromScene(m_scene, id);
    for (uint32_t id : rd.glueIds) AMD::FmRemoveGlueConstraintFromScene(m_scene, id);
    for (size_t i = 0; i < rd.bodies.size(); ++i) {
        AMD::FmRemoveRigidBodyFromScene(m_scene, rd.bodyIds[i]);
        AMD::FmDestroyRigidBody(rd.bodies[i]);
    }
    m_ragdolls.erase(it);
}

bool PhysicsModule::ragdollBodyTransforms(RagdollHandle handle, std::vector<glm::mat4>& out) const {
    auto it = m_ragdolls.find(handle);
    if (it == m_ragdolls.end()) return false;
    out.resize(it->second.bodies.size());
    for (size_t i = 0; i < out.size(); ++i) {
        AMD::FmVector3 p = AMD::FmGetPosition(*it->second.bodies[i]);
        AMD::FmQuat q = AMD::FmGetRotation(*it->second.bodies[i]);
        glm::mat4 m = glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));
        m[3] = glm::vec4(p.x, p.y, p.z, 1.0f);
        out[i] = m;
    }
    return true;
}

void PhysicsModule::pushRagdollBody(RagdollHandle handle, int body, const glm::vec3& dv) {
    auto it = m_ragdolls.find(handle);
    if (it == m_ragdolls.end() || body < 0 || body >= static_cast<int>(it->second.bodies.size())) return;
    AMD::FmRigidBody* rb = it->second.bodies[body];
    AMD::FmVector3 v = AMD::FmGetVelocity(*rb);
    AMD::FmSetVelocity(m_scene, rb, AMD::FmInitVector3(v.x + dv.x, v.y + dv.y, v.z + dv.z));
}

void PhysicsModule::publishTimingWindow(double now) {
    double window = now - m_timingWindowStart;
    m_lastStepMsAvg = m_timing.ticks ? m_timing.stepMsTotal / m_timing.ticks : 0.0;
    m_lastStepMsMax = m_timing.stepMsMax;
    m_lastRenderPrepMsAvg = m_timing.frames ? m_timing.renderPrepMsTotal / m_timing.frames : 0.0;
    m_lastTicksPerSecond = static_cast<float>(m_timing.ticks / window);
    m_lastFramesPerSecond = static_cast<float>(m_timing.frames / window);
    log::get(name())->info("perf: {:.1f} fps, {:.1f} ticks/s, step avg {:.2f} ms max {:.2f} ms, render prep {:.2f} ms, "
                           "{} object(s) / {} piece(s), {} awake piece(s), {} faces drawn",
                           m_lastFramesPerSecond, m_lastTicksPerSecond, m_lastStepMsAvg, m_lastStepMsMax,
                           m_lastRenderPrepMsAvg, m_objects.size(), m_totalPieces, m_awakePieces, m_renderedFaces);
    if (m_lastWarningFlags) {
        log::get(name())->warn("FEMFX hit a scene capacity limit this second (FM_WARNING_FLAG_* = 0x{:x}, see AMD_FEMFX.h) -- "
                               "contacts/pieces beyond it were dropped", m_lastWarningFlags);
        m_lastWarningFlags = 0;
    }
    if (m_benchTicks && !m_benchDone && m_benchStartSeconds > 0.0) {
        m_benchWindows.push_back({ now - m_benchStartSeconds, m_lastFramesPerSecond, m_lastTicksPerSecond, m_lastStepMsAvg,
                                   m_lastStepMsMax, m_lastRenderPrepMsAvg, static_cast<double>(m_totalPieces),
                                   static_cast<double>(m_awakePieces) });
    }
    m_timing = TimingWindow{};
    m_timingWindowStart = now;
}

// The fixed schedule: every scene this module has, a second apart in
// simulation time, so the run covers fracture, plasticity, bouncing and
// a pile-up of debris all at once by the end. Keyed on tickIndex rather
// than wall-clock time so a slow machine sees the exact same simulation,
// just more slowly — which is what makes the numbers comparable.
void PhysicsModule::benchTick(uint64_t tickIndex) {
    if (m_benchDone) return;
    if (m_benchStartSeconds == 0.0) m_benchStartSeconds = nowSeconds();
    switch (tickIndex) {
        case 30:  spawnScene(Scene::GlassSheet); break;
        case 90:  spawnScene(Scene::Brick); break;
        case 150: spawnScene(Scene::CarCrash); break;
        case 210: spawnScene(Scene::LavaMelt); break;
        case 270: spawnScene(Scene::RubberBall); break;
        case 330: spawnScene(Scene::FracturableCube); break;
        case 360: spawnScene(Scene::GlassSheet); break;
        case 390: spawnScene(Scene::FracturableCube); break;
        case 420: spawnScene(Scene::Brick); break;
        default: break;
    }
    if (tickIndex < m_benchTicks) return;

    m_benchDone = true;
    double wall = nowSeconds() - m_benchStartSeconds;
    uint32_t totalTets = 0, pieces = 0;
    for (auto& [handle, obj] : m_objects) {
        uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        pieces += n;
        for (uint32_t m = 0; m < n; ++m) totalTets += AMD::FmGetNumTets(*AMD::FmGetTetMesh(*obj->tetMeshBuffer, m));
    }
    log::get(name())->info(
        "BENCH RESULT: {} ticks in {:.2f} s wall ({:.2f}x realtime), {} frames ({:.1f} fps avg), "
        "step avg {:.2f} ms max {:.2f} ms, render prep avg {:.2f} ms, {} objects / {} pieces / {} tets",
        m_benchTicks, wall, (m_benchTicks / 60.0) / wall, m_benchFrames, m_benchFrames / wall,
        m_benchStepMsTotal / m_benchTicks, m_benchStepMsMax,
        m_benchFrames ? m_benchRenderPrepMsTotal / m_benchFrames : 0.0,
        m_objects.size(), pieces, totalTets);
    writeBenchReport(wall, pieces, totalTets);
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

// Writes benchmark/physics_<time>_<host>.{json,txt} (or $KKE_BENCH_DIR) —
// see kke/BenchmarkReport.h. Paste either file back for analysis.
void PhysicsModule::writeBenchReport(double wall, uint32_t pieces, uint32_t totalTets) {
    BenchmarkReport r;
    r.name = "physics";
    r.system = collectSystemInfo(m_app->device().physicalDevice());
    auto env = [](const char* k) { const char* v = std::getenv(k); return std::string(v ? v : ""); };
    r.config = {
        { "scene", "scripted: glass, brick, car crash, lava, rubber ball, fracture cubes (PhysicsModule::benchTick)" },
        { "ticks", std::to_string(m_benchTicks) },
        { "fixed_hz", "60" },
        { "physics_worker_threads", std::to_string(m_workerThreads) },
        { "hardware_concurrency", std::to_string(m_hardwareThreads) },
        { "KKE_PHYSICS_THREADS", env("KKE_PHYSICS_THREADS").empty() ? "(unset)" : env("KKE_PHYSICS_THREADS") },
        { "max_fixed_steps_per_frame", std::to_string(m_app->maxFixedStepsPerFrame()) },
        { "render_scale", std::to_string(m_renderScale) },
    };
    r.sampleColumns = { "t_s", "fps", "ticks_per_s", "step_avg_ms", "step_max_ms", "render_prep_ms", "pieces", "awake_pieces" };
    r.samples = m_benchWindows;
    double stepAvg = m_benchTicks ? m_benchStepMsTotal / m_benchTicks : 0.0;
    r.results = {
        { "wall_s", wall },
        { "realtime_factor", (m_benchTicks / 60.0) / wall },
        { "frames", static_cast<double>(m_benchFrames) },
        { "fps_avg", m_benchFrames / wall },
        { "step_avg_ms", stepAvg },
        { "step_p50_ms", percentile(m_benchStepSamples, 50) },
        { "step_p95_ms", percentile(m_benchStepSamples, 95) },
        { "step_p99_ms", percentile(m_benchStepSamples, 99) },
        { "step_max_ms", m_benchStepMsMax },
        { "render_prep_avg_ms", m_benchFrames ? m_benchRenderPrepMsTotal / m_benchFrames : 0.0 },
        { "objects", static_cast<double>(m_objects.size()) },
        { "pieces", static_cast<double>(pieces) },
        { "tets", static_cast<double>(totalTets) },
        { "peak_rss_mb", peakResidentMemoryMb() },
    };
    r.notes = {
        "realtime_factor 1.0 = the simulation kept up with wall-clock time the whole run.",
        "step_* = FEMFX FmUpdateScene per fixed tick; render_prep = CPU time building physics vertex data per frame.",
    };
    std::string dir = env("KKE_BENCH_DIR").empty() ? "benchmark" : env("KKE_BENCH_DIR");
    std::string base = r.writeFiles(dir, timestampForFileName(), hostNameForFileName());
    if (base.empty()) log::get(name())->error("BENCH: could not write report to '{}'", dir);
    else log::get(name())->info("BENCH REPORT: {}.txt and .json", base);
}

void PhysicsModule::spawnScene(Scene scene) {
    switch (scene) {
    case Scene::FracturableCube: {
        // Now built through the same general buildGridBox() every new
        // scene shape below uses too, instead of its own separate
        // inline grid-generation code -- see that function's own
        // header comment for the full account of why one generator
        // now serves every box-like shape this class spawns.
        TetMeshData cube = buildGridBox(2, 2, 2, 1.0f, 1.0f, 1.0f);

        float jitterX = static_cast<float>((m_nextHandle * 41) % 200) / 100.0f - 1.0f;
        float jitterZ = static_cast<float>((m_nextHandle * 59) % 200) / 100.0f - 1.0f;

        // The real, currently-selected material now, not a hardcoded
        // one — the actual bug being fixed here: this button
        // previously ignored kke::MaterialGridModule's selection
        // entirely, so choosing "Glass" and clicking this never made
        // it shatter any differently than "Rubber" would have, despite
        // real, distinct fractureStressThreshold values sitting right
        // there on m_selectedMaterial the whole time -- confirmed via
        // real testing after the fix (see README "Real fracture
        // support"), not assumed correct from the code alone.
        Material material = m_selectedMaterial;

        spawnFracturableTetMesh(cube, glm::vec3(jitterX * 3.0f, m_nextSpawnHeight + 3.0f, jitterZ * 3.0f), material,
                                 glm::vec3(0.0f, -25.0f, 0.0f));
        break;
    }
    case Scene::PlasticCube: {
        // Same real 6-tet cube decomposition as the fracture button
        // above — see its own comment for why a single tetrahedron
        // wouldn't show anything meaningful here either: plasticity is
        // visible as a change in the object's overall shape, which a
        // single, always-convex tetrahedron doesn't display nearly as
        // legibly as a cube's flat faces and sharp edges do.
        TetMeshData cube;
        cube.vertices = {
            {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f},
        };
        cube.tets = {
            {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
            {0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6},
        };

        float jitterX = static_cast<float>((m_nextHandle * 47) % 200) / 100.0f - 1.0f;
        float jitterZ = static_cast<float>((m_nextHandle * 61) % 200) / 100.0f - 1.0f;

        Material material;
        material.density = 500.0f;
        material.stiffness = 5.0e6f;
        material.poissonsRatio = 0.3f;
        // fractureStressThreshold left at the real "doesn't fracture"
        // default deliberately — the point of this button is isolating
        // and observing PURE plastic deformation, not a mix of denting
        // and breaking that would make it hard to tell which effect
        // produced what's on screen.
        material.fractureStressThreshold = 1.0e8f;
        // Empirically tuned the same way the fracture threshold above
        // was — not AMD's own reference value (2.5e6, see
        // external/FEMFX/samples/common/TestScenes.cpp) taken on
        // faith, given this project's own actual stress values already
        // confirmed to run orders of magnitude smaller than AMD's
        // examples for fracture. Verified with a real, corrected
        // diagnostic, not FmGetVertRestPosition() (the first attempt —
        // wrong signal, confirmed by reading FEMFX's own source:
        // plasticity is tracked as a per-tet plasticDeformationMatrix,
        // not a change to the vertex rest-position array, and that
        // internal state has no public accessor at all). What's
        // directly observable instead: the distance between this
        // cube's own vertex 0 and vertex 1, exactly 1.0 unit apart at
        // spawn. Confirmed via that measurement growing from 1.0000 to
        // over 1.02 across a real run and never springing back — real,
        // permanent deformation, not elastic settling still in
        // progress. The growth rate genuinely decelerates over time
        // (each second's increase smaller than the last) rather than
        // instantly snapping to a final shape or diverging unbounded —
        // consistent with plasticCreep's own documented meaning (how
        // much permanent deformation accumulates *per unit of excess
        // stress*, a rate, not a one-time jump), not a bug. AMD's own
        // 1.0 for plasticCreep kept as a reasonable starting point,
        // not itself re-tuned.
        material.plasticYieldThreshold = 2.0f;
        material.plasticCreep = 1.0f;

        spawnPlasticTetMesh(cube, glm::vec3(jitterX * 3.0f, m_nextSpawnHeight + 3.0f, jitterZ * 3.0f), material,
                             glm::vec3(0.0f, -25.0f, 0.0f));
        break;
    }
    case Scene::GlassSheet: {
        // A 2 m pane, 0.15 m thick, breaking in a radial star
        // (kke::FracturePattern::Radial): small shards near the impact,
        // long wedges further out. 12x1x12 cells, 864 tets. It used to
        // crack along every tet face (every piece a same-size triangle).
        Material glass;
        glass.density = 2500.0f;
        glass.stiffness = 7.0e7f;
        glass.poissonsRatio = 0.22f;
        glass.fractureStressThreshold = 2000.0f; // same real, measured glass value as MaterialGridModule's own preset -- see that preset's own comment for the full empirical account
        glass.plasticYieldThreshold = 1800.0f;
        glass.plasticCreep = 0.02f;
        glass.metallic = 0.0f;
        glass.roughness = 0.05f;
        glass.textureId = 4; // matches PhysicsModule's own glass texture in the material library built in init()

        float jitterX = static_cast<float>((m_nextHandle * 43) % 200) / 100.0f - 1.0f;
        float jitterZ = static_cast<float>((m_nextHandle * 71) % 200) / 100.0f - 1.0f;
        // Breakables compare FEMFX's per-tet stress (Pa) at piece borders:
        // ~400 in free fall with spikes to ~60k, 1e6-1e7 on a 20 m/s
        // landing (tools/physics_lab). 150k: never in the air, always on
        // landing.
        glass.fractureStressThreshold = 1.5e5f;
        spawnPatternedBox({ 12, 1, 12 }, { 2.0f, 0.15f, 2.0f }, glm::vec3(jitterX * 2.0f, m_nextSpawnHeight + 3.0f, jitterZ * 2.0f), glass,
                          static_cast<int>(FracturePattern::Radial), 0.5f, 0, glm::vec3(0.0f, -20.0f, 0.0f));
        break;
    }
    case Scene::Brick: {
        // 2:1:1 brick, breaking into irregular Voronoi chunks that
        // first split into a few clusters, then crumble further on
        // harder hits (kke::FracturePattern::Voronoi, 2 levels). 8x4x4
        // cells, 768 tets. It used to crack along every tet face.
        Material stone;
        stone.density = 2500.0f;
        stone.stiffness = 3.0e7f;
        stone.poissonsRatio = 0.25f;
        stone.fractureStressThreshold = 4000.0f; // same real, measured stone value as MaterialGridModule's own preset -- see that preset's own comment for the full empirical account
        stone.plasticYieldThreshold = 3000.0f;
        stone.plasticCreep = 0.1f;
        stone.metallic = 0.0f;
        stone.roughness = 0.9f;
        stone.textureId = 1; // matches PhysicsModule's own stone texture

        float jitterX = static_cast<float>((m_nextHandle * 37) % 200) / 100.0f - 1.0f;
        float jitterZ = static_cast<float>((m_nextHandle * 53) % 200) / 100.0f - 1.0f;
        stone.fractureStressThreshold = 2.5e5f; // see the glass scene's comment
        spawnPatternedBox({ 8, 4, 4 }, { 1.0f, 0.5f, 0.5f }, glm::vec3(jitterX * 2.0f, m_nextSpawnHeight + 3.0f, jitterZ * 2.0f), stone,
                          static_cast<int>(FracturePattern::Voronoi), 0.25f, 3, glm::vec3(0.0f, -20.0f, 0.0f));
        break;
    }
    case Scene::BreakTest: {
        // Three breakables resting on the ground, armed once settled
        // (thresholds relative to their resting stress), then an iron
        // ball dropped on each: glass (radial star centred where its ball
        // lands), a stone slab (Voronoi chunks in clusters) and a wooden
        // plank (splinters along its length). Thresholds from
        // tools/physics_lab "shoot". Seeds: the Fracture seed above.
        Material glass;
        glass.density = 2500.0f; glass.stiffness = 7.0e7f; glass.poissonsRatio = 0.22f;
        glass.fractureStressThreshold = 1.0e5f; glass.roughness = 0.05f; glass.textureId = 4;
        Material stone;
        stone.density = 2500.0f; stone.stiffness = 3.0e7f; stone.poissonsRatio = 0.25f;
        stone.fractureStressThreshold = 1.0e5f; stone.roughness = 0.9f; stone.textureId = 1;
        Material wood;
        wood.density = 600.0f; wood.stiffness = 1.0e7f; wood.poissonsRatio = 0.3f;
        wood.fractureStressThreshold = 1.5e5f; wood.roughness = 0.75f; wood.textureId = 0;
        // A clean stage: anything already lying there (the demo's
        // starting tetrahedra) would rest on or knock the pieces.
        {
            std::vector<ObjectHandle> handles;
            for (auto& [h, o] : m_objects) handles.push_back(h);
            for (ObjectHandle h : handles) removeObject(h);
            m_breakables.clear();
        }
        // Supports: plain, stiff, unbreakable stone blocks.
        Material support = stone;
        support.fractureStressThreshold = 1.0e12f;
        auto block = [&](glm::vec3 size, glm::vec3 at) {
            spawnTetMeshInternal(buildGridBox(2, 2, 2, size.x, size.y, size.z), at, support, false);
        };
        // Glass pane across two blocks; its star centres under its ball.
        const glm::vec3 glassAt(-2.4f, 0.525f, 0.0f), glassHit(0.1f, 0.0f, -0.1f);
        block({ 0.3f, 0.5f, 1.2f }, { -3.3f, 0.25f, 0.0f });
        block({ 0.3f, 0.5f, 1.2f }, { -1.5f, 0.25f, 0.0f });
        spawnPatternedBox({ 12, 1, 8 }, { 2.1f, 0.05f, 1.2f }, glassAt, glass, static_cast<int>(FracturePattern::Radial), 0.45f, 0,
                          glm::vec3(0.0f), 3.0f, &glassHit);
        // Wooden plank bridging two blocks: snaps into splinters.
        block({ 0.3f, 0.5f, 0.6f }, { -1.05f, 0.25f, 2.2f });
        block({ 0.3f, 0.5f, 0.6f }, { 1.05f, 0.25f, 2.2f });
        spawnPatternedBox({ 16, 1, 2 }, { 2.4f, 0.12f, 0.3f }, { 0.0f, 0.561f, 2.2f }, wood, static_cast<int>(FracturePattern::Splinters), 0.35f, 0,
                          glm::vec3(0.0f), 3.0f);
        // Stone wall standing on its own, hit from the side.
        spawnPatternedBox({ 8, 6, 2 }, { 1.4f, 1.0f, 0.25f }, { 2.4f, 0.501f, 0.0f }, stone, static_cast<int>(FracturePattern::Voronoi), 0.3f, 3,
                          glm::vec3(0.0f), 3.0f);
        m_breakTestTargets = { glassAt + glassHit, glm::vec3(0.0f, 0.62f, 2.2f) };
        m_breakTestWallTarget = glm::vec3(2.3f, 0.55f, 0.0f);
        m_breakTestTicks = 240; // after they've settled and armed (at most 3 s)
        break;
    }
    case Scene::RubberBall: {
        // A real ball now: buildSphere() pushes a cell grid out onto a
        // sphere (BUG-039). Soft, low-stiffness rubber that deforms
        // elastically and recovers, bouncing rather than fracturing.
        TetMeshData ball = buildSphere(4, 0.35f);
        Material rubber;
        rubber.density = 1200.0f;
        rubber.stiffness = 1.0e5f;
        rubber.poissonsRatio = 0.45f;
        // Deliberately far higher than any real impact in this demo
        // should reach -- the point of this scene is bouncing, not
        // fracturing, so this is set to a value real testing (see
        // README "Real fracture support") confirmed doesn't trigger
        // under this same drop.
        rubber.fractureStressThreshold = 1000000.0f; // matches MaterialGridModule's own retuned rubber preset -- see that preset's own comment for the full account of why this needed real margin above rubber's own measured stress range
        rubber.plasticYieldThreshold = 4000.0f;
        rubber.plasticCreep = 0.05f;
        rubber.metallic = 0.0f;
        rubber.roughness = 0.95f;
        rubber.textureId = 3; // matches PhysicsModule's own rubber texture

        float jitterX = static_cast<float>((m_nextHandle * 29) % 200) / 100.0f - 1.0f;
        float jitterZ = static_cast<float>((m_nextHandle * 83) % 200) / 100.0f - 1.0f;
        // spawnFracturableTetMesh(), not the simpler spawnTetMesh() --
        // a deliberate choice, not an oversight: spawnTetMesh() has no
        // initial-velocity parameter at all, and this scene genuinely
        // needs one for a real, convincing drop-and-bounce. Using the
        // fracturable path with a threshold real testing confirmed
        // this drop never reaches (see rubber.fractureStressThreshold
        // above) gets the velocity parameter this scene needs while
        // staying non-fracturing in actual practice — the ball bounces
        // under real elastic physics, it just happens to be spawned
        // through the path that *could* fracture it, without ever
        // actually doing so.
        spawnFracturableTetMesh(ball, glm::vec3(jitterX * 2.0f, m_nextSpawnHeight + 4.0f, jitterZ * 2.0f), rubber,
                                 glm::vec3(0.0f, -18.0f, 0.0f));
        break;
    }
    case Scene::CarCrash: {
        // Two real, distinct objects, not one -- a plastic "car" (real
        // permanent denting on impact, the same mechanic the "Spawn
        // plastic cube" button already demonstrates in isolation) and
        // a fracturable "wall" it's driven straight into (the same
        // real mechanic the fracture scenes above use), shown together
        // for the first time as an actual collision between two
        // different behaviors rather than two separate, unrelated
        // demo buttons.
        TetMeshData car = buildGridBox(6, 2, 3, 2.0f, 0.6f, 1.0f);
        Material carBody;
        carBody.density = 2700.0f; // aluminum-ish, not full structural steel -- real cars are mostly not solid iron
        carBody.stiffness = 7.0e7f;
        carBody.poissonsRatio = 0.33f;
        carBody.fractureStressThreshold = 40000.0f; // above this scene's own real stress range for a 7e7-stiffness object (see MaterialGridModule's own Glass preset comment for real measured numbers at this same stiffness) -- the car should crumple, not shatter
        carBody.plasticYieldThreshold = 3000.0f; // genuinely low relative to that same range, so it visibly, realistically dents well before ever approaching the fracture threshold above
        carBody.plasticCreep = 0.4f;
        carBody.metallic = 0.6f;
        carBody.roughness = 0.4f;
        carBody.textureId = 2; // reuses the iron/brushed-metal texture -- closest existing material to a painted metal car body

        TetMeshData wall = buildGridBox(2, 6, 8, 0.3f, 2.0f, 3.0f);
        Material wallMaterial;
        wallMaterial.density = 2500.0f;
        wallMaterial.stiffness = 3.0e7f;
        wallMaterial.poissonsRatio = 0.25f;
        wallMaterial.fractureStressThreshold = 4000.0f; // same real, measured stone value as the standalone brick scene and MaterialGridModule's own preset
        wallMaterial.plasticYieldThreshold = 3000.0f;
        wallMaterial.plasticCreep = 0.1f;
        wallMaterial.metallic = 0.0f;
        wallMaterial.roughness = 0.9f;
        wallMaterial.textureId = 1;

        // The wall sits still, offset along +X from the car's own spawn
        // point; the car is given a real horizontal initial velocity
        // toward it, not just gravity from a drop, matching this
        // project's own established pattern (see the fracture cube's
        // own comment on why a hard, fast impact is what triggers
        // fracture/plasticity in this engine's actual stress
        // magnitudes, not a gentle fall).
        float baseZ = static_cast<float>((m_nextHandle * 67) % 200) / 100.0f - 1.0f;
        // Both start resting on the floor (y is each box's center: half
        // its height, plus a hair so they don't begin interpenetrating
        // the ground plane). They used to spawn ~6 m up and fall first,
        // which is not what a car crash looks like.
        glm::vec3 wallPos(4.0f, 1.0f + 0.01f, baseZ);
        glm::vec3 carPos(0.0f, 0.3f + 0.01f, baseZ);
        spawnFracturableTetMesh(wall, wallPos, wallMaterial, glm::vec3(0.0f));
        spawnPlasticTetMesh(car, carPos, carBody, glm::vec3(22.0f, 0.0f, 0.0f));
        break;
    }
    case Scene::LavaMelt: {
        // An honest, clearly-labeled approximation, not real melting
        // physics -- worth stating plainly rather than implying
        // otherwise: FEMFX has no phase-change or topology-loss
        // simulation at all, so there's no way to make a real solid
        // object actually liquefy and lose volume. What this scene
        // does instead uses only real, already-proven mechanics: a
        // target block with a real, genuinely low plastic yield
        // threshold (softened metal, not fresh iron), and several
        // real, heavy "lava chunk" objects dropped on top of it. Real
        // physics does the rest — the chunks' own sustained weight
        // keeps the block's internal stress above its yield threshold
        // long after the initial impact, so it keeps slowly,
        // permanently sagging and flattening under that ongoing load,
        // the same real plasticCreep-driven mechanic the "Spawn
        // plastic cube" button demonstrates in isolation, just sustained
        // by real, continued weight instead of a single impact. A
        // real, physically-grounded stand-in for "gets soft under heat
        // and collapses under its own load," not a literal simulation
        // of melting.
        TetMeshData targetBlock = buildGridBox(2, 2, 2, 1.0f, 1.0f, 1.0f);
        Material softenedMetal;
        softenedMetal.density = 7000.0f;
        softenedMetal.stiffness = 5.0e7f; // real iron stiffness, but real weakened
        softenedMetal.poissonsRatio = 0.3f;
        softenedMetal.fractureStressThreshold = 30000.0f; // above this scene's own real stress range for a 5e7-stiffness object (comparable to Stone/Glass's own measured ranges, see MaterialGridModule's own preset comment) -- should sag, not shatter
        softenedMetal.plasticYieldThreshold = 2000.0f; // genuinely low relative to that range -- softened, not fresh iron's own 100000 (see MaterialGridModule's own preset)
        softenedMetal.plasticCreep = 0.6f; // real, visibly fast accumulation once yielding, so the sag is clearly visible within a real, short demo timeframe
        softenedMetal.metallic = 0.7f;
        softenedMetal.roughness = 0.5f;
        softenedMetal.textureId = 2; // reuses the existing iron/brushed-metal texture

        float jitterX = static_cast<float>((m_nextHandle * 31) % 200) / 100.0f - 1.0f;
        float jitterZ = static_cast<float>((m_nextHandle * 79) % 200) / 100.0f - 1.0f;
        glm::vec3 blockPos(jitterX * 2.0f, m_nextSpawnHeight + 0.5f, jitterZ * 2.0f);
        spawnPlasticTetMesh(targetBlock, blockPos, softenedMetal, glm::vec3(0.0f));

        // Three real, heavy lava chunks, not one -- a single chunk's
        // own weight settles and stops adding new stress once it's at
        // rest; three, landing across a real few tenths of a second
        // instead of simultaneously, keep genuinely disturbing and
        // reloading the block underneath for longer, giving the
        // plastic creep more real time to accumulate before the whole
        // scene settles down.
        for (int i = 0; i < 3; ++i) {
            TetMeshData lavaChunk = buildGridBox(1, 1, 1, 0.35f, 0.35f, 0.35f);
            Material lava;
            lava.density = 3100.0f; // real molten-rock-range density, heavier than the solid stone preset
            lava.stiffness = 2.0e6f; // real, soft -- lava is not rigid, so it shouldn't bounce or ring like the metal it's crushing
            lava.poissonsRatio = 0.35f;
            lava.fractureStressThreshold = 5000.0f; // stays intact as a real heavy weight, not part of what this scene is demonstrating breaking
            lava.plasticYieldThreshold = 4000.0f;
            lava.plasticCreep = 0.02f;
            lava.metallic = 0.0f;
            lava.roughness = 0.3f;
            lava.textureId = 5; // the real lava texture generated in init()

            float chunkOffsetX = (static_cast<float>(i) - 1.0f) * 0.4f;
            glm::vec3 chunkPos = blockPos + glm::vec3(chunkOffsetX, 2.5f + static_cast<float>(i) * 0.6f, 0.0f);
            spawnFracturableTetMesh(lavaChunk, chunkPos, lava, glm::vec3(0.0f, -8.0f, 0.0f));
        }
        break;
    }
    }
}

void PhysicsModule::renderUi() {
    // Position/size chosen to fit the panel's actual content within a
    // default 1280x720 window without needing to scroll — re-checked
    // after adding the "Load mesh" section made the panel taller than
    // it used to be; the old y=500 start overflowed the bottom of the
    // window at that height, found by actually testing this in a
    // running session, not assumed to still fit. Width bumped from 300
    // to 420 for the same reason, found the same way: three spawn
    // buttons ("Spawn tetrahedron"/"Spawn fracturable cube"/"Spawn
    // plastic cube") on one row genuinely needed more room than 300px
    // gave them, confirmed by a real screenshot showing the third
    // button clipped off entirely rather than just wrapping.
    ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin("Physics");

    ImGui::Text("Objects: %zu / %u", m_objects.size(), kMaxObjects);
    {
        int budget = static_cast<int>(m_debrisBudget);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
        if (ImGui::SliderInt("Debris budget", &budget, 0, 500)) m_debrisBudget = static_cast<uint32_t>(budget);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Most broken pieces kept at once (0 = no limit). Past it the oldest\nsleeping piece is removed. Each awake piece costs ~0.2 ms per step\non one core. Removed so far: %u", m_debrisRemoved);
    }
    ImGui::Text("Physics step: %.2f ms avg, %.2f ms max (%.0f ticks/s)", m_lastStepMsAvg, m_lastStepMsMax, m_lastTicksPerSecond);
    ImGui::Text("Render prep: %.2f ms  |  %.0f fps", m_lastRenderPrepMsAvg, m_lastFramesPerSecond);
    ImGui::Text("Pieces: %u (%u awake)  |  faces drawn: %u", m_totalPieces, m_awakePieces, m_renderedFaces);
    if (!m_ragdolls.empty()) {
        ImGui::Text("Ragdolls: %zu", m_ragdolls.size());
        ImGui::SameLine();
        ImGui::Checkbox("show bodies", &m_showRagdollBodies);
    }
    if (m_app->fixedStepsLastFrame() >= m_app->maxFixedStepsPerFrame() && m_lastTicksPerSecond < 55.0f) {
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Simulation can't keep up -- running in slow motion");
    }
    ImGui::TextWrapped(
        "This is a live example of PhysicsModule's spawn API, not a "
        "special-cased demo button -- see PhysicsModule::spawnTetrahedron() "
        "for the exact call this makes.");

    if (ImGui::Button("Spawn tetrahedron")) {
        // A little horizontal jitter so consecutive spawns don't land
        // in the exact same spot — genuinely just for a legible demo,
        // not a real placement system.
        float jitterX = static_cast<float>((m_nextHandle * 37) % 200) / 100.0f - 1.0f; // -1..1
        float jitterZ = static_cast<float>((m_nextHandle * 53) % 200) / 100.0f - 1.0f;

        // m_selectedMaterial, not a hardcoded value — real, settable
        // state (see selectedMaterial()'s own doc comment in
        // PhysicsModule.h). Defaults to a reasonable wood-like
        // material, but any UI (see kke::MaterialGridModule) can
        // change what actually gets spawned here.
        spawnTetrahedron(glm::vec3(jitterX * 3.0f, m_nextSpawnHeight, jitterZ * 3.0f), m_selectedMaterial);
    }

    ImGui::SameLine();
    if (ImGui::Button("Spawn fracturable cube")) spawnScene(Scene::FracturableCube);

    ImGui::SameLine();
    if (ImGui::Button("Spawn plastic cube")) spawnScene(Scene::PlasticCube);

    ImGui::Separator();
    ImGui::TextUnformatted("Scenes -- real fracture/plasticity, real materials, purpose-built shapes");
    // Each scene below is a genuinely distinct shape (via
    // buildGridBox()'s own general cellsX/Y/Z + sizeX/Y/Z parameters,
    // not the same cube reused with a different material) and a
    // hand-picked, scene-appropriate material -- not
    // m_selectedMaterial, deliberately: a "Glass Sheet" scene should
    // always demonstrate glass, regardless of whatever happens to be
    // selected in the Material Grid above, the same way a real,
    // purpose-built game level wouldn't let a settings panel silently
    // change what material its own set-piece is made of.
    {
        // World fracture seed: each object mixes in its own id, so every
        // brick breaks differently, but the same seed replays the same
        // breaks (kke::fractureSeed).
        int seed = static_cast<int>(m_fractureWorldSeed);
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
        if (ImGui::InputInt("Fracture seed", &seed)) m_fractureWorldSeed = static_cast<uint32_t>(std::max(seed, 0));
        ImGui::SameLine();
        if (ImGui::Button("New")) m_fractureWorldSeed = static_cast<uint32_t>(SDL_GetPerformanceCounter() & 0x7fffffff);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Brick: Voronoi chunks in 2 levels (big pieces first, then smaller).\nGlass: radial star around a point near the middle.");
    }
    if (ImGui::Button("Scene: Break test")) spawnScene(Scene::BreakTest);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Glass, stone and wood resting on the ground, then an iron ball dropped on each.");
    if (ImGui::Button("Scene: Glass Sheet")) spawnScene(Scene::GlassSheet);
    ImGui::SameLine();
    if (ImGui::Button("Scene: Brick")) spawnScene(Scene::Brick);
    if (ImGui::Button("Scene: Rubber Ball")) spawnScene(Scene::RubberBall);
    ImGui::SameLine();
    if (ImGui::Button("Scene: Car Crash")) spawnScene(Scene::CarCrash);
    // The old "Lava Melt" scene (a plastic block with lava cubes dropped on
    // it) is kept for the scripted benchmark only; real pouring and melting
    // lives in games/melt_demo (kke::ParticleFluid + kke::MeltVolume).
    ImGui::TextDisabled("Lava & melting: run melt_demo");

    if (ImGui::Button("Clear all")) {
        // Copy the keys first — removeObject() erases from m_objects,
        // so iterating that map directly while erasing from it would
        // be exactly the kind of iterator invalidation bug this
        // codebase has otherwise been careful to avoid all session.
        std::vector<ObjectHandle> handles;
        handles.reserve(m_objects.size());
        for (auto& [handle, obj] : m_objects) {
            handles.push_back(handle);
        }
        for (ObjectHandle handle : handles) {
            removeObject(handle);
        }
        m_breakables.clear();
    }

    ImGui::End();
}

void PhysicsModule::shutdown() {
    // The material texture library — real GPU resources (see init()'s
    // own comment on what this owns). m_materialTextures itself (a
    // vector of unique_ptr<Texture>) cleans up each texture's own
    // image/view/sampler automatically once cleared; the pool that
    // allocated their descriptor sets needs an explicit destroy call,
    // same as every other bare Vulkan handle in this codebase.
    m_materialTextures.clear();
    if (m_materialTexturePool) {
        vkDestroyDescriptorPool(m_vkDevice, m_materialTexturePool, nullptr);
        m_materialTexturePool = VK_NULL_HANDLE;
    }

    // Copy handles first — same reasoning as "Clear all" above.
    std::vector<ObjectHandle> handles;
    handles.reserve(m_objects.size());
    for (auto& [handle, obj] : m_objects) {
        handles.push_back(handle);
    }
    for (ObjectHandle handle : handles) {
        removeObject(handle);
    }
    m_breakables.clear();

    std::vector<RagdollHandle> ragdolls;
    for (auto& [h, rd] : m_ragdolls) ragdolls.push_back(h);
    for (RagdollHandle h : ragdolls) destroyRagdoll(h);

    if (m_scene) {
        AMD::FmDestroyScene(m_scene);
        m_scene = nullptr;
        log::get(name())->info("FEMFX scene destroyed");
    }

    // Destroyed last, after the scene — FmDestroyScene may itself
    // submit/wait on tasks during cleanup, so the pool needs to still
    // be alive for that. g_pool cleared first so nothing can observe
    // a dangling pointer between these two lines.
    g_pool = nullptr;
    g_poolOwner.reset();
}

} // namespace kke

#endif // KKE_ENABLE_FEMFX
