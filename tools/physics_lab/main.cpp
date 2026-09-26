// kke_physics_lab -- headless FEMFX experiments, no window, no GPU.
//
// Why: physics questions ("does this fracture pattern blow up?", "how
// many falling rocks fit in a 16 ms frame on one core?") should be
// answerable with numbers, repeatably, in CI and on the min-spec box,
// without clicking through a demo. Same FEMFX setup sequence as
// PhysicsModule::spawnTetMeshInternal (bounds -> buffer -> vert/tet state
// -> connectivity -> masses -> finish -> flags), same materials.
//
// USAGE
//   kke_physics_lab fracture [pattern] [seeds] [threads]
//       Drops baked bricks/panes (kke::bakeFracture) at 20 m/s, one per
//       seed, and reports pieces, runaways (BUG-043) and step times.
//       pattern: voronoi | splinters | shards | radial | all (default all)
//   kke_physics_lab volcano [seconds] [threads] [rocksPerSecond] [maxRocks]
//       Worst-case "run from the erupting volcano" load: breakable rocks
//       raining onto the ground at a fixed rate, debris kept under a
//       budget (oldest pieces removed), timed per step. See docs/SCALING.md.
#include "AMD_FEMFX.h"
#include "kke/BreakGraph.h"
#include "kke/ParticleFluid.h"
#include "kke/VoronoiFracture.h"
#include "kke/VoxelTets.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#if defined(_WIN32)
#include <malloc.h>
#endif
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

using namespace AMD;

void* FmAlignedMalloc(size_t size, size_t alignment) {
    size_t rounded = ((size + alignment - 1) / alignment) * alignment;
    #if defined(_WIN32)
    return _aligned_malloc(rounded, alignment); // no std::aligned_alloc in the Windows C runtime
#else
    return std::aligned_alloc(alignment, rounded);
#endif
}
void FmAlignedFree(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

namespace {

// ---------------------------------------------------------------- task system
// Same scheme as PhysicsModule: worker index 0 = the calling thread.
class ThreadPool {
public:
    explicit ThreadPool(int n) : m_n(n) {
        tl_index = 0;
        for (int i = 1; i < n; ++i) m_threads.emplace_back([this, i] { tl_index = i; loop(); });
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> l(m_mutex); m_stop = true; }
        m_cv.notify_all();
        for (auto& t : m_threads) t.join();
    }
    int size() const { return m_n; }
    void submit(std::function<void()> f) {
        { std::lock_guard<std::mutex> l(m_mutex); m_tasks.push(std::move(f)); }
        m_cv.notify_one();
    }
    static thread_local int tl_index;

private:
    void loop() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> l(m_mutex);
                m_cv.wait(l, [this] { return m_stop || !m_tasks.empty(); });
                if (m_stop && m_tasks.empty()) return;
                f = std::move(m_tasks.front());
                m_tasks.pop();
            }
            f();
        }
    }
    int m_n;
    std::vector<std::thread> m_threads;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
};
thread_local int ThreadPool::tl_index = -1;
ThreadPool* g_pool = nullptr;

struct SyncEvent { std::mutex m; std::condition_variable cv; bool on = false; };
int numThreads() { return g_pool->size(); }
int workerIndex() { return ThreadPool::tl_index; }
void submitTask(const char*, FmTaskFuncCallback f, void* d, int32_t b, int32_t e) {
    if (g_pool->size() <= 1) { f(d, b, e); return; }
    g_pool->submit([f, d, b, e] { f(d, b, e); });
}
FmSyncEvent* createEvent() { return reinterpret_cast<FmSyncEvent*>(new SyncEvent()); }
void destroyEvent(FmSyncEvent* e) { delete reinterpret_cast<SyncEvent*>(e); }
void waitEvent(FmSyncEvent* ev) {
    auto* e = reinterpret_cast<SyncEvent*>(ev);
    std::unique_lock<std::mutex> l(e->m);
    e->cv.wait(l, [e] { return e->on; });
}
void triggerEvent(FmSyncEvent* ev) {
    auto* e = reinterpret_cast<SyncEvent*>(ev);
    { std::lock_guard<std::mutex> l(e->m); e->on = true; }
    e->cv.notify_all();
}

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// ---------------------------------------------------------------- scene

FmScene* makeScene(int threads, uint maxObjects, uint maxPieces) {
    FmSceneSetupParams p;
    p.maxTetMeshBuffers = maxObjects;
    p.maxTetMeshes = maxPieces;
    p.maxRigidBodies = 4;
    p.maxDistanceContacts = 65536;
    p.maxVolumeContacts = 8192;
    p.maxVolumeContactVerts = 131072;
    p.maxDeformationConstraints = maxObjects * 64;
    p.maxGlueConstraints = 8;
    p.maxPlaneConstraints = 0;
    p.maxRigidBodyAngleConstraints = 4;
    p.maxBroadPhasePairs = 16384;
    p.maxRigidBodyBroadPhasePairs = 4096;
    p.maxSceneVerts = 131072;
    p.maxTetMeshBufferFeatures = 8192;
    p.numWorkerThreads = threads;
    p.rigidBodiesExternal = false;
    p.maxConstraintSolverDataSize = FmEstimateSceneConstraintSolverDataSize(p);
    FmScene* scene = FmCreateScene(p);
    FmTaskSystemCallbacks cb;
    cb.SetCallbacks(numThreads, workerIndex, submitTask, createEvent, destroyEvent, waitEvent, triggerEvent);
    FmSetSceneTaskSystemCallbacks(scene, cb);
    FmSceneControlParams control = FmGetSceneControlParams(*scene);
    control.collisionPlanes.minY = 0.0f; // the ground, as in PhysicsModule
    if (const char* e = std::getenv("LAB_MASSDAMP")) control.kRayleighMassDamping = std::atof(e);
    if (const char* e = std::getenv("LAB_STIFFDAMP")) control.kRayleighStiffnessDamping = std::atof(e);
    if (const char* e = std::getenv("LAB_CG")) control.defaultMaxCgIterations = std::atoi(e);
    FmSetSceneControlParams(scene, control);
    if (std::getenv("LAB_NOSELF")) FmSetGroupsCanCollide(scene, 1, 1, false);
    return scene;
}

struct Object {
    FmTetMeshBuffer* buffer = nullptr;
    FmTetMesh* mesh = nullptr;
    std::vector<FmTetVertIds> tetVertIds;
    std::vector<FmArray<uint>> incident;
    std::vector<FmFractureGroupCounts> groupCounts;
    std::vector<uint> groupIds;
    uint sceneId = 0;
    std::vector<uint16_t> flags;
    double bornMs = 0.0;
    std::vector<uint32_t> bakedTet, bakedVert; // breakable parts only
    int grace = 0;                              // ticks left before a fresh part may break
    int pending = -1;                           // >= 0: overloaded, splitting in this many ticks
};

struct Mat { float density, stiffness, poisson, fracture; };
const Mat kStone{ 2500.0f, 3.0e7f, 0.25f, 4000.0f };
const Mat kGlass{ 2500.0f, 7.0e7f, 0.22f, 2000.0f };
const Mat kWood{ 600.0f, 1.0e7f, 0.3f, 1500.0f };

// Mirrors PhysicsModule::spawnTetMeshInternal + tetStrength handling.
Object* spawn(FmScene* scene, const kke::TetMeshData& m, const glm::vec3& pos, const Mat& mat, const std::vector<uint16_t>& flags,
              const std::vector<float>& strength, const glm::vec3& vel) {
    auto* o = new Object();
    const uint nv = static_cast<uint>(m.vertices.size()), nt = static_cast<uint>(m.tets.size());
    std::vector<FmVector3> rest(nv);
    for (uint i = 0; i < nv; ++i) rest[i] = FmInitVector3(pos.x + m.vertices[i].x, pos.y + m.vertices[i].y, pos.z + m.vertices[i].z);
    o->tetVertIds.resize(nt);
    o->incident.resize(nv);
    for (uint t = 0; t < nt; ++t) {
        for (int j = 0; j < 4; ++j) o->tetVertIds[t].ids[j] = m.tets[t][j];
        for (int j = 0; j < 4; ++j) o->incident[m.tets[t][j]].Add(t);
    }
    o->groupCounts.resize(nt);
    o->groupIds.resize(nt);
    o->flags = flags;
    FmTetMeshBufferBounds bounds;
    FmComputeTetMeshBufferBounds(&bounds, o->groupCounts.data(), o->groupIds.data(), o->incident.data(), o->tetVertIds.data(),
                                 o->flags.empty() ? nullptr : o->flags.data(), nv, nt, true);
    FmTetMeshBufferSetupParams sp;
    sp.numVerts = bounds.numVerts;
    sp.numTets = bounds.numTets;
    sp.numVertIncidentTets = bounds.numVertIncidentTets;
    sp.maxVertAdjacentVerts = bounds.maxVertAdjacentVerts;
    sp.maxVerts = bounds.maxVerts;
    sp.maxExteriorFaces = bounds.maxExteriorFaces;
    sp.maxTetMeshes = bounds.maxTetMeshes;
    sp.collisionGroup = std::getenv("LAB_NOSELF") ? 1 : 0;
    sp.enablePlasticity = false;
    sp.enableFracture = true;
    sp.isKinematic = false;
    o->buffer = FmCreateTetMeshBuffer(sp, o->groupCounts.data(), o->groupIds.data(), &o->mesh);
    FmInitVertState(o->mesh, rest.data(), FmMatrix3::identity(), FmInitVector3(0.0f), 1.0f, FmInitVector3(vel.x, vel.y, vel.z));
    FmTetMaterialParams mp;
    mp.restDensity = mat.density;
    mp.youngsModulus = mat.stiffness;
    mp.poissonsRatio = mat.poisson;
    mp.plasticYieldThreshold = 0.0f;
    mp.fractureStressThreshold = mat.fracture;
    FmInitTetState(o->mesh, o->tetVertIds.data(), mp);
    FmComputeMeshConstantMatrices(o->mesh);
    std::vector<FmArray<uint>> conn(nv);
    for (uint t = 0; t < nt; ++t) for (int j = 0; j < 4; ++j) conn[m.tets[t][j]].Add(t);
    if (!FmInitConnectivity(o->mesh, conn.data())) { std::printf("FmInitConnectivity failed\n"); std::exit(1); }
    FmSetMassesFromRestDensities(o->mesh, 0.0f);
    if (FmFinishTetMeshInit(o->mesh) != 0) { std::printf("FmFinishTetMeshInit failed\n"); std::exit(1); }
    for (uint t = 0; t < o->flags.size(); ++t) if (o->flags[t]) FmSetTetFlags(o->mesh, t, o->flags[t]);
    FmEnableSelfCollision(o->mesh, false);
    FmEnableSleeping(scene, o->mesh, true);
    o->sceneId = FmAddTetMeshBufferToScene(scene, o->buffer);
    for (uint t = 0; t < strength.size(); ++t) {
        if (strength[t] == 1.0f) continue;
        FmTetMaterialParams p = mp;
        p.fractureStressThreshold = mat.fracture * strength[t];
        FmUpdateTetMaterialParams(scene, o->mesh, t, p);
    }
    o->bornMs = nowMs();
    return o;
}

void destroy(FmScene* scene, Object* o) {
    FmRemoveTetMeshBufferFromScene(scene, o->sceneId);
    FmDestroyTetMeshBuffer(o->buffer);
    delete o;
}

float extent(const Object& o) {
    float e = 0.0f;
    for (uint m = 0; m < FmGetNumTetMeshes(*o.buffer); ++m) {
        const FmTetMesh* p = FmGetTetMesh(*o.buffer, m);
        if (!p) continue;
        FmVector3 lo = FmGetMinPosition(*p), hi = FmGetMaxPosition(*p);
        for (float v : { lo.x, lo.y, lo.z, hi.x, hi.y, hi.z }) e = std::max(e, std::isfinite(v) ? std::fabs(v) : 1e30f);
    }
    return e;
}

kke::TetMeshData gridBox(glm::ivec3 c, glm::vec3 size) {
    kke::TetMeshData box;
    auto vi = [&](int x, int y, int z) { return static_cast<uint32_t>((z * (c.y + 1) + y) * (c.x + 1) + x); };
    for (int z = 0; z <= c.z; ++z)
        for (int y = 0; y <= c.y; ++y)
            for (int x = 0; x <= c.x; ++x) box.vertices.push_back((glm::vec3(x, y, z) / glm::vec3(c) - 0.5f) * size);
    for (int z = 0; z < c.z; ++z)
        for (int y = 0; y < c.y; ++y)
            for (int x = 0; x < c.x; ++x) {
                uint32_t v0 = vi(x, y, z), v1 = vi(x + 1, y, z), v2 = vi(x + 1, y + 1, z), v3 = vi(x, y + 1, z);
                uint32_t v4 = vi(x, y, z + 1), v5 = vi(x + 1, y, z + 1), v6 = vi(x + 1, y + 1, z + 1), v7 = vi(x, y + 1, z + 1);
                box.tets.push_back({ v0, v1, v2, v6 });
                box.tets.push_back({ v0, v2, v3, v6 });
                box.tets.push_back({ v0, v3, v7, v6 });
                box.tets.push_back({ v0, v7, v4, v6 });
                box.tets.push_back({ v0, v4, v5, v6 });
                box.tets.push_back({ v0, v5, v1, v6 });
            }
    return box;
}

kke::FracturePattern parsePattern(const std::string& s) {
    if (s == "splinters") return kke::FracturePattern::Splinters;
    if (s == "shards") return kke::FracturePattern::Shards;
    if (s == "radial") return kke::FracturePattern::Radial;
    return kke::FracturePattern::Voronoi;
}

// ---------------------------------------------------------------- breakables
// The same scheme as PhysicsModule's Breakable (see PhysicsModule.h):
// plain FEMFX bodies, KKE decides where it breaks (kke::BreakGraph) and
// swaps a body for one body per group that still holds together.
struct LabBreakable {
    kke::TetMeshData mesh;
    kke::BreakGraph graph;
    Mat mat;
    glm::vec3 origin{0.0f};
    std::vector<Object*> parts;
    std::vector<Object*> partOf;
    std::vector<uint32_t> localOf;
    int splits = 0;
};

Object* spawnPart(FmScene* scene, LabBreakable& b, const std::vector<uint32_t>& tets, const Object* from, const glm::vec3& vel) {
    kke::TetMeshData sub;
    std::vector<uint32_t> localOfVert(b.mesh.vertices.size(), UINT32_MAX), bakedVert;
    for (uint32_t t : tets) {
        std::array<uint32_t, 4> ids{};
        for (int k = 0; k < 4; ++k) {
            uint32_t v = b.mesh.tets[t][k];
            if (localOfVert[v] == UINT32_MAX) { localOfVert[v] = static_cast<uint32_t>(sub.vertices.size()); sub.vertices.push_back(b.mesh.vertices[v]); bakedVert.push_back(v); }
            ids[k] = localOfVert[v];
        }
        sub.tets.push_back(ids);
    }
    Mat m = b.mat;
    m.fracture = 1.0e20f; // FEMFX never fractures; it only measures stress
    Object* o = spawn(scene, sub, b.origin, m, {}, {}, vel);
    o->bakedTet = tets;
    o->bakedVert = bakedVert;
    for (size_t t = 0; t < tets.size(); ++t) { b.partOf[tets[t]] = o; b.localOf[tets[t]] = static_cast<uint32_t>(t); }
    if (from) {
        o->grace = std::getenv("LAB_GRACE") ? std::atoi(std::getenv("LAB_GRACE")) : 0;
        std::vector<uint32_t> oldLocal(b.mesh.vertices.size(), UINT32_MAX);
        for (uint32_t v = 0; v < from->bakedVert.size(); ++v) oldLocal[from->bakedVert[v]] = v;
        for (uint32_t v = 0; v < bakedVert.size(); ++v) {
            uint32_t ov = oldLocal[bakedVert[v]];
            if (ov == UINT32_MAX) continue;
            FmSetVertPosition(scene, o->mesh, v, FmGetVertPosition(*from->mesh, ov));
            FmSetVertVelocity(scene, o->mesh, v, FmGetVertVelocity(*from->mesh, ov));
        }
    }
    b.parts.push_back(o);
    return o;
}

void updateBreakable(FmScene* scene, LabBreakable& b) {
    std::vector<Object*> overloaded;
    const int window = std::getenv("LAB_WINDOW") ? std::atoi(std::getenv("LAB_WINDOW")) : 0;
    for (Object* p : b.parts) {
        if (p->grace > 0) { --p->grace; continue; }
        if (FmIsTetMeshSleeping(*p->mesh) && p->pending < 0) continue;
        bool any = false;
        auto same = [&](uint32_t t) { return b.partOf[t] == p; };
        for (uint32_t t = 0; t < p->bakedTet.size(); ++t)
            if (b.graph.isBorderTet(p->bakedTet[t])) any |= b.graph.report(p->bakedTet[t], FmGetTetMaxStress(*p->mesh, t), same);
        if (any && p->pending < 0) p->pending = window;
        if (p->pending == 0) overloaded.push_back(p);
        if (p->pending > 0) --p->pending;
    }
    for (Object* p : overloaded) {
        auto groups = b.graph.groups(p->bakedTet, [&](uint32_t t) { return b.partOf[t] == p; });
        if (groups.size() < 2) continue;
        for (const auto& g : groups) spawnPart(scene, b, g, p, glm::vec3(0.0f));
        b.parts.erase(std::find(b.parts.begin(), b.parts.end(), p));
        destroy(scene, p);
        ++b.splits;
    }
}

float extent(const LabBreakable& b) {
    float e = 0.0f;
    for (const Object* p : b.parts) e = std::max(e, extent(*p));
    return e;
}

// ---------------------------------------------------------------- fracture

int runFracture(const std::string& which, int seeds, int threads) {
    std::vector<kke::FracturePattern> patterns;
    if (which == "all") patterns = { kke::FracturePattern::Voronoi, kke::FracturePattern::Splinters, kke::FracturePattern::Shards, kke::FracturePattern::Radial };
    else patterns = { parsePattern(which) };
    int failures = 0;
    for (kke::FracturePattern pattern : patterns) {
        int exploded = 0;
        double stepTotal = 0.0, stepMax = 0.0;
        size_t piecesTotal = 0, bakedTotal = 0;
        for (int s = 1; s <= seeds; ++s) {
            ThreadPool pool(threads);
            g_pool = &pool;
            FmScene* scene = makeScene(threads, 4, 1024);
            const bool pane = pattern == kke::FracturePattern::Radial;
            kke::TetMeshData box = pane ? gridBox({ 12, 1, 12 }, { 2.0f, 0.15f, 2.0f }) : gridBox({ 8, 4, 4 }, { 1.0f, 0.5f, 0.5f });
            kke::FractureSeedOptions o;
            o.pattern = pattern;
            o.chunkSize = pane ? 0.5f : 0.25f;
            o.seed = kke::fractureSeed(1, static_cast<uint32_t>(s));
            o.cellsPerCluster = pattern == kke::FracturePattern::Voronoi ? 3 : 0;
            o.hasImpactPoint = pane;
            if (std::getenv("LAB_NOCLUSTER")) o.cellsPerCluster = 0;
            kke::BakedFracture b = kke::bakeFracture(box, o);
            if (std::getenv("LAB_NOFLAGS")) b.flags.assign(b.flags.size(), 0);
            if (std::getenv("LAB_ORIG")) b.cut.mesh = box;
            if (std::getenv("LAB_NOSTRENGTH")) b.cut.tetStrength.clear();
            bakedTotal += b.pieces;
            const Mat& mat = pane ? kGlass : (pattern == kke::FracturePattern::Splinters ? kWood : kStone);
            Mat m2 = mat;
            if (std::getenv("LAB_TOUGH")) m2.fracture *= std::atof(std::getenv("LAB_TOUGH"));
            float vy = std::getenv("LAB_VY") ? std::atof(std::getenv("LAB_VY")) : -20.0f;
            const bool femfxFracture = std::getenv("LAB_FEMFX") != nullptr; // the old way, for comparison
            Object* obj = nullptr;
            LabBreakable lb;
            if (femfxFracture) {
                obj = spawn(scene, b.cut.mesh, { 0.0f, 3.0f, 0.0f }, m2, b.flags, b.cut.tetStrength, { 0.0f, vy, 0.0f });
            } else {
                lb.mesh = b.cut.mesh;
                lb.graph = kke::BreakGraph(b.cut.mesh, b.cut.chunkOfTet, b.cut.tetStrength);
                lb.graph.arm(m2.fracture);
                lb.mat = m2;
                lb.origin = { 0.0f, 3.0f, 0.0f };
                lb.partOf.assign(b.cut.mesh.tets.size(), nullptr);
                lb.localOf.assign(b.cut.mesh.tets.size(), 0);
                std::vector<uint32_t> all(b.cut.mesh.tets.size());
                for (uint32_t t = 0; t < all.size(); ++t) all[t] = t;
                spawnPart(scene, lb, all, nullptr, { 0.0f, vy, 0.0f });
            }
            float worst = 0.0f;
            for (int step = 0; step < 240; ++step) {
                double t0 = nowMs();
                int sub = std::getenv("LAB_SUB") ? std::atoi(std::getenv("LAB_SUB")) : 1;
                for (int k = 0; k < sub; ++k) FmUpdateScene(scene, 1.0f / (60.0f * sub));
                if (!femfxFracture) updateBreakable(scene, lb);
                double dt = nowMs() - t0;
                stepTotal += dt;
                stepMax = std::max(stepMax, dt);
                worst = std::max(worst, femfxFracture ? extent(*obj) : extent(lb));
                if (std::getenv("LAB_TRACE") && s == 1 && step % 5 == 0)
                    std::printf("    step %d pieces %zu extent %.2f\n", step, femfxFracture ? size_t(FmGetNumTetMeshes(*obj->buffer)) : lb.parts.size(), femfxFracture ? extent(*obj) : extent(lb));
            }
            uint pieces = femfxFracture ? FmGetNumTetMeshes(*obj->buffer) : static_cast<uint>(lb.parts.size());
            piecesTotal += pieces;
            if (worst > 100.0f) {
                ++exploded;
                std::printf("  %s seed %d: EXPLODED (extent %.3g m), %u pieces of %zu baked\n", kke::fracturePatternName(pattern), s, worst, pieces, b.pieces);
            }
            if (obj) destroy(scene, obj);
            for (Object* p : lb.parts) destroy(scene, p);
            FmDestroyScene(scene);
        }
        std::printf("%-16s %d drops: %d exploded, avg %.1f pieces (of %.1f baked), step avg %.2f ms max %.2f ms\n", kke::fracturePatternName(pattern), seeds,
                    exploded, double(piecesTotal) / seeds, double(bakedTotal) / seeds, stepTotal / (seeds * 240.0), stepMax);
        failures += exploded;
    }
    return failures ? 1 : 0;
}

// Resting stress: the same brick, unsnapped vs snapped, lowered onto
// the ground unbreakable. Peak tet stress while it settles.
int runRest(int seeds) {
    for (int s = 1; s <= seeds; ++s) {
        for (int snapped = 0; snapped < 2; ++snapped) {
            ThreadPool pool(1);
            g_pool = &pool;
            FmScene* scene = makeScene(1, 4, 256);
            kke::TetMeshData box = gridBox({ 8, 4, 4 }, { 1.0f, 0.5f, 0.5f });
            kke::FractureSeedOptions o;
            o.chunkSize = 0.25f;
            o.seed = kke::fractureSeed(1, static_cast<uint32_t>(s));
            kke::BakedFracture b = kke::bakeFracture(box, o);
            Mat m = kStone;
            m.fracture = 1e12f;
            Object* obj = spawn(scene, snapped ? b.cut.mesh : box, { 0.0f, 0.26f, 0.0f }, m, b.flags, {}, { 0.0f, 0.0f, 0.0f });
            float peak = 0.0f;
            uint peakTet = 0;
            for (int step = 0; step < 120; ++step) {
                FmUpdateScene(scene, 1.0f / 60.0f);
                for (uint t = 0; t < FmGetNumTets(*obj->mesh); ++t) {
                    float st = FmGetTetMaxStress(*obj->mesh, t);
                    if (st > peak) { peak = st; peakTet = t; }
                }
            }
            std::printf("seed %d %s: peak resting stress %.0f (tet %u)\n", s, snapped ? "snapped" : "grid   ", peak, peakTet);
            destroy(scene, obj);
            FmDestroyScene(scene);
        }
    }
    return 0;
}


kke::TetMeshData sphere(int cells, float radius) {
    kke::TetMeshData m = gridBox({ cells, cells, cells }, glm::vec3(2.0f));
    for (glm::vec3& v : m.vertices) {
        glm::vec3 q = v * v;
        v = glm::vec3(v.x * std::sqrt(std::max(0.0f, 1.0f - q.y / 2 - q.z / 2 + q.y * q.z / 3)),
                      v.y * std::sqrt(std::max(0.0f, 1.0f - q.z / 2 - q.x / 2 + q.z * q.x / 3)),
                      v.z * std::sqrt(std::max(0.0f, 1.0f - q.x / 2 - q.y / 2 + q.x * q.y / 3))) * radius;
    }
    return m;
}

// The sandbox's test: a 0.8 m breakable crate at rest, armed after it
// settles (threshold = base + 1.25x its resting stress), then the
// sandbox's ball (0.22 m, 3000 kg/m3, soft) at `speed`. For a range of
// base thresholds: how many pieces, and the peak border stress seen.
int runShoot(const std::string& which, float speed) {
    std::vector<float> bases = { 1e4f, 3e4f, 1e5f, 3e5f, 1e6f, 3e6f };
    if (const char* e = std::getenv("LAB_BASES")) { bases.clear(); for (const char* c = e; *c;) { bases.push_back(std::strtof(c, const_cast<char**>(&c))); if (*c == ',') ++c; } }
    kke::FracturePattern pattern = parsePattern(which);
    std::printf("%s crate, ball at %.0f m/s:\n", kke::fracturePatternName(pattern), speed);
    for (float base : bases) {
        ThreadPool pool(1);
        g_pool = &pool;
        FmScene* scene = makeScene(1, 256, 1024);
        const bool pane = pattern == kke::FracturePattern::Radial;
        kke::TetMeshData box = pane ? gridBox({ 10, 1, 10 }, { 1.4f, 0.06f, 1.4f }) : gridBox({ 5, 5, 5 }, { 0.8f, 0.8f, 0.8f });
        kke::FractureSeedOptions o;
        o.pattern = pattern;
        o.chunkSize = pane ? 0.45f : 0.3f;
        o.seed = 7;
        o.hasImpactPoint = true;
        kke::BakedFracture b = kke::bakeFracture(box, o);
        LabBreakable lb;
        lb.mesh = b.cut.mesh;
        lb.graph = kke::BreakGraph(b.cut.mesh, b.cut.chunkOfTet, b.cut.tetStrength);
        lb.graph.arm(1e20f); // unbreakable while it settles
        lb.mat = kStone;
        lb.origin = { 0.0f, pane ? 0.031f : 0.401f, 0.0f };
        lb.partOf.assign(b.cut.mesh.tets.size(), nullptr);
        lb.localOf.assign(b.cut.mesh.tets.size(), 0);
        std::vector<uint32_t> all(b.cut.mesh.tets.size());
        for (uint32_t t = 0; t < all.size(); ++t) all[t] = t;
        spawnPart(scene, lb, all, nullptr, glm::vec3(0.0f));
        std::vector<float> rest(all.size(), 0.0f);
        const int settleSteps = std::getenv("LAB_SETTLE") ? std::atoi(std::getenv("LAB_SETTLE")) : 90;
        for (int step = 0; step < settleSteps; ++step) {
            FmUpdateScene(scene, 1.0f / 60.0f);
            Object* p = lb.parts[0];
            for (uint32_t t = 0; t < p->bakedTet.size(); ++t) rest[p->bakedTet[t]] = std::max(rest[p->bakedTet[t]], FmGetTetMaxStress(*p->mesh, t));
        }
        lb.graph.arm(base, rest);
        float restMax = *std::max_element(rest.begin(), rest.end());
        Mat ballMat{ 3000.0f, 2.0e6f, 0.3f, 1e20f };
        Object* ball = speed > 0.0f ? spawn(scene, sphere(3, 0.22f), { -1.5f, 0.45f, 0.05f }, ballMat, {}, {}, { speed, 0.0f, 0.0f }) : nullptr;
        float peak = 0.0f;
        for (int step = 0; step < 300; ++step) {
            FmUpdateScene(scene, 1.0f / 60.0f);
            for (Object* p : lb.parts)
                for (uint32_t t = 0; t < p->bakedTet.size(); ++t)
                    if (lb.graph.isBorderTet(p->bakedTet[t])) peak = std::max(peak, FmGetTetMaxStress(*p->mesh, t));
            updateBreakable(scene, lb);
        }
        std::printf("  base %8.0f: %2zu pieces of %zu (rest max %.0f, peak border stress %.0f)\n", base, lb.parts.size(), b.pieces, restMax, peak);
        if (ball) destroy(scene, ball);
        for (Object* p : lb.parts) destroy(scene, p);
        FmDestroyScene(scene);
    }
    return 0;
}

// "Run from the erupting volcano": breakable boulders (0.5-1.4 m, 8-14
// Voronoi pieces each) thrown from a crater at `rate` per second onto a
// 40 x 40 m play area, plus a lava flow of `lavaCount` particles, for
// `seconds`. Debris budget (docs/OPTIMIZATION.md rule 5): at most `maxBodies`
// FEMFX bodies; past that, the oldest sleeping piece goes, then the
// oldest piece. Reports step time per second of simulated time.
int runVolcano(float seconds, int threads, float rate, int maxBodies, int lavaCount) {
    ThreadPool pool(threads);
    g_pool = &pool;
    FmScene* scene = makeScene(threads, 1024, 8192);
    const Mat rock{ 2600.0f, 3.0e7f, 0.25f, 1.2e5f };
    std::deque<LabBreakable> boulders;
    kke::ParticleFluid::Params fp;
    fp.radius = 0.08f;
    fp.substeps = 1;
    kke::ParticleFluid lava(fp, static_cast<size_t>(std::max(1, lavaCount)));
    uint32_t rng = 12345;
    auto rnd = [&] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng >> 8) * (1.0f / 16777216.0f); };
    float spawnAccum = 0.0f;
    const int steps = static_cast<int>(seconds * 60.0f);
    std::vector<double> window;
    double fluidMs = 0.0, physMs = 0.0, worstStep = 0.0;
    size_t thrown = 0, removed = 0;
    std::printf("volcano: %.0f s, %d thread(s), %.1f boulders/s, body budget %d, lava %d particles\n", seconds, threads, rate, maxBodies, lavaCount);
    std::printf("   t  bodies  awake  step avg  step max  lava ms\n");
    for (int step = 0; step < steps; ++step) {
        spawnAccum += rate / 60.0f;
        while (spawnAccum >= 1.0f) {
            spawnAccum -= 1.0f;
            ++thrown;
            boulders.emplace_back();
            LabBreakable& b = boulders.back();
            float r = 0.25f + 0.45f * rnd();
            kke::TetMeshData m = sphere(3, r);
            kke::FractureSeedOptions o;
            o.pattern = kke::FracturePattern::Voronoi;
            o.chunkSize = r * 0.8f;
            o.seed = kke::fractureSeed(1, static_cast<uint32_t>(thrown));
            o.maxPieces = 14;
            kke::BakedFracture baked = kke::bakeFracture(m, o);
            b.mesh = baked.cut.mesh;
            b.graph = kke::BreakGraph(baked.cut.mesh, baked.cut.chunkOfTet, baked.cut.tetStrength);
            b.graph.arm(rock.fracture);
            b.mat = rock;
            b.origin = glm::vec3(-6.0f + 12.0f * rnd(), 18.0f + 4.0f * rnd(), -30.0f);
            b.partOf.assign(b.mesh.tets.size(), nullptr);
            b.localOf.assign(b.mesh.tets.size(), 0);
            std::vector<uint32_t> all(b.mesh.tets.size());
            for (uint32_t t = 0; t < all.size(); ++t) all[t] = t;
            spawnPart(scene, b, all, nullptr, glm::vec3(-4.0f + 8.0f * rnd(), 4.0f + 6.0f * rnd(), 12.0f + 10.0f * rnd()));
        }
        for (int k = 0; k < 6 && lavaCount > 0 && lava.size() < static_cast<size_t>(lavaCount); ++k)
            lava.add(glm::vec3(-1.0f + 2.0f * rnd(), 1.0f, -20.0f + rnd()), glm::vec3(0.0f, 0.0f, 3.0f), 1100.0f, 0);

        // Budget: count bodies, drop the oldest (sleeping first).
        size_t bodies = 0;
        for (auto& b : boulders) bodies += b.parts.size();
        while (bodies > static_cast<size_t>(maxBodies)) {
            Object* victim = nullptr;
            LabBreakable* owner = nullptr;
            for (auto& b : boulders) {
                for (Object* p : b.parts)
                    if (FmIsTetMeshSleeping(*p->mesh) && (!victim || p->bornMs < victim->bornMs)) { victim = p; owner = &b; }
            }
            if (!victim) {
                for (auto& b : boulders)
                    for (Object* p : b.parts)
                        if (!victim || p->bornMs < victim->bornMs) { victim = p; owner = &b; }
            }
            if (!victim) break;
            owner->parts.erase(std::find(owner->parts.begin(), owner->parts.end(), victim));
            for (auto& po : owner->partOf) if (po == victim) po = nullptr;
            destroy(scene, victim);
            --bodies;
            ++removed;
        }
        while (!boulders.empty() && boulders.front().parts.empty()) boulders.pop_front();

        double t0 = nowMs();
        FmUpdateScene(scene, 1.0f / 60.0f);
        for (auto& b : boulders) updateBreakable(scene, b);
        double t1 = nowMs();
        if (lavaCount > 0) lava.step(1.0f / 60.0f);
        double t2 = nowMs();
        physMs = t1 - t0;
        fluidMs += t2 - t1;
        window.push_back(physMs + (t2 - t1));
        worstStep = std::max(worstStep, physMs + (t2 - t1));
        if ((step + 1) % 60 == 0) {
            size_t awake = 0;
            bodies = 0;
            for (auto& b : boulders)
                for (Object* p : b.parts) { ++bodies; awake += !FmIsTetMeshSleeping(*p->mesh); }
            double sum = 0.0, mx = 0.0;
            for (double w : window) { sum += w; mx = std::max(mx, w); }
            std::printf("%4d  %6zu  %5zu  %6.2f ms  %6.2f ms  %5.2f\n", (step + 1) / 60, bodies, awake, sum / window.size(), mx, fluidMs / 60.0);
            window.clear();
            fluidMs = 0.0;
        }
    }
    std::printf("thrown %zu boulders, %zu pieces removed by the budget, worst step %.2f ms (16.7 ms = one 60 Hz frame)\n", thrown, removed, worstStep);
    for (auto& b : boulders) for (Object* p : b.parts) destroy(scene, p);
    FmDestroyScene(scene);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "fracture";
    if (mode == "fracture") {
        std::string pattern = argc > 2 ? argv[2] : "all";
        int seeds = argc > 3 ? std::atoi(argv[3]) : 20;
        int threads = argc > 4 ? std::atoi(argv[4]) : 1;
        return runFracture(pattern, seeds, threads);
    }
    if (mode == "volcano")
        return runVolcano(argc > 2 ? std::atof(argv[2]) : 30.0f, argc > 3 ? std::atoi(argv[3]) : 1, argc > 4 ? std::atof(argv[4]) : 2.0f,
                          argc > 5 ? std::atoi(argv[5]) : 150, argc > 6 ? std::atoi(argv[6]) : 0);
    if (mode == "shoot") return runShoot(argc > 2 ? argv[2] : "voronoi", argc > 3 ? std::atof(argv[3]) : 18.0f);
    if (mode == "freefall") {
        ThreadPool pool(1);
        g_pool = &pool;
        FmScene* scene = makeScene(1, 4, 256);
        kke::TetMeshData box = gridBox({ 8, 4, 4 }, { 1.0f, 0.5f, 0.5f });
        Mat m = kStone;
        m.fracture = 1e20f;
        Object* o = spawn(scene, box, { 0.0f, 8.0f, 0.0f }, m, {}, {}, { 0.0f, -20.0f, 0.0f });
        for (int step = 0; step < 30; ++step) {
            float mx = 0.0f;
            for (uint t = 0; t < FmGetNumTets(*o->mesh); ++t) mx = std::max(mx, FmGetTetMaxStress(*o->mesh, t));
            std::printf("step %d y %.2f max stress %.1f\n", step, FmGetVertPosition(*o->mesh, 0).y, mx);
            FmUpdateScene(scene, 1.0f / 60.0f);
        }
        return 0;
    }
    if (mode == "rest") return runRest(argc > 2 ? std::atoi(argv[2]) : 3);
    std::printf("usage: kke_physics_lab fracture [voronoi|splinters|shards|radial|all] [seeds] [threads]\n");
    return 2;
}
