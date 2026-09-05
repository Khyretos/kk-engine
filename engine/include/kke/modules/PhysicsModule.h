#pragma once

#include "kke/Module.h"
#include "kke/Material.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"
#include "kke/Buffer.h"
#include "kke/TetMeshAsset.h"

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <string>

#if KKE_ENABLE_FEMFX

// Needed for real, not forward-declarable: unlike FmScene/FmRigidBody
// (opaque pointers below, forward declaration is enough), FmVector3/
// FmTetVertIds/FmArray/FmTetMesh are held as direct members of
// SpawnedTet further down, which requires their full definition to be
// visible here, not just a name.
#include <AMD_FEMFX.h>

namespace AMD {
struct FmScene;
}

namespace kke {

// Wraps AMD FEMFX's FmScene lifecycle as a real kke::Module — see
// README "Physics: AMD FEMFX integration" for what FEMFX is, why it was
// chosen, and the ~30 fixes that were needed just to get it building on
// Linux at all before this module could exist.
//
// GENERAL SPAWN API — this is the actual point of this class now, not
// just "one hardcoded falling tetrahedron": call spawnTetMesh() at any
// time after init() has run (including at runtime, from ImGui — see
// renderUi() for a live, working example of exactly that) to add
// another object with its own shape, position, and kke::Material. As
// of the CGAL content-pipeline work (see README "Content pipeline:
// CGAL tetrahedralization"), "its own shape" is genuinely arbitrary —
// spawnTetMesh() takes a kke::TetMeshData loaded from any
// kke_tetrahedralizer output file, not just the one hardcoded
// tetrahedron. spawnTetrahedron() still exists as a thin convenience
// wrapper (builds a 4-vert/1-tet TetMeshData and calls spawnTetMesh())
// — kept specifically because it's the same call the demo's starting
// object and every earlier verification in this codebase already
// exercised; reimplementing it on top of the general path rather than
// deleting it means that whole verification history still covers the
// general path too, not just a special case sitting next to it.
// Each spawn call returns an ObjectHandle; removeObject() takes it
// back out of the scene and frees its FEMFX resources.
//
// HONEST CURRENT LIMITS, not hidden:
//   - The scene is sized for a small, fixed number of objects
//     (kMaxObjects below), not stress-test scale — this engine has
//     never been tested with more than a handful of physics objects,
//     and the task system is still a synchronous single-thread stand-
//     in (see the class's own notes on that further down). A real
//     stress-test demo is separately planned, not assumed to work here.
//   - Every spawned object's tets are independent, disconnected
//     tetrahedra as far as FEMFX's fracture/plasticity model is
//     concerned unless the mesh's own connectivity says otherwise —
//     spawnTetMesh() computes real shared-vertex connectivity from
//     whatever TetMeshData it's given (see spawnTetMesh()'s own
//     comment), so a real multi-tet imported mesh behaves as one
//     connected deformable body, not a pile of separate tets that
//     happen to share vertex positions.
//   - No render-mesh-to-tetrahedra *skinning* bridge — each object's
//     own simulated tet vertices are directly what gets rendered, with
//     no separate higher-resolution render surface. FEMFX's own
//     `RenderTetAssignment` sample is what real skinning (a detailed
//     render mesh draped over a coarser simulation tet mesh) looks
//     like — worth reading before building that; a CGAL-tetrahedralized
//     mesh at reasonable quality settings is usually detailed enough
//     to render directly, but a real skinning bridge is what a
//     modeler-authored high-poly character would need.
//   - kke_tetrahedralizer (the CGAL-based offline tool that produces
//     the files spawnTetMesh() loads) currently only accepts OFF input
//     and only the direct-CDT "already watertight" pipeline — no
//     OBJ/FBX/glTF import and no voxel-grid robustness path for messy
//     non-manifold input yet. See the tool's own header comment and
//     the README's "Content pipeline" section.
//
// THE FULL, HONEST DEBUGGING ACCOUNT from getting the first object
// working at all — four distinct real issues found via gdb and direct
// testing, three fixed, one genuinely open but non-blocking:
//
// 1. FIXED: missing `FmInitConnectivity()` call. FEMFX's own header
//    walkthrough jumps straight from building `vertIncidentTets`
//    arrays to `FmFinishConnectivityFromVertIncidentTets()` without
//    showing this call — following that literally left the mesh's
//    sparse stiffness-matrix row structure never built. Confirmed via
//    gdb: crashed in `FmAddRowSubmatrices` with `rowSize=0`.
// 2. FIXED (real, but not the direct cause of #3 below): a SIMD ABI
//    mismatch. `femfx` is compiled with `-mavx2 -mfma`; `kke_engine`
//    (which compiles this file, inlining many of FEMFX's own AVX2/FMA
//    header functions directly) wasn't. Fixed by matching the compile
//    options exactly (`engine/CMakeLists.txt`).
// 3. FIXED — this was the real one: calling
//    `FmFinishConnectivityFromVertIncidentTets()` a SECOND time after
//    `FmInitConnectivity()`, not realizing the latter already calls
//    the former internally, at its own end. Calling it twice doubled
//    `tetMesh->numExteriorFaces` (4 real faces counted as 8), which
//    crashed `AMD::FmBuildHierarchy`'s BVH-rebuild code writing past
//    the end of a `nodes` array correctly sized for 4.
// 4. OPEN, non-blocking: an object falls correctly but settles at a
//    small positive height above whatever it lands on (~0.002, a
//    small collision-contact gap) rather than exactly at the contact
//    surface — cosmetic, not a stability or correctness problem.
//
// FEMFX requires two things the application must provide, implemented
// in PhysicsModule.cpp rather than reused from FEMFX's own ~1,800-line
// threaded sample task system:
//   - A task system callback interface (FEMFX hardcodes
//     FM_ASYNC_THREADING=1, so even a single-threaded scene needs real
//     callbacks — there's no "just run synchronously" built-in mode).
//     What's implemented here is a genuinely synchronous adapter:
//     "submit a task" means "call it immediately, on the calling
//     thread." This is honestly not real multithreading — parallelizing
//     this is real future work — but it is a fully valid, fully
//     verified implementation of the required interface.
//   - FmAlignedMalloc/FmAlignedFree — an allocator hook FEMFX declares
//     extern and expects the application to define. Implemented via
//     std::aligned_alloc.
class PhysicsModule : public Module {
public:
    using ObjectHandle = uint32_t;
    static constexpr ObjectHandle kInvalidHandle = 0;

    // renderScale defaults to 0.02 — the value tuned specifically for
    // the generic kke_demo_game's small-scale camera (see the .cpp's
    // own comment on kRenderScale for the full story of why that
    // number exists at all). A demo whose camera is actually suited to
    // real-world physics scale (a 100-unit floor, gravity=9.88) should
    // pass 1.0 here rather than force its camera to fight this scale
    // hack — that's the whole reason this is a constructor parameter
    // now instead of a fixed constant.
    //
    // initialObjectCount spawns that many tetrahedra at init(), spread
    // out at varied positions/heights rather than stacked on top of
    // each other, instead of the original single hardcoded object —
    // for a demo where "physics is visibly happening" matters more
    // than needing someone to click the spawn button first.
    explicit PhysicsModule(float renderScale = 0.02f, int initialObjectCount = 1)
        : m_renderScale(renderScale), m_initialObjectCount(initialObjectCount) {}

    const char* name() const override { return "Physics"; }

    void init(Application& app) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    // The general path (see the class comment). `mesh` must have at
    // least one vertex and one tet, and every tet's 4 indices must be
    // valid vertex indices — exactly what kke::loadTetMeshFromFile()
    // itself already guarantees for a file loaded through it, but this
    // function re-validates anyway since it's callable directly with
    // hand-built data too, not just loaded files. Returns
    // kInvalidHandle if the scene is already at kMaxObjects or if any
    // setup step fails — always check before assuming the object
    // exists.
    ObjectHandle spawnTetMesh(const TetMeshData& mesh, const glm::vec3& position, const Material& material);

    // Convenience wrapper: builds a 4-vert/1-tet TetMeshData for the
    // same single tetrahedron shape used throughout this class's own
    // verification history, and calls spawnTetMesh() with it — see
    // the class comment for why this is implemented on top of the
    // general path rather than kept as separate, parallel code.
    ObjectHandle spawnTetrahedron(const glm::vec3& position, const Material& material);

    // Removes a previously spawned object. Safe to call with
    // kInvalidHandle or a handle that's already been removed — both
    // are no-ops, not errors, matching this codebase's general
    // "double-remove is fine" convention elsewhere (e.g. module
    // shutdown ordering).
    void removeObject(ObjectHandle handle);

    size_t objectCount() const { return m_objects.size(); }

private:
    // One spawned tetrahedron's full FEMFX + render state. A plain
    // struct, not a class with its own methods — PhysicsModule owns
    // the behavior, this just owns the data, and it needs a genuinely
    // stable address for as long as it's alive: FEMFX retains pointers
    // into some of these member arrays beyond the setup calls that
    // take them (see the class comment's debugging account, point 3,
    // and how m_restPositions/m_tetVertIds/m_vertIncidentTets used to
    // be locals before that exact bug was found) — so SpawnedTet
    // instances are heap-allocated individually (std::unique_ptr) and
    // never moved or copied once created, even as the surrounding
    // container grows.
    struct SpawnedTet {
        AMD::FmTetMesh* tetMesh = nullptr;               // owned by tetMeshBuffer, not separately
        AMD::FmTetMeshBuffer* tetMeshBuffer = nullptr;
        uint sceneBufferId = 0;                          // needed to remove it from the scene later
        Material material;
        std::unique_ptr<Buffer> vertexBuffer;            // re-uploaded every frame — this object moves/deforms
        std::unique_ptr<Buffer> indexBuffer;             // static per-object now — different objects can have different topology
        uint32_t numVerts = 0;
        uint32_t numTets = 0;

        // Kept alive for this object's whole lifetime — see the struct
        // comment above for why. std::vector rather than fixed-size
        // arrays now that a spawned object's vertex/tet count is
        // genuinely variable, not always exactly 4/1 — safe for the
        // same reason fixed arrays were safe: SpawnedTet itself is
        // heap-allocated (std::unique_ptr, see m_objects below) and
        // never moved once created, so these vectors' own internal
        // buffers are just as stable as fixed member arrays were,
        // provided nothing resizes them after spawnTetMesh() finishes
        // building them once — which is exactly how they're used.
        std::vector<AMD::FmVector3> restPositions;
        std::vector<AMD::FmTetVertIds> tetVertIds;
        std::vector<AMD::FmArray<uint>> vertIncidentTets;
    };

    // A small, fixed cap, not a stress-test number — see the class
    // comment's "honest current limits." Raising this later just
    // means raising the FmSceneSetupParams fields in init() to match;
    // nothing else about the design changes.
    static constexpr uint32_t kMaxObjects = 8;

    AMD::FmScene* m_scene = nullptr;
    float m_renderScale; // see the constructor's doc comment
    int m_initialObjectCount; // see the constructor's doc comment
    AMD::FmRigidBody* m_ground = nullptr; // static kinematic ground plane, top surface at y=0
    Application* m_app = nullptr;         // needed by spawnTetrahedron() if called after init(), e.g. from renderUi()

    std::unordered_map<ObjectHandle, std::unique_ptr<SpawnedTet>> m_objects;
    ObjectHandle m_nextHandle = 1; // 0 is kInvalidHandle

    // Render bridge — this is genuinely the simplest possible version,
    // not a real render-mesh-to-tetrahedra skinning system: each
    // object's own simulated tet vertices are directly what gets
    // rendered, with no separate render-resolution surface to skin
    // onto. FEMFX's own `RenderTetAssignment` sample is what a real
    // bridge (arbitrary detailed render mesh skinned onto a coarser
    // tet mesh) looks like — worth reading before building that. The
    // pipeline is shared across every object (same shaders regardless
    // of shape); vertex and index buffers are per-object now, stored
    // on SpawnedTet above, since different spawned objects can
    // genuinely have different vertex/tet counts.
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Mesh> m_groundMesh;

    // renderUi() state — a spawn button needs *something* to vary
    // between clicks, or every spawned object would land in an
    // identical stack. Deliberately simple (position jitter only, one
    // fixed "wood-ish" material) — a real per-material picker is
    // future work once there's more than one material worth choosing
    // between in a demo.
    float m_nextSpawnHeight = 5.0f;
};

} // namespace kke

#endif // KKE_ENABLE_FEMFX

