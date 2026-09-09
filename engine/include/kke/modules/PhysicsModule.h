#pragma once

#include "kke/Module.h"
#include "kke/Material.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"
#include "kke/Buffer.h"
#include "kke/Texture.h"
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
    void renderShadow(const ShadowRenderContext& ctx) override;
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

    // Same as spawnTetMesh(), but with real fracture enabled — the
    // object can genuinely break apart into multiple independently-
    // moving pieces under enough stress. Kept as a SEPARATE method
    // rather than adding a parameter to spawnTetMesh() itself: enabling
    // fracture requires real, extra per-tet setup (FmFractureGroupCounts,
    // tetFractureGroupIds — see spawnTetMesh()'s own implementation
    // comment for the full account of what FEMFX actually needs here,
    // confirmed against AMD's own vendored sample code, not guessed),
    // and fracturable objects take a genuinely more expensive render
    // path (rebuilding vertex/index data every frame instead of once —
    // see SpawnedTet's own comment on why) — a real, meaningful
    // distinction worth keeping visible at the call site, not hidden
    // behind a boolean default parameter.
    ObjectHandle spawnFracturableTetMesh(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                          const glm::vec3& initialVelocity = glm::vec3(0.0f));

    // Same as spawnTetMesh(), but with real plasticity enabled — the
    // object can genuinely dent and stay dented (permanent deformation)
    // rather than always springing back to its original rest shape.
    // Structurally simpler than fracture to enable — confirmed by
    // reading FmComputeTetMeshBufferBounds's own signature, which has
    // real, separate output parameters for fracture bookkeeping
    // (FmFractureGroupCounts, tetFractureGroupIds) but nothing
    // plasticity-specific at all — just the enablePlasticity flag plus
    // real (non-default-zero) plasticYieldThreshold/plasticCreep
    // material values, which already flow through FmInitTetState's own
    // per-tet loop correctly. No render-path changes needed either:
    // unlike fracture, a plastic object never splits into new
    // FmTetMesh pieces, so the existing single-mesh render path
    // already handles it.
    ObjectHandle spawnPlasticTetMesh(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                      const glm::vec3& initialVelocity = glm::vec3(0.0f));

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

    // The material a plain "Spawn tetrahedron" click uses — real,
    // settable state (mirroring how Application::lighting() already
    // works), not a per-button hardcoded value. Built specifically so
    // an external UI (see kke::MaterialGridModule) can change what
    // gets spawned next, the same way its own lighting presets already
    // change real Application::lighting() state. Deliberately NOT used
    // by "Spawn fracturable cube" — that button's own material has a
    // specifically, empirically tuned fracture threshold (see
    // renderUi()'s own comment on how that value was found) that an
    // arbitrary preset swapped in here could quietly break.
    Material& selectedMaterial() { return m_selectedMaterial; }

private:
    // A real, general box-mesh generator — the same 8-corner, 6-tet
    // diagonal decomposition already proven for the single-cell cube
    // and the 2x2x2 "Spawn fracturable cube" grid, generalized to any
    // cell count and any physical dimensions per axis. What makes the
    // new scene-specific shapes (a thin glass sheet, an elongated
    // brick, a car-like block, a wall) all possible from one function
    // instead of four separately hand-written ones: a thin sheet is
    // just this with a small Y cell count and a small sizeY; a brick
    // is this with roughly 2:1:1 proportions; a wall is this scaled
    // wide and tall but thin. cellsX/Y/Z are the *tet* resolution
    // (more cells = more possible fracture pieces, at real simulation
    // cost — see the "Spawn fracturable cube" button's own comment on
    // why cell count matters for how convincing fracture looks), while
    // sizeX/Y/Z are the actual physical dimensions in world units.
    static TetMeshData buildGridBox(int cellsX, int cellsY, int cellsZ, float sizeX, float sizeY, float sizeZ);

    // Shared implementation behind both spawnTetMesh() (enableFracture
    // always false) and spawnFracturableTetMesh() (always true) — see
    // that method's own header comment for why fracture support needed
    // a real, separate entry point rather than a bool parameter on the
    // existing public method.
    ObjectHandle spawnTetMeshInternal(const TetMeshData& mesh, const glm::vec3& position, const Material& material, bool enableFracture,
                                       const glm::vec3& initialVelocity = glm::vec3(0.0f), bool enablePlasticity = false);

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

        // Real fracture support — see spawnTetMesh()'s own comment for
        // the full account of what this needed. When true, render()
        // takes a genuinely different, more expensive path: fracture
        // can split one object into multiple independently-moving
        // FmTetMesh pieces at runtime (FmGetNumTetMeshes() can grow
        // past 1), and FEMFX's own setup docs say vertex count itself
        // "may grow with fracture" (new vertices duplicated along
        // fracture seams) — so a fracturable object's vertex/index
        // buffers are sized to bounds.maxVerts/maxTets (the reserved
        // capacity), not the object's initial spawn-time counts, and
        // both get rebuilt from each current sub-mesh's actual
        // topology every frame rather than uploaded once and reused.
        bool fracturable = false;
        bool plastic = false; // see spawnPlasticTetMesh()'s own comment
        uint32_t maxVerts = 0;
        uint32_t maxTets = 0;

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
        // Fracture-specific, only populated/used when fracturable —
        // same lifetime reasoning as the arrays above.
        std::vector<AMD::FmFractureGroupCounts> fractureGroupCounts;
        std::vector<uint> tetFractureGroupIds;
    };

    // Raised from the original 8 to a genuinely meaningful showcase
    // number, not a stress-test number either — see the class
    // comment's "honest current limits." AMD's own reference demo
    // scenes (see external/FEMFX/samples/common/TestScenes.cpp,
    // vendored alongside the library itself) show piles of dozens of
    // soft-body objects at once (BLOCKS_SCENE, DUCKS_SCENE), not a
    // handful — 8 was never meant to represent a real ceiling, just
    // the smallest number that proved the spawn API worked at all.
    // Raising this further just means raising the FmSceneSetupParams
    // fields in init() to match; nothing else about the design
    // changes.
    static constexpr uint32_t kMaxObjects = 64;

    AMD::FmScene* m_scene = nullptr;

    // A small material texture library — five real, distinct
    // procedurally-generated textures, one per kke::MaterialGridModule
    // preset (Wood/Stone/Iron/Rubber/Glass, matching their own
    // material.textureId — see Material.h's own comment on that
    // field). Each object binds the texture its own spawning
    // Material points to (see render()); an object whose material
    // doesn't set textureId falls back to the shared default white
    // texture, same as before this feature existed.
    struct MaterialTexture {
        std::unique_ptr<Texture> texture;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    };
    std::vector<MaterialTexture> m_materialTextures;
    VkDescriptorPool m_materialTexturePool = VK_NULL_HANDLE;
    VkDevice m_vkDevice = VK_NULL_HANDLE; // stashed for shutdown(), which takes no Application& — see CubeModule's own identical comment
    Material m_selectedMaterial; // see selectedMaterial()'s own doc comment above
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
    // Real shadow casting for spawned objects — the ground plane is
    // deliberately excluded (see renderShadow()'s own comment): it's
    // a receiver, not a caster, and casting its own shadow onto itself
    // would be meaningless. Same minimal shadow.vert/frag pipeline
    // pattern already proven in CubeModule, just a second, independent
    // instance of it here.
    std::unique_ptr<Pipeline> m_shadowPipeline;
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

