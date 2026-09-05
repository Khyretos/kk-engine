// kke_physics_benchmark -- verifies PhysicsModule's real thread-pool
// task system (see engine/src/modules/PhysicsModule.cpp) in isolation,
// and measures its actual effect on this specific machine.
//
// IMPORTANT, honest context this tool exists specifically to address:
// the sandbox this multithreading work was originally built and
// verified in has exactly 1 CPU core (`nproc` == 1). Everything about
// *correctness* was verified there -- no crash, no deadlock, and
// critically, IDENTICAL simulation results between a 1-worker and a
// 4-worker run, proving the per-thread scratch-buffer indexing scheme
// doesn't corrupt anything. But no speedup can be measured on a
// single-core machine by definition -- multithreading there is pure
// overhead (confirmed: 4 workers ran measurably SLOWER than 1 in that
// environment, exactly as expected). This tool exists so a real,
// honest speedup number can be measured on whatever hardware actually
// has multiple cores -- run it and read the comparison it prints.
//
// USAGE: kke_physics_benchmark [numTets] [numSteps] [forceMultiWorkers]
//   numTets defaults to enough tetrahedra to give the CPU real work
//   (see kDefaultNumTets below) -- a single tetrahedron is too cheap
//   to show a meaningful difference either way.
//   forceMultiWorkers overrides the auto-detected hardware_concurrency()
//   worker count for the second run -- useful for testing a specific
//   core count deliberately, or (as used to verify this tool's own
//   comparison logic before trusting it) forcing real multithreading
//   even on a machine where hardware_concurrency() reports 1.
#include "AMD_FEMFX.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <atomic>
#include <set>
#include <chrono>

using namespace AMD;

void* FmAlignedMalloc(size_t size, size_t alignment) {
    size_t roundedSize = ((size + alignment - 1) / alignment) * alignment;
    return std::aligned_alloc(alignment, roundedSize);
}
void FmAlignedFree(void* ptr) { std::free(ptr); }

namespace {

// A real thread pool. Design, and why: FEMFX indexes a per-worker
// scratch buffer array directly by whatever GetTaskSystemWorkerIndex()
// returns (confirmed by reading FEMFXSimulate.cpp directly -- e.g.
// scene->threadTempMemoryBuffer->buffers[workerIndex]), and that array
// is sized to EXACTLY numWorkerThreads, not numWorkerThreads+1
// (confirmed in FEMFXThreadTempMemory.cpp: numBuffers =
// params.numWorkerThreads). Two threads returning the same index would
// race on the same memory. This pool reserves index 0 for the main
// thread itself (permanently, for its whole lifetime) and gives real
// pool threads indices 1..N-1 -- every possible caller, whichever
// thread FEMFX's own internal task-chaining ends up running work on,
// has a stable, unique index for as long as that thread exists.
class ThreadPool {
public:
    explicit ThreadPool(int totalWorkers) : m_numWorkers(totalWorkers) {
        tl_workerIndex = 0; // main thread claims index 0
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

// Real synchronization now -- not a no-op, since tasks can genuinely
// still be pending when something waits on them.
struct SyncEvent {
    std::mutex mutex;
    std::condition_variable cv;
    bool triggered = false;
};

ThreadPool* g_pool = nullptr;
std::mutex g_threadIdsMutex;
std::set<std::thread::id> g_observedThreadIds; // proves real dispatch, not just "looks right"

int GetTaskSystemNumThreads() { return g_pool->numWorkers(); }
int GetTaskSystemWorkerIndex() { return ThreadPool::tl_workerIndex; }

void SubmitAsyncTask(const char*, FmTaskFuncCallback func, void* data, int32_t begin, int32_t end) {
    // With numWorkers==1 there are zero real pool threads (see the
    // pool's own constructor: the loop creating threads runs for
    // i in [1, totalWorkers), which is empty when totalWorkers==1) --
    // queuing a task in that configuration deadlocks forever, since
    // nothing would ever service the queue. Found by actually hitting
    // this hang (a real `timeout` kill, not assumed), not anticipated:
    // FEMFX's own numWorkerThreads=1 configuration is a real,
    // supported case (matching this whole project's original verified
    // single-threaded stand-in), and it means "run synchronously on
    // whichever thread called in," not "queue and starve."
    if (g_pool->numWorkers() <= 1) {
        func(data, begin, end);
        return;
    }
    g_pool->submit([func, data, begin, end] {
        {
            std::lock_guard<std::mutex> lock(g_threadIdsMutex);
            g_observedThreadIds.insert(std::this_thread::get_id());
        }
        func(data, begin, end);
    });
}

FmSyncEvent* CreateSyncEvent() { return reinterpret_cast<FmSyncEvent*>(new SyncEvent()); }
void DestroySyncEvent(FmSyncEvent* event) { delete reinterpret_cast<SyncEvent*>(event); }

void WaitForSyncEvent(FmSyncEvent* event) {
    auto* e = reinterpret_cast<SyncEvent*>(event);
    std::unique_lock<std::mutex> lock(e->mutex);
    e->cv.wait(lock, [e] { return e->triggered; });
}

void TriggerSyncEvent(FmSyncEvent* event) {
    auto* e = reinterpret_cast<SyncEvent*>(event);
    {
        std::lock_guard<std::mutex> lock(e->mutex);
        e->triggered = true;
    }
    e->cv.notify_all();
}

} // namespace

// Spawns one real tetrahedron into `scene` at the given position --
// same shape and setup sequence PhysicsModule::spawnTetMesh() itself
// uses (including the FmInitConnectivity fix, not the doubled-finish
// bug it replaced). Returns the tet mesh so the caller can read back
// its position for a correctness check.
FmTetMesh* spawnOneTet(FmScene* scene, float x, float y, float z, FmTetMeshBuffer** outBuffer) {
    const uint numVerts = 4, numTets = 1;
    FmVector3 restPositions[4] = {
        FmInitVector3(x + 0.0f, y + 0.0f, z + 0.0f), FmInitVector3(x + 1.0f, y + 0.0f, z + 0.0f),
        FmInitVector3(x + 0.0f, y + 1.0f, z + 0.0f), FmInitVector3(x + 0.0f, y + 0.0f, z + 1.0f),
    };
    FmTetVertIds tetVertIds[1];
    tetVertIds[0].ids[0] = 0; tetVertIds[0].ids[1] = 1; tetVertIds[0].ids[2] = 2; tetVertIds[0].ids[3] = 3;
    FmArray<uint> vertIncidentTets[4];
    for (uint i = 0; i < numVerts; ++i) vertIncidentTets[i].Add(0u);

    FmTetMeshBufferBounds bounds;
    FmComputeTetMeshBufferBounds(&bounds, nullptr, nullptr, vertIncidentTets, tetVertIds, nullptr, numVerts, numTets, false);

    FmTetMeshBufferSetupParams meshParams;
    meshParams.numVerts = bounds.numVerts; meshParams.numTets = bounds.numTets;
    meshParams.numVertIncidentTets = bounds.numVertIncidentTets;
    meshParams.maxVertAdjacentVerts = bounds.maxVertAdjacentVerts;
    meshParams.maxVerts = bounds.maxVerts; meshParams.maxExteriorFaces = bounds.maxExteriorFaces;
    meshParams.maxTetMeshes = bounds.maxTetMeshes; meshParams.collisionGroup = 0;
    meshParams.enablePlasticity = false; meshParams.enableFracture = false; meshParams.isKinematic = false;

    FmTetMesh* tetMesh = nullptr;
    FmTetMeshBuffer* tetMeshBuffer = FmCreateTetMeshBuffer(meshParams, nullptr, nullptr, &tetMesh);
    if (!tetMeshBuffer) { printf("FmCreateTetMeshBuffer failed\n"); return nullptr; }

    FmMatrix3 identity = FmMatrix3::identity();
    FmInitVertState(tetMesh, restPositions, identity, FmInitVector3(0.0f), 1.0f, FmInitVector3(0.0f));

    FmTetMaterialParams material;
    material.restDensity = 700.0f; material.youngsModulus = 1.0e7f; material.poissonsRatio = 0.3f;
    material.plasticYieldThreshold = 0.0f; material.fractureStressThreshold = 1.0e8f;
    FmInitTetState(tetMesh, tetVertIds, material);
    FmComputeMeshConstantMatrices(tetMesh);

    FmArray<uint> connectivityVertIncidentTets[4];
    for (uint i = 0; i < numVerts; ++i) connectivityVertIncidentTets[i].Add(0u);
    if (!FmInitConnectivity(tetMesh, connectivityVertIncidentTets)) { printf("FmInitConnectivity failed\n"); return nullptr; }
    FmSetMassesFromRestDensities(tetMesh, 0.0f);

    if (FmFinishTetMeshInit(tetMesh) != 0) { printf("FmFinishTetMeshInit failed\n"); return nullptr; }
    FmEnableSelfCollision(tetMesh, false);
    FmAddTetMeshBufferToScene(scene, tetMeshBuffer);
    *outBuffer = tetMeshBuffer;
    return tetMesh;
}

struct BenchmarkResult {
    bool ok = false;
    double totalMs = 0.0;
    float finalHeight = 0.0f;
    size_t distinctThreadsUsed = 0;
};

BenchmarkResult runBenchmark(int numWorkers, int numTetObjects, int numSteps) {
    BenchmarkResult result;

    ThreadPool pool(numWorkers);
    g_pool = &pool;
    {
        std::lock_guard<std::mutex> lock(g_threadIdsMutex);
        g_observedThreadIds.clear();
    }

    FmSceneSetupParams sceneParams;
    sceneParams.maxTetMeshBuffers = static_cast<uint>(numTetObjects);
    sceneParams.maxTetMeshes = static_cast<uint>(numTetObjects);
    sceneParams.maxRigidBodies = 1;
    sceneParams.maxDistanceContacts = static_cast<uint>(numTetObjects) * 16;
    sceneParams.maxVolumeContacts = static_cast<uint>(numTetObjects) * 16;
    sceneParams.maxVolumeContactVerts = static_cast<uint>(numTetObjects) * 16;
    sceneParams.maxDeformationConstraints = static_cast<uint>(numTetObjects) * 16;
    sceneParams.maxGlueConstraints = 0;
    sceneParams.maxPlaneConstraints = 0;
    sceneParams.maxRigidBodyAngleConstraints = 0;
    sceneParams.maxBroadPhasePairs = static_cast<uint>(numTetObjects) * 16;
    sceneParams.maxRigidBodyBroadPhasePairs = static_cast<uint>(numTetObjects) * 16;
    sceneParams.maxSceneVerts = static_cast<uint>(numTetObjects) * 4 + 16;
    sceneParams.maxTetMeshBufferFeatures = static_cast<uint>(numTetObjects) * 256;
    sceneParams.maxConstraintSolverDataSize = 1 << 24;
    sceneParams.numWorkerThreads = numWorkers;
    sceneParams.rigidBodiesExternal = false;

    FmScene* scene = FmCreateScene(sceneParams);
    if (!scene) { printf("FmCreateScene failed\n"); return result; }

    FmTaskSystemCallbacks callbacks;
    callbacks.SetCallbacks(GetTaskSystemNumThreads, GetTaskSystemWorkerIndex, SubmitAsyncTask,
                            CreateSyncEvent, DestroySyncEvent, WaitForSyncEvent, TriggerSyncEvent);
    FmSetSceneTaskSystemCallbacks(scene, callbacks);

    FmRigidBodySetupParams groundParams;
    groundParams.halfDimX = 50.0f; groundParams.halfDimY = 0.5f; groundParams.halfDimZ = 50.0f;
    groundParams.mass = 1.0f; groundParams.isKinematic = true; groundParams.collisionGroup = 0;
    groundParams.state.pos = FmInitVector3(0.0f, -0.5f, 0.0f);
    groundParams.bodyInertiaTensor = FmComputeBodyInertiaTensorForBox(50.0f, 0.5f, 50.0f, 1.0f);
    FmRigidBody* ground = FmCreateRigidBody(groundParams);
    FmAddRigidBodyToScene(scene, ground);

    std::vector<FmTetMesh*> tets;
    std::vector<FmTetMeshBuffer*> tetMeshBuffers;
    for (int i = 0; i < numTetObjects; ++i) {
        float x = static_cast<float>(i % 4) * 3.0f;
        float z = static_cast<float>(i / 4) * 3.0f;
        FmTetMeshBuffer* buf = nullptr;
        FmTetMesh* t = spawnOneTet(scene, x, 5.0f + i * 0.5f, z, &buf);
        if (!t) { printf("Failed to spawn tet %d\n", i); return result; }
        tets.push_back(t);
        tetMeshBuffers.push_back(buf);
    }

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < numSteps; ++i) {
        FmUpdateScene(scene, 1.0f / 60.0f);
    }
    auto t1 = std::chrono::steady_clock::now();

    result.ok = true;
    result.totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.finalHeight = FmGetVertPosition(*tets[0], 0).y;
    {
        std::lock_guard<std::mutex> lock(g_threadIdsMutex);
        result.distinctThreadsUsed = g_observedThreadIds.size();
    }

    // Explicit cleanup, matching the verified pattern used throughout
    // this whole codebase (see PhysicsModule::removeObject()) -- each
    // buffer freed before the scene itself, since runBenchmark() runs
    // twice in this same process and leaked FEMFX allocations between
    // the two calls would be a real, if minor, correctness gap.
    for (FmTetMeshBuffer* buf : tetMeshBuffers) {
        FmDestroyTetMeshBuffer(buf);
    }
    FmDestroyRigidBody(ground);
    FmDestroyScene(scene);
    return result;
}

int main(int argc, char** argv) {
    const int kDefaultNumTets = 8; // enough real work to be a meaningful comparison, not so much it's slow to run
    int numTets = (argc > 1) ? std::atoi(argv[1]) : kDefaultNumTets;
    int numSteps = (argc > 2) ? std::atoi(argv[2]) : 120;

    unsigned int hwThreads = std::thread::hardware_concurrency();
    int multiWorkers = (argc > 3) ? std::atoi(argv[3]) : ((hwThreads == 0) ? 1 : static_cast<int>(hwThreads));

    printf("kke_physics_benchmark: %d tetrahedra, %d steps, hardware_concurrency()=%u\n\n", numTets, numSteps, hwThreads);

    printf("--- 1 worker (synchronous fallback path) ---\n");
    BenchmarkResult single = runBenchmark(1, numTets, numSteps);
    if (!single.ok) { printf("1-worker run failed\n"); return 1; }
    printf("  %.2f ms total (%.4f ms/step), final height %.4f, %zu distinct thread(s) used\n\n",
           single.totalMs, single.totalMs / numSteps, single.finalHeight, single.distinctThreadsUsed);

    printf("--- %d workers (real thread pool) ---\n", multiWorkers);
    BenchmarkResult multi = runBenchmark(multiWorkers, numTets, numSteps);
    if (!multi.ok) { printf("%d-worker run failed\n", multiWorkers); return 1; }
    printf("  %.2f ms total (%.4f ms/step), final height %.4f, %zu distinct thread(s) used\n\n",
           multi.totalMs, multi.totalMs / numSteps, multi.finalHeight, multi.distinctThreadsUsed);

    printf("=== Summary ===\n");
    printf("Correctness: final heights %s (single=%.4f, multi=%.4f)\n",
           (std::abs(single.finalHeight - multi.finalHeight) < 0.0001f) ? "MATCH" : "DIFFER -- investigate before trusting either result",
           single.finalHeight, multi.finalHeight);
    if (multiWorkers > 1) {
        double speedup = single.totalMs / multi.totalMs;
        printf("Speed: %.2fx (%s than single-threaded)\n", speedup, speedup > 1.0 ? "faster" : "SLOWER");
        if (hwThreads <= 1) {
            printf("Note: hardware_concurrency() reported %u -- on a genuinely single-core\n"
                   "machine, multithreading is pure overhead and SLOWER here is expected,\n"
                   "not a bug. Run this on real multi-core hardware for a meaningful number.\n", hwThreads);
        }
    } else {
        printf("hardware_concurrency() reported 1 -- both runs used the same synchronous\n"
               "fallback path, so there's nothing to compare here. Run this on real\n"
               "multi-core hardware to see an actual speedup number.\n");
    }

    return 0;
}

