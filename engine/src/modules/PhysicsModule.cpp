#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Log.h"
#include "kke/Application.h"
#include "kke/VulkanCheck.h"

#include <imgui.h>
#include <AMD_FEMFX.h>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

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
    return std::aligned_alloc(alignment, roundedSize);
}

void FmAlignedFree(void* ptr) {
    std::free(ptr);
}

namespace kke {

namespace {

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
    int numWorkers = (hwThreads == 0) ? 1 : static_cast<int>(hwThreads);
    g_poolOwner = std::make_unique<ThreadPool>(numWorkers);
    g_pool = g_poolOwner.get();
    log::get(name())->info("Task system: {} worker(s) (hardware_concurrency={})", numWorkers, hwThreads);

    // Sized for kMaxObjects (see PhysicsModule.h) plus the one ground
    // rigid body — generous-but-modest numbers, not derived from first
    // principles, verified empirically by actually spawning up to the
    // cap via renderUi()'s button rather than assumed correct.
    AMD::FmSceneSetupParams sceneParams;
    sceneParams.maxTetMeshBuffers = kMaxObjects;
    sceneParams.maxTetMeshes = kMaxObjects;
    sceneParams.maxRigidBodies = 1;
    // A real bug, found from a real user report ("spawn a lot of
    // pieces and they fall through the ground"), not caught by this
    // project's own earlier testing (which never spawned anywhere
    // near kMaxObjects at once): these were left hardcoded at 64 when
    // kMaxObjects itself was raised from 8 to 64 earlier this project
    // — meaning every one of these pre-allocated capacity limits was
    // sized for the *old*, much smaller object cap. With genuinely 64
    // objects on screen, each touching the ground plus potentially
    // several piled-up neighbors, the real number of simultaneous
    // contacts can easily exceed a fixed 64 — and contacts FEMFX has
    // no room to record are contacts that don't get a real collision
    // response, which reads exactly like "falls through the floor."
    // Scaled to a real, generous per-object multiplier instead of
    // another fixed number that would just as quietly run out again
    // the next time kMaxObjects changes.
    sceneParams.maxDistanceContacts = kMaxObjects * 8;
    sceneParams.maxVolumeContacts = kMaxObjects * 8;
    sceneParams.maxVolumeContactVerts = kMaxObjects * 8;
    sceneParams.maxDeformationConstraints = kMaxObjects * 8;
    sceneParams.maxGlueConstraints = 0;
    sceneParams.maxPlaneConstraints = 0;
    sceneParams.maxRigidBodyAngleConstraints = 0;
    sceneParams.maxBroadPhasePairs = kMaxObjects * 16;
    sceneParams.maxRigidBodyBroadPhasePairs = kMaxObjects * 8;
    sceneParams.maxSceneVerts = kMaxObjects * 4 + 16;
    sceneParams.maxTetMeshBufferFeatures = 128;
    sceneParams.maxConstraintSolverDataSize = 1 << 24;
    sceneParams.numWorkerThreads = numWorkers;
    sceneParams.rigidBodiesExternal = false;

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

    // --- A wide, flat, kinematic (static) ground plane, top surface at
    // y=0.
    AMD::FmRigidBodySetupParams groundParams;
    groundParams.halfDimX = 50.0f;
    groundParams.halfDimY = 0.5f;
    groundParams.halfDimZ = 50.0f;
    groundParams.mass = 1.0f; // irrelevant for a kinematic body, but must be nonzero for the inertia calc below
    groundParams.isKinematic = true;
    groundParams.collisionGroup = 0;
    groundParams.state.pos = AMD::FmInitVector3(0.0f, -0.5f, 0.0f);
    groundParams.bodyInertiaTensor = AMD::FmComputeBodyInertiaTensorForBox(50.0f, 0.5f, 50.0f, 1.0f);

    m_ground = AMD::FmCreateRigidBody(groundParams);
    if (!m_ground) {
        throw std::runtime_error("FmCreateRigidBody returned null");
    }
    AMD::FmAddRigidBodyToScene(m_scene, m_ground);

    // --- Shared render resources (see PhysicsModule.h for why these
    // are shared across every spawned object, not per-object). Reuses
    // the existing cube shaders directly — they just transform a
    // position by an MVP matrix and output a flat color, which is
    // exactly what a physics-driven mesh needs too, no new shaders
    // required.
    {
        PipelineConfig config;
        // frontFace flipped from this project's own default (Clockwise)
        // -- a real fix attempt, not the "just disable culling"
        // workaround CubeModule/DestructionModule use elsewhere. Real
        // backface culling matters for performance (every triangle
        // rendered twice is real, measurable GPU cost this project can't
        // afford right now -- see README/BUGS.md for the ongoing
        // performance investigation), so this tests whether the actual
        // winding mismatch can be fixed by flipping which winding
        // Vulkan treats as "front" for this pipeline specifically,
        // instead of disabling the optimization entirely.
        config.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        config.cullMode = VK_CULL_MODE_NONE; // TEMPORARY DIAGNOSTIC -- isolating whether this is a culling issue at all
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

PhysicsModule::ObjectHandle PhysicsModule::spawnTetMeshInternal(const TetMeshData& mesh, const glm::vec3& position, const Material& material, bool enableFracture,
                                                                  const glm::vec3& initialVelocity, bool enablePlasticity) {
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
    const uint numVerts = static_cast<uint>(mesh.vertices.size());
    const uint numTets = static_cast<uint>(mesh.tets.size());
    obj->numVerts = numVerts;
    obj->numTets = numTets;

    obj->restPositions.resize(numVerts);
    for (uint i = 0; i < numVerts; ++i) {
        const glm::vec3& v = mesh.vertices[i];
        obj->restPositions[i] = AMD::FmInitVector3(position.x + v.x, position.y + v.y, position.z + v.z);
    }

    obj->tetVertIds.resize(numTets);
    for (uint i = 0; i < numTets; ++i) {
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
    for (uint t = 0; t < numTets; ++t) {
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

    AMD::FmTetMeshBufferBounds bounds;
    AMD::FmComputeTetMeshBufferBounds(
        &bounds,
        enableFracture ? obj->fractureGroupCounts.data() : nullptr,
        enableFracture ? obj->tetFractureGroupIds.data() : nullptr,
        obj->vertIncidentTets.data(), obj->tetVertIds.data(),
        nullptr, numVerts, numTets, enableFracture);

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

    // kke::Material -> FmTetMaterialParams, the actual bridge.
    AMD::FmTetMaterialParams femfxMaterial;
    femfxMaterial.restDensity = material.density;
    femfxMaterial.youngsModulus = material.stiffness;
    femfxMaterial.poissonsRatio = material.poissonsRatio;
    femfxMaterial.plasticYieldThreshold = material.plasticYieldThreshold;
    femfxMaterial.plasticCreep = material.plasticCreep;
    femfxMaterial.fractureStressThreshold = material.fractureStressThreshold;

    AMD::FmInitTetState(obj->tetMesh, obj->tetVertIds.data(), femfxMaterial);
    AMD::FmComputeMeshConstantMatrices(obj->tetMesh);

    // A FRESH set of arrays for FmInitConnectivity, not the same
    // vertIncidentTets already passed to FmComputeTetMeshBufferBounds
    // above — that call takes a non-const pointer and, verified by
    // testing, appears to consume/mutate them internally: reusing the
    // same arrays crashed immediately in standalone testing, building
    // a fresh set fixed it. See PhysicsModule.h's class comment for
    // the real, gdb-traced crash this call fixes.
    std::vector<AMD::FmArray<uint>> connectivityVertIncidentTets(numVerts);
    for (uint t = 0; t < numTets; ++t) {
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

    AMD::FmEnableSelfCollision(obj->tetMesh, false);
    AMD::FmEnableSleeping(m_scene, obj->tetMesh, false);

    obj->sceneBufferId = AMD::FmAddTetMeshBufferToScene(m_scene, obj->tetMeshBuffer);
    obj->material = material;

    if (enableFracture) {
        // Sized for flat shading's own real vertex-count needs (see
        // the non-fracturable branch's comment for the full account of
        // why this changed at all) -- 12 unique vertices per tet (4
        // faces x 3 corners each, none shared with any other face),
        // not FEMFX's own maxVerts bound, which reserves capacity for
        // *simulated* (shared) vertices, a different, smaller number
        // than what flat-shaded rendering actually needs to draw.
        obj->vertexBuffer = std::make_unique<Buffer>(
            m_app->device(), sizeof(Vertex) * obj->maxTets * 12, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        obj->indexBuffer = std::make_unique<Buffer>(
            m_app->device(), sizeof(uint32_t) * obj->maxTets * 12, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    } else {
        // Flat shading, not smooth -- a real, visible bug found and
        // fixed, not a style preference: the original version here
        // shared vertices between adjacent faces and averaged their
        // normals together (the same technique CubeModule's own
        // render() comment already documents as "correct and
        // meaningful for genuine exterior-facing geometry" -- true for
        // a mesh with enough faces to read as curved, false for a
        // single tetrahedron's 4 faces, or even a multi-tet object's
        // handful of exterior faces). Averaging normals across a
        // shape this low-poly blends adjacent faces' shading into each
        // other, producing a smooth, iridescent-looking gradient
        // instead of the sharp, distinct flat faces a solid object
        // should show -- confirmed directly, not assumed: a temporary
        // diagnostic shader outputting the raw world-space normal as
        // color showed a continuous color gradient sweeping across
        // what should have been 2-3 separate, uniformly-colored faces.
        // Real flat shading needs each face's 3 corners to be genuinely
        // separate vertices with that face's own single normal, not
        // shared with any neighboring face -- hence numTets*12 unique
        // vertices here, not numVerts shared ones.
        obj->vertexBuffer = std::make_unique<Buffer>(
            m_app->device(), sizeof(Vertex) * numTets * 12, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

        // A trivial sequential index buffer (0,1,2,...) -- with every
        // vertex now unique to its own face (see above), there's no
        // sharing left for an index buffer to meaningfully express.
        // Kept anyway, rather than switching to non-indexed vkCmdDraw,
        // specifically to avoid touching the draw-call code path at
        // all -- a smaller, safer change than restructuring how these
        // objects get drawn.
        std::vector<uint32_t> indices(numTets * 12);
        for (uint32_t i = 0; i < numTets * 12; ++i) indices[i] = i;
        obj->indexBuffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(
            m_app->device(), indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    }

    ObjectHandle handle = m_nextHandle++;
    m_objects[handle] = std::move(obj);
    return handle;
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
    auto it = m_objects.find(handle);
    if (it == m_objects.end()) {
        return; // no-op, not an error — see the header's own doc comment
    }
    AMD::FmRemoveTetMeshBufferFromScene(m_scene, it->second->sceneBufferId);
    AMD::FmDestroyTetMeshBuffer(it->second->tetMeshBuffer);
    m_objects.erase(it);
}

void PhysicsModule::fixedUpdate(const FixedUpdateContext& ctx) {
    AMD::FmUpdateScene(m_scene, ctx.fixedDt);

    // Logged every ~1s (at 60Hz), not every tick — see spdlog's own
    // async design: this is exactly the "don't spend even the queue-push
    // cost 60 times a second for something a human glances at
    // occasionally" case.
    if (!m_objects.empty() && ctx.tickIndex % 60 == 0) {
        const auto& first = m_objects.begin()->second;
        AMD::FmVector3 pos = AMD::FmGetVertPosition(*first->tetMesh, 0);
        log::get(name())->info("{} object(s); first object's vert0 height: {:.4f}", m_objects.size(), pos.y);

        // Real, direct confirmation of whether fracture has actually
        // happened, not inferred from screenshots — logged for every
        // fracturable object specifically, alongside the existing
        // height log above.
        for (auto& [handle, obj] : m_objects) {
            if (obj->fracturable) {
                uint numPieces = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
                if (numPieces > 1) {
                    log::get(name())->info("fracturable object (handle {}) has split into {} pieces", handle, numPieces);
                }
            }
            // Same reasoning as the fracture check just above — real,
            // numeric confirmation of permanent deformation, not
            // inferred from a screenshot. FmGetVertRestPosition()
            // returns the shape FEMFX currently treats as this
            // object's own "rest" (unstressed) state; for a purely
            // elastic object this never changes after spawning, but
            // real plasticity permanently moves it. Comparing against
            // obj->restPositions (this object's own original spawn-time
            // values, still held for its whole lifetime — see
            // SpawnedTet's own comment on why) directly measures how
            // far any single vertex has permanently drifted.
            // Real, indirect but genuinely diagnostic confirmation of
            // permanent deformation -- FmGetVertRestPosition() turned
            // out to be the wrong signal (confirmed by reading FEMFX's
            // own source: plasticity is tracked as a per-TET
            // plasticDeformationMatrix, not a change to the vertex
            // rest-position array at all, and that internal state has
            // no public accessor in AMD_FEMFX.h to read directly).
            // What's directly observable instead: vertex 0 and vertex 1
            // of this object's own cube shape started exactly 1.0 unit
            // apart (see the "Spawn plastic cube" button's own
            // vertices). For a purely elastic object fully at rest
            // (settled, no external forces), that distance returns
            // very close to 1.0 again once it stops moving. If it's
            // meaningfully different once genuinely at rest, that's
            // real, permanent shape change -- plasticity working, not
            // just elastic springback still in progress.
            if (obj->plastic && obj->numVerts >= 2) {
                AMD::FmVector3 p0 = AMD::FmGetVertPosition(*obj->tetMesh, 0);
                AMD::FmVector3 p1 = AMD::FmGetVertPosition(*obj->tetMesh, 1);
                float dx = p1.x - p0.x, dy = p1.y - p0.y, dz = p1.z - p0.z;
                float currentEdgeLength = std::sqrt(dx * dx + dy * dy + dz * dz);
                log::get(name())->info("plastic object (handle {}) vert0-vert1 distance: {:.4f} (started at 1.0000)", handle, currentEdgeLength);
            }
        }
    }
}

void PhysicsModule::renderShadow(const ShadowRenderContext& ctx) {
    // Reuses each object's existing vertex/index buffers as-is,
    // whatever they currently contain -- deliberately not
    // regenerating them here. render() (which runs after this in the
    // same frame, see Application's own frame loop) is what uploads
    // each object's current simulated positions; this pass runs
    // first, so it draws whatever was uploaded last frame. In
    // practice this means a spawned object's shadow lags its own
    // visible position by at most one frame -- at 60fps, roughly
    // 16ms, not visually meaningful for anything in this demo — and a
    // brand new object simply casts no shadow for its first frame,
    // before render() has populated its buffers at all. A real,
    // deliberate simplicity trade-off, not an oversight: correctly
    // synchronizing this would mean duplicating render()'s own
    // simulated-position readback here too, real complexity this
    // demo's own motion speeds don't need.
    m_shadowPipeline->bind(ctx.cmd);
    for (auto& [handle, obj] : m_objects) {
        if (!obj->vertexBuffer || !obj->indexBuffer || obj->numTets == 0) continue;
        ShadowPushConstants pc{ ctx.lightViewProj, glm::mat4(1.0f) };
        vkCmdPushConstants(ctx.cmd, m_shadowPipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkBuffer buffers[] = { obj->vertexBuffer->handle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(ctx.cmd, obj->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(ctx.cmd, obj->numTets * 12, 1, 0, 0, 0);
    }
}

void PhysicsModule::render(const RenderContext& ctx) {
    if (!m_pipeline) {
        return;
    }

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
    auto bindMaterialTexture = [&](const Material& material) {
        VkDescriptorSet textureSet = ctx.defaultMaterialTextureDescriptorSet;
        if (material.textureId >= 0 && material.textureId < static_cast<int>(m_materialTextures.size())) {
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

    // Every spawned object — each one's simulated vertex positions are
    // already in world space, so the model matrix is identity for all
    // of them; see the class comment for why this is the simplest
    // possible render bridge, not a real skinning one. metallic/
    // roughness, unlike model, genuinely differ per-object now (see
    // Material.h) -- pushed inside the loop below, once per object,
    // rather than once here outside it.

    size_t colorIndex = 0;
    std::vector<Vertex> verts; // reused across objects, resized per-object below
    // normalSum removed -- flat shading no longer accumulates/averages
    // per-vertex normals across faces (see spawnTetMeshInternal()'s
    // own comment for the full account of why smooth normals were the
    // real bug behind a "hollow"-looking tetrahedron).
    std::vector<uint32_t> dynamicIndices; // reused too — fracturable objects only, rebuilt every sub-mesh
    for (auto& [handle, obj] : m_objects) {
        glm::vec3 color = kColorPalette[colorIndex % (sizeof(kColorPalette) / sizeof(kColorPalette[0]))];
        ++colorIndex;

        if (!obj->fracturable) {
            // Flat shading now, not smooth — see spawnTetMeshInternal()'s
            // own comment for the full, diagnostic-confirmed account of
            // why. numTets*12 unique vertices (12 per tet: 4 faces x 3
            // corners, none shared with any other face), each getting
            // its own face's single normal — not obj->numVerts shared
            // ones averaged across adjacent faces.
            verts.resize(obj->numTets * 12);

            // Positions still come from FEMFX's own simulated (shared)
            // vertex array — read once per unique global vertex id
            // here, then looked up per-face-corner below, rather than
            // querying FmGetVertPosition() up to 12x redundantly for
            // vertices that are geometrically the same simulated point.
            static thread_local std::vector<glm::vec3> simPositions;
            simPositions.resize(obj->numVerts);
            for (uint32_t i = 0; i < obj->numVerts; ++i) {
                AMD::FmVector3 p = AMD::FmGetVertPosition(*obj->tetMesh, i);
                simPositions[i] = glm::vec3(p.x, p.y, p.z) * m_renderScale;
            }

            uint32_t outIdx = 0;
            for (uint32_t t = 0; t < obj->numTets; ++t) {
                const uint32_t* ids = obj->tetVertIds[t].ids;
                const uint32_t faces[4][3] = {
                    {ids[3], ids[1], ids[2]},
                    {ids[2], ids[0], ids[3]},
                    {ids[1], ids[3], ids[0]},
                    {ids[0], ids[2], ids[1]},
                };
                for (const auto& f : faces) {
                    glm::vec3 a = simPositions[f[0]];
                    glm::vec3 b = simPositions[f[1]];
                    glm::vec3 c = simPositions[f[2]];
                    glm::vec3 faceNormal = glm::normalize(glm::cross(b - a, c - a));
                    // TEMPORARY DIAGNOSTIC -- log every face's real
                    // vertex positions for the very first tet drawn,
                    // to check whether all 4 faces genuinely get
                    // distinct, non-degenerate positions.
                    static bool loggedOnce = false;
                    if (!loggedOnce && t == 0) {
                        log::get(name())->info("face a=({:.3f},{:.3f},{:.3f}) b=({:.3f},{:.3f},{:.3f}) c=({:.3f},{:.3f},{:.3f}) normal=({:.3f},{:.3f},{:.3f})",
                            a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z, faceNormal.x, faceNormal.y, faceNormal.z);
                    }
                    // Real per-face UVs now, not the Vertex struct's own
                    // (0,0) default -- a standard, simple per-triangle
                    // mapping (each face gets its own full copy of the
                    // texture, not a seamless continuation from its
                    // neighbors), made possible by this same flat-
                    // shading rewrite already giving every face its own
                    // unique, unshared vertices. Real per-material
                    // *patterns* now, not just flat tints -- see
                    // README's own account of this as a genuine
                    // continuation of the flat-shading fix, not a
                    // separate change.
                    verts[outIdx + 0] = { a, color, faceNormal, {0.0f, 0.0f} };
                    verts[outIdx + 1] = { b, color, faceNormal, {1.0f, 0.0f} };
                    verts[outIdx + 2] = { c, color, faceNormal, {0.0f, 1.0f} };
                    outIdx += 3;
                }
            }

            obj->vertexBuffer->upload(verts.data(), verts.size() * sizeof(Vertex));

            // TEMPORARY DIAGNOSTIC -- log the exact index count being
            // drawn and the vertex data size actually uploaded, to
            // rule out a spawn-time vs render-time numTets mismatch.
            static bool loggedDraw = false;
            if (!loggedDraw) {
                loggedDraw = true;
                log::get(name())->info("draw: obj->numTets={} verts.size()={} indexCount={} vertexBufferBytes={}",
                    obj->numTets, verts.size(), obj->numTets * 12, verts.size() * sizeof(Vertex));
            }

            bindMaterialTexture(obj->material);
            PhysicsPushConstants pc{ glm::mat4(1.0f), obj->material.metallic, obj->material.roughness };
            vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

            VkBuffer buffers[] = { obj->vertexBuffer->handle() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
            vkCmdBindIndexBuffer(ctx.cmd, obj->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.cmd, obj->numTets * 12, 1, 0, 0, 0);
            continue;
        }

        // Fracturable path — genuinely different, not just the same
        // logic with extra checks: a fractured object can be split
        // into multiple independently-moving FmTetMesh pieces at
        // runtime (FmGetNumTetMeshes() can grow past 1), and each
        // piece has its OWN vertex numbering (confirmed by testing —
        // see the README's "Real fracture support" section for the
        // full account), so vertex positions AND the index buffer's
        // actual content both get rebuilt from scratch every frame,
        // per piece, rather than uploaded once at spawn time. Real,
        // measured cost of this simplicity, not hidden: every
        // fracturable object costs a full CPU-side rebuild per frame
        // regardless of whether it has actually fractured yet.
        uint numSubMeshes = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        for (uint m = 0; m < numSubMeshes; ++m) {
            AMD::FmTetMesh* subMesh = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (!subMesh) continue;

            uint32_t subNumVerts = AMD::FmGetNumVerts(*subMesh);
            uint32_t subNumTets = AMD::FmGetNumTets(*subMesh);
            if (subNumVerts == 0 || subNumTets == 0) continue;
            if (subNumTets > obj->maxTets) {
                // A real safety check, not decorative -- updated to
                // compare against maxTets specifically now that flat
                // shading's own buffer capacity (see
                // spawnTetMeshInternal()) is sized from maxTets*12
                // unique per-face vertices, not FEMFX's own maxVerts
                // bound (a different, smaller number: shared,
                // simulated vertex count, not flat-shaded render
                // vertex count). If this ever fires, the reservation
                // was wrong for some fracture pattern this specific
                // mesh produced — better to skip drawing this piece
                // for a frame than write past the buffers below.
                log::get(name())->warn(
                    "fracturable object sub-mesh {} exceeds reserved capacity ({} tets vs max {}) -- skipping this frame",
                    m, subNumTets, obj->maxTets);
                continue;
            }

            // Flat shading now, not smooth — same fix, same reasoning,
            // as the non-fracturable path above. subNumTets*12 unique
            // vertices, each face getting its own single normal.
            verts.resize(subNumTets * 12);

            static thread_local std::vector<glm::vec3> subSimPositions;
            subSimPositions.resize(subNumVerts);
            for (uint32_t i = 0; i < subNumVerts; ++i) {
                AMD::FmVector3 p = AMD::FmGetVertPosition(*subMesh, i);
                subSimPositions[i] = glm::vec3(p.x, p.y, p.z) * m_renderScale;
            }

            dynamicIndices.clear();
            dynamicIndices.reserve(subNumTets * 12);
            uint32_t outIdx = 0;
            for (uint32_t t = 0; t < subNumTets; ++t) {
                AMD::FmTetVertIds ids = AMD::FmGetTetVertIds(*subMesh, t);
                const uint32_t faces[4][3] = {
                    {ids.ids[3], ids.ids[1], ids.ids[2]},
                    {ids.ids[2], ids.ids[0], ids.ids[3]},
                    {ids.ids[1], ids.ids[3], ids.ids[0]},
                    {ids.ids[0], ids.ids[2], ids.ids[1]},
                };
                for (const auto& f : faces) {
                    glm::vec3 a = subSimPositions[f[0]];
                    glm::vec3 b = subSimPositions[f[1]];
                    glm::vec3 c = subSimPositions[f[2]];
                    glm::vec3 faceNormal = glm::normalize(glm::cross(b - a, c - a));
                    // Same real per-face UV mapping as the
                    // non-fracturable path above -- see that one's own
                    // comment for the full reasoning.
                    verts[outIdx + 0] = { a, color, faceNormal, {0.0f, 0.0f} };
                    verts[outIdx + 1] = { b, color, faceNormal, {1.0f, 0.0f} };
                    verts[outIdx + 2] = { c, color, faceNormal, {0.0f, 1.0f} };
                    dynamicIndices.push_back(outIdx + 0);
                    dynamicIndices.push_back(outIdx + 1);
                    dynamicIndices.push_back(outIdx + 2);
                    outIdx += 3;
                }
            }

            obj->vertexBuffer->upload(verts.data(), verts.size() * sizeof(Vertex));
            obj->indexBuffer->upload(dynamicIndices.data(), dynamicIndices.size() * sizeof(uint32_t));

            bindMaterialTexture(obj->material);
            PhysicsPushConstants pc{ glm::mat4(1.0f), obj->material.metallic, obj->material.roughness };
            vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

            VkBuffer buffers[] = { obj->vertexBuffer->handle() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
            vkCmdBindIndexBuffer(ctx.cmd, obj->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(ctx.cmd, static_cast<uint32_t>(dynamicIndices.size()), 1, 0, 0, 0);
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
    if (ImGui::Button("Spawn fracturable cube")) {
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
    }

    ImGui::SameLine();
    if (ImGui::Button("Spawn plastic cube")) {
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
    }

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
    if (ImGui::Button("Scene: Glass Sheet")) {
        // Thin and wide, not cube-shaped -- a real pane of glass, not
        // a glass-colored cube. 6x2x6 cells (72 tets) gives real room
        // for it to shatter into many small, convincing shards rather
        // than a couple of big chunks, the same "not enough internal
        // boundaries" problem the original single-tet fracture demo
        // had.
        TetMeshData sheet = buildGridBox(6, 2, 6, 2.0f, 0.15f, 2.0f);
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
        spawnFracturableTetMesh(sheet, glm::vec3(jitterX * 2.0f, m_nextSpawnHeight + 3.0f, jitterZ * 2.0f), glass,
                                 glm::vec3(0.0f, -20.0f, 0.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("Scene: Brick")) {
        // Real 2:1:1 brick proportions, not a cube -- 4x2x2 cells (48
        // tets) for real fracture room without this being noticeably
        // more expensive than the existing fracturable cube.
        TetMeshData brick = buildGridBox(4, 2, 2, 1.0f, 0.5f, 0.5f);
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
        spawnFracturableTetMesh(brick, glm::vec3(jitterX * 2.0f, m_nextSpawnHeight + 3.0f, jitterZ * 2.0f), stone,
                                 glm::vec3(0.0f, -20.0f, 0.0f));
    }
    if (ImGui::Button("Scene: Rubber Ball")) {
        // An honest approximation, not a real sphere -- worth stating
        // plainly rather than implying otherwise: buildGridBox() only
        // produces box shapes, and a genuine tetrahedralized sphere
        // would need real mesh-import machinery (see README "Content
        // pipeline: CGAL tetrahedralization") this button doesn't use.
        // A small, roughly cube-shaped block of real rubber is still a
        // real, honest demonstration of the actual point of this scene
        // -- soft, low-stiffness material that deforms elastically and
        // recovers, bouncing under real physics, not fracturing --
        // it's just visually a rounded-corner-free block, not a ball.
        TetMeshData ball = buildGridBox(3, 3, 3, 0.6f, 0.6f, 0.6f);
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
    }
    ImGui::SameLine();
    if (ImGui::Button("Scene: Car Crash")) {
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
        glm::vec3 wallPos(4.0f, m_nextSpawnHeight + 1.0f, baseZ);
        glm::vec3 carPos(0.0f, m_nextSpawnHeight + 1.0f, baseZ);
        spawnFracturableTetMesh(wall, wallPos, wallMaterial, glm::vec3(0.0f));
        spawnPlasticTetMesh(car, carPos, carBody, glm::vec3(22.0f, 0.0f, 0.0f));
    }
    if (ImGui::Button("Scene: Lava Melt")) {
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
    }

    ImGui::SameLine();
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
    }

    ImGui::Separator();
    ImGui::TextWrapped(
        "Load a real tetrahedralized mesh from disk -- proves the "
        "general spawnTetMesh() path, not just the single hardcoded "
        "tetrahedron above. Path is temporarily hardcoded (no asset "
        "browser yet) -- see kke_tetrahedralizer and README 'Content "
        "pipeline: CGAL tetrahedralization'.");
    if (ImGui::Button("Load /tmp/test_output.ktet.json")) {
        try {
            TetMeshData mesh = loadTetMeshFromFile("/tmp/test_output.ktet.json");
            Material material;
            material.density = 700.0f;
            material.stiffness = 1.0e7f;
            material.poissonsRatio = 0.3f;
            material.plasticYieldThreshold = 0.0f;
            material.fractureStressThreshold = 1.0e8f;
            ObjectHandle handle = spawnTetMesh(mesh, glm::vec3(0.0f, m_nextSpawnHeight, 0.0f), material);
            if (handle != kInvalidHandle) {
                log::get(name())->info("Loaded and spawned mesh: {} verts, {} tets",
                                        mesh.vertices.size(), mesh.tets.size());
            }
        } catch (const std::exception& e) {
            log::get(name())->error("Load mesh failed: {}", e.what());
        }
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

    if (m_ground) {
        AMD::FmDestroyRigidBody(m_ground);
        m_ground = nullptr;
    }
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
