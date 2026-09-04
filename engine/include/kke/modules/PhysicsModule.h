#pragma once

#include "kke/Module.h"
#include "kke/Material.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"
#include "kke/Buffer.h"

#include <glm/glm.hpp>
#include <memory>
#include <unordered_map>
#include <cstdint>

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
// just "one hardcoded falling tetrahedron": call spawnTetrahedron() at
// any time after init() has run (including at runtime, from ImGui —
// see renderUi() for a live, working example of exactly that) to add
// another object with its own position and kke::Material. Each
// returns an ObjectHandle; removeObject() takes it back out of the
// scene and frees its FEMFX resources. The demo's own single falling
// tetrahedron is created by init() calling this same public method,
// not through separate one-off code — the API is dogfooded, not just
// declared.
//
// HONEST CURRENT LIMITS, not hidden:
//   - Every spawned object is the same fixed single-tetrahedron shape
//     (4 verts, 1 tet, no fracture) — there is no general mesh import
//     or tetrahedralization yet (see README "Physics: AMD FEMFX
//     integration" and the Roadmap's TetGen entry), so "spawn" means
//     "spawn this one shape with your choice of position and
//     material," not "spawn any mesh."
//   - The scene is sized for a small, fixed number of objects
//     (kMaxObjects below), not stress-test scale — this engine has
//     never been tested with more than a handful of physics objects,
//     and the task system is still a synchronous single-thread stand-
//     in (see the class's own notes on that further down). A real
//     stress-test demo is separately planned, not assumed to work here.
//   - No render-mesh-to-tetrahedra skinning bridge for anything beyond
//     this one shape — see the render bridge section further down.
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

    const char* name() const override { return "Physics"; }

    void init(Application& app) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void render(const RenderContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    // Adds one tetrahedron-shaped object to the scene at `position`
    // (world/physics units — see kRenderScale in the .cpp for how this
    // relates to what's actually drawn), with material properties from
    // `material` mapped onto FEMFX's FmTetMaterialParams. Returns
    // kInvalidHandle if the scene is already at kMaxObjects or if setup
    // fails for any other reason — always check before assuming the
    // object exists.
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

        // Kept alive for this object's whole lifetime — see the struct
        // comment above for why.
        AMD::FmVector3 restPositions[4];
        AMD::FmTetVertIds tetVertIds[1];
        AMD::FmArray<uint> vertIncidentTets[4];
    };

    // A small, fixed cap, not a stress-test number — see the class
    // comment's "honest current limits." Raising this later just
    // means raising the FmSceneSetupParams fields in init() to match;
    // nothing else about the design changes.
    static constexpr uint32_t kMaxObjects = 8;

    AMD::FmScene* m_scene = nullptr;
    AMD::FmRigidBody* m_ground = nullptr; // static kinematic ground plane, top surface at y=0
    Application* m_app = nullptr;         // needed by spawnTetrahedron() if called after init(), e.g. from renderUi()

    std::unordered_map<ObjectHandle, std::unique_ptr<SpawnedTet>> m_objects;
    ObjectHandle m_nextHandle = 1; // 0 is kInvalidHandle

    // Render bridge — this is genuinely the simplest possible version,
    // not a real render-mesh-to-tetrahedra skinning system: each
    // object has exactly one tetrahedron, so its 4 simulated vertices
    // *are* the render mesh, with no separate render-resolution
    // surface to skin onto. FEMFX's own `RenderTetAssignment` sample
    // is what a real bridge (arbitrary render mesh skinned onto many
    // tets) looks like — worth reading before generalizing this past
    // one shape. Shared across every spawned object, since they're all
    // the same shape: one pipeline, one static index buffer. Only the
    // per-object vertex buffer (in SpawnedTet above) actually differs.
    std::unique_ptr<Pipeline> m_pipeline;
    std::unique_ptr<Mesh> m_groundMesh;
    std::unique_ptr<Buffer> m_tetIndexBuffer;

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

