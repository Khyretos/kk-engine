#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Log.h"
#include "kke/Application.h"

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
struct PhysicsPushConstants { glm::mat4 mvp; glm::mat4 model; };

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
    sceneParams.maxDistanceContacts = 64;
    sceneParams.maxVolumeContacts = 64;
    sceneParams.maxVolumeContactVerts = 64;
    sceneParams.maxDeformationConstraints = 64;
    sceneParams.maxGlueConstraints = 0;
    sceneParams.maxPlaneConstraints = 0;
    sceneParams.maxRigidBodyAngleConstraints = 0;
    sceneParams.maxBroadPhasePairs = 64;
    sceneParams.maxRigidBodyBroadPhasePairs = 64;
    sceneParams.maxSceneVerts = kMaxObjects * 4 + 16;
    sceneParams.maxTetMeshBufferFeatures = 128;
    sceneParams.maxConstraintSolverDataSize = 1 << 22;
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
        config.pushConstantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PhysicsPushConstants) };
        config.descriptorSetLayouts = { app.lightingBuffer().descriptorSetLayout() };
        m_pipeline = std::make_unique<Pipeline>(
            app.device(), app.renderer().renderPass(),
            "shaders/cube.vert.spv", "shaders/cube.frag.spv", config);
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

    AMD::FmTetMeshBufferBounds bounds;
    AMD::FmComputeTetMeshBufferBounds(&bounds, nullptr, nullptr, obj->vertIncidentTets.data(), obj->tetVertIds.data(),
                                       nullptr, numVerts, numTets, /*enableFracture=*/false);

    AMD::FmTetMeshBufferSetupParams meshParams;
    meshParams.numVerts = bounds.numVerts;
    meshParams.numTets = bounds.numTets;
    meshParams.numVertIncidentTets = bounds.numVertIncidentTets;
    meshParams.maxVertAdjacentVerts = bounds.maxVertAdjacentVerts;
    meshParams.maxVerts = bounds.maxVerts;
    meshParams.maxExteriorFaces = bounds.maxExteriorFaces;
    meshParams.maxTetMeshes = bounds.maxTetMeshes;
    meshParams.collisionGroup = 0;
    meshParams.enablePlasticity = false;
    meshParams.enableFracture = false;
    meshParams.isKinematic = false;

    obj->tetMeshBuffer = AMD::FmCreateTetMeshBuffer(meshParams, nullptr, nullptr, &obj->tetMesh);
    if (!obj->tetMeshBuffer || !obj->tetMesh) {
        log::get(name())->error("spawnTetMesh: FmCreateTetMeshBuffer failed");
        return kInvalidHandle;
    }

    AMD::FmMatrix3 identity = AMD::FmInitMatrix3(
        AMD::FmInitVector3(1.0f, 0.0f, 0.0f),
        AMD::FmInitVector3(0.0f, 1.0f, 0.0f),
        AMD::FmInitVector3(0.0f, 0.0f, 1.0f));
    AMD::FmInitVertState(obj->tetMesh, obj->restPositions.data(), identity, AMD::FmInitVector3(0.0f), 1.0f, AMD::FmInitVector3(0.0f));

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
    obj->vertexBuffer = std::make_unique<Buffer>(
        m_app->device(), sizeof(Vertex) * numVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

    // Per-object index buffer now (different objects can have
    // different topology) — same face-winding convention already
    // verified for the single-tet case, generalized: for tet i with
    // global vertex ids (a,b,c,d), its 4 faces follow
    // FEMFXTetMeshConnectivity.h's documented "CCW from exterior:
    // 312, 203, 130, 021" using THIS tet's own vertex ids, not always
    // literally 0,1,2,3 like the old hardcoded single-tet version.
    std::vector<uint32_t> indices;
    indices.reserve(numTets * 12);
    for (uint t = 0; t < numTets; ++t) {
        const auto& ids = obj->tetVertIds[t].ids;
        indices.push_back(ids[3]); indices.push_back(ids[1]); indices.push_back(ids[2]);
        indices.push_back(ids[2]); indices.push_back(ids[0]); indices.push_back(ids[3]);
        indices.push_back(ids[1]); indices.push_back(ids[3]); indices.push_back(ids[0]);
        indices.push_back(ids[0]); indices.push_back(ids[2]); indices.push_back(ids[1]);
    }
    obj->indexBuffer = std::make_unique<Buffer>(Buffer::createDeviceLocal(
        m_app->device(), indices.data(), indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

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
    }
}

void PhysicsModule::render(const RenderContext& ctx) {
    if (!m_pipeline) {
        return;
    }

    m_pipeline->bind(ctx.cmd);
    vkCmdBindDescriptorSets(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline->layout(),
                             0, 1, &ctx.lightingDescriptorSet, 0, nullptr);

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
        PhysicsPushConstants pc{ ctx.proj * ctx.view * model, model };
        vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        m_groundMesh->bind(ctx.cmd);
        m_groundMesh->draw(ctx.cmd);
    }

    // Every spawned object — each one's simulated vertex positions are
    // already in world space, so the model matrix is identity; see the
    // class comment for why this is the simplest possible render
    // bridge, not a real skinning one.
    PhysicsPushConstants pc{ ctx.proj * ctx.view, glm::mat4(1.0f) }; // model = identity, shared by every object
    vkCmdPushConstants(ctx.cmd, m_pipeline->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

    size_t colorIndex = 0;
    std::vector<Vertex> verts; // reused across objects, resized per-object below
    std::vector<glm::vec3> normalSum; // reused too — accumulated per-vertex before normalizing
    for (auto& [handle, obj] : m_objects) {
        glm::vec3 color = kColorPalette[colorIndex % (sizeof(kColorPalette) / sizeof(kColorPalette[0]))];
        ++colorIndex;

        verts.resize(obj->numVerts);
        for (uint32_t i = 0; i < obj->numVerts; ++i) {
            AMD::FmVector3 p = AMD::FmGetVertPosition(*obj->tetMesh, i);
            verts[i].position = glm::vec3(p.x, p.y, p.z) * m_renderScale;
            verts[i].color = color;
        }

        // Real per-vertex normals, not left uninitialized — a genuine
        // gap from when this class only ever wrote position/color:
        // adding a `normal` field to the shared Vertex struct (for
        // this engine's first real lighting slice) would otherwise
        // have left every physics object lit by garbage memory. Each
        // vertex is shared by up to 3 of a tet's 4 faces (the same
        // winding convention already used for the index buffer, in
        // spawnTetMesh()); summing the un-normalized cross-product of
        // each adjacent face and normalizing once at the end weights
        // larger faces more, a standard, reasonable approach — not
        // full smooth-shading correctness across an entire multi-tet
        // mesh's interior, but correct and meaningful for genuine
        // exterior-facing geometry, which is all that's ever visible.
        normalSum.assign(obj->numVerts, glm::vec3(0.0f));
        for (uint32_t t = 0; t < obj->numTets; ++t) {
            const uint32_t* ids = obj->tetVertIds[t].ids;
            const uint32_t faces[4][3] = {
                {ids[3], ids[1], ids[2]},
                {ids[2], ids[0], ids[3]},
                {ids[1], ids[3], ids[0]},
                {ids[0], ids[2], ids[1]},
            };
            for (const auto& f : faces) {
                glm::vec3 a = verts[f[0]].position;
                glm::vec3 b = verts[f[1]].position;
                glm::vec3 c = verts[f[2]].position;
                glm::vec3 faceNormal = glm::cross(b - a, c - a);
                normalSum[f[0]] += faceNormal;
                normalSum[f[1]] += faceNormal;
                normalSum[f[2]] += faceNormal;
            }
        }
        for (uint32_t i = 0; i < obj->numVerts; ++i) {
            float len = glm::length(normalSum[i]);
            verts[i].normal = (len > 1e-8f) ? (normalSum[i] / len) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        obj->vertexBuffer->upload(verts.data(), verts.size() * sizeof(Vertex));

        VkBuffer buffers[] = { obj->vertexBuffer->handle() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(ctx.cmd, 0, 1, buffers, offsets);
        vkCmdBindIndexBuffer(ctx.cmd, obj->indexBuffer->handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(ctx.cmd, obj->numTets * 12, 1, 0, 0, 0);
    }
}

void PhysicsModule::renderUi() {
    // Position/size chosen to fit the panel's actual content within a
    // default 1280x720 window without needing to scroll — re-checked
    // after adding the "Load mesh" section made the panel taller than
    // it used to be; the old y=500 start overflowed the bottom of the
    // window at that height, found by actually testing this in a
    // running session, not assumed to still fit.
    ImGui::SetNextWindowPos(ImVec2(320, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_FirstUseEver);
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

        Material material;
        material.density = 700.0f;
        material.stiffness = 1.0e7f;
        material.poissonsRatio = 0.3f;
        material.plasticYieldThreshold = 0.0f;
        material.fractureStressThreshold = 1.0e8f;

        spawnTetrahedron(glm::vec3(jitterX * 3.0f, m_nextSpawnHeight, jitterZ * 3.0f), material);
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
