#pragma once

#include "kke/Module.h"
#include "kke/Material.h"
#include "kke/Pipeline.h"
#include "kke/Mesh.h"
#include "kke/Buffer.h"
#include "kke/Texture.h"
#include "kke/TetMeshAsset.h"
#include "kke/VoxelTets.h"
#include "kke/Renderer.h"
#include "kke/Capabilities.h"
#include "kke/Ragdoll.h"
#include "kke/BreakGraph.h"
#include "kke/InteriorColor.h"
#include "kke/PhysicsBridge.h"

#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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
// docs/HISTORY.md "Physics: AMD FEMFX integration" for what FEMFX is, why it was
// chosen, and the ~30 fixes that were needed just to get it building on
// Linux at all before this module could exist.
//
// GENERAL SPAWN API — this is the actual point of this class now, not
// just "one hardcoded falling tetrahedron": call spawnTetMesh() at any
// time after init() has run (including at runtime, from ImGui — see
// renderUi() for a live, working example of exactly that) to add
// another object with its own shape, position, and kke::Material. "Its
// own shape" is arbitrary: any kke::TetMeshData, typically built at
// runtime from a render mesh by kke::voxelizeToTets (VoxelTets.h), or
// loaded from a .ktet.json file, not just the one hardcoded
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
//   - Rendering: by default an object's own tet surface is what gets
//     drawn. A detailed render mesh draped over the tets (a "skinning
//     bridge") is available through deformEmbedded() +
//     kke::embedTriangles, with drawOnlyCracks so only fresh crack faces
//     come from the tets (see the sandbox's breakable props).
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
class PhysicsModule : public Module, public IRagdollPhysics, public IPhysicsWorld {
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

    // Something broke this frame: a Breakable split, or a fracturable
    // object lost pieces. For sounds (AudioModule), particles, and the
    // networking layer. Cleared at the start of every frame.
    struct BreakEvent {
        glm::vec3 position{0.0f};   // world, centre of the part that broke
        Material material;
        uint32_t newPieces = 0;
        float size = 1.0f;          // m, largest extent of what broke
    };
    const std::vector<BreakEvent>& frameBreaks() const { return m_frameBreaks; }

    // Something soft-body hit something this frame hard enough to hear:
    // two FEMFX objects (FEMFX's collision report), or one landing on the
    // ground plane (which FEMFX doesn't report: found from the piece's
    // fall stopping at the floor). For AudioModule. Cleared every frame.
    struct ImpactEvent {
        glm::vec3 position{0.0f};   // world
        float speed = 0.0f;         // m/s, approach speed along the contact normal
        Material materialA;
        Material materialB;         // the other object's; for the ground, see `ground`
        bool ground = false;        // B is the floor
        uint64_t pair = 0;          // stable id of the two objects, for per-pair cooldowns
    };
    const std::vector<ImpactEvent>& frameImpacts() const { return m_frameImpacts; }

    const char* name() const override { return "Physics"; }

    void init(Application& app) override;
    void frameStart(const UpdateContext&) override {
        m_frameBreaks.clear();
        m_frameImpacts.clear();
    }
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

    // A fracturable box of cellsX*Y*Z cells (6 tets each) with the given
    // world size, centered at `center` and turned `yawDegrees` about +Y.
    // More cells = more, smaller pieces when it breaks, at more cost.
    ObjectHandle spawnFracturableBox(const glm::ivec3& cells, const glm::vec3& size, const glm::vec3& center,
                                     const Material& material, float yawDegrees = 0.0f,
                                     const glm::vec3& velocity = glm::vec3(0.0f));

    // Everything above, with every knob: fracture and/or plasticity, an
    // initial velocity, per-tet FEMFX flags (e.g. from
    // kke::fractureFlagsFromChunks() — which faces may crack), and
    // whether to draw the whole tet surface or only fresh crack faces
    // (for objects whose real look is a render mesh glued on with
    // kke::embedPoints — see deformEmbedded()).
    struct TetSpawnOptions {
        bool fracture = false;
        bool plastic = false;
        glm::vec3 velocity{0.0f};
        std::vector<uint16_t> tetFlags;   // empty, or one FM_TET_FLAG_* set per tet
        bool drawOnlyCracks = false;
        // "Settle, then arm": spawn unbreakable; once the object has come
        // to rest (asleep, or this many seconds at most — never sooner than
        // 0.75 s), each tet's threshold becomes the material threshold PLUS
        // 1.25x the peak stress that tet carried while settling. Only
        // stress *added* by an impact breaks it, so a prop never collapses
        // under its own weight whatever its size (BUG-043: FEMFX's resting
        // stress on a 1 m crate was ~4x wood's threshold). 0 = off: the
        // plain material threshold applies from the first step.
        float armFractureAfterSeconds = 0.0f;
        // Optional look for the tet surface / crack faces: an image file
        // and one UV per TetMeshData vertex (e.g. taken from the nearest
        // vertex of the prop's own mesh, so a blue crate is blue inside).
        // Empty = the material's procedural texture with box-projected UVs.
        std::string texturePath;
        std::vector<glm::vec2> vertexUVs;
        // Optional per-tet multiplier on the fracture threshold (e.g.
        // kke::VoronoiCut::tetStrength: cracks inside a cluster tougher
        // than cracks between clusters). Empty = 1 everywhere.
        std::vector<float> tetStrength;
        // Pre-baked pieces (kke::VoronoiCut::chunkOfTet): with this set
        // (and fracture on) the object breaks KKE's way instead of by
        // FEMFX's own fracture - see Breakable below. tetFlags is ignored.
        std::vector<uint32_t> chunkOfTet;
        // Colour of the insides (fresh crack faces): see kke/InteriorColor.h.
        // Not set but texturePath is: worked out here from the texture as
        // vertexUVs sample it. Neither: the surface look, darkened.
        InteriorFill interior;
    };
    ObjectHandle spawnTetMeshWithOptions(const TetMeshData& mesh, const glm::vec3& position, const Material& material,
                                         const TetSpawnOptions& options);

    // World-space positions and normals of embedded points (see
    // kke::TetEmbedding; tet indices refer to the TetMeshData the object
    // was spawned from), following deformation and fracture. `restNormals`
    // are the points' normals in the same space as that TetMeshData.
    // Normals are carried by each tet's deformation (inverse-transpose),
    // so dents and bends shade correctly. Returns false if the handle is
    // unknown. Cost: one small matrix per used tet + one per point.
    bool deformEmbedded(ObjectHandle handle, const TetEmbedding& embedding, const std::vector<glm::vec3>& restNormals,
                        std::vector<glm::vec3>& outPositions, std::vector<glm::vec3>& outNormals) const;
    // True once every piece of the object is asleep (nothing moves: a
    // caller can skip recomputing anything derived from it).
    bool isObjectAsleep(ObjectHandle handle) const;
    // Number of separate pieces the object has broken into (1 = intact).
    uint32_t pieceCount(ObjectHandle handle) const;

    // Removes a previously spawned object. Safe to call with
    // kInvalidHandle or a handle that's already been removed — both
    // are no-ops, not errors, matching this codebase's general
    // "double-remove is fine" convention elsewhere (e.g. module
    // shutdown ordering).
    void removeObject(ObjectHandle handle);

    size_t objectCount() const { return m_objects.size(); }
    // FEMFX step time, averaged over the last second (0 until then).
    double lastStepMsAvg() const { return m_lastStepMsAvg; }

    // The purpose-built demo scenes, callable from code rather than only
    // from renderUi()'s buttons — the scripted benchmark below uses
    // these, and so will any future scripting layer (Lua) that wants to
    // set up a scene without clicking through ImGui.
    enum class Scene { GlassSheet, Brick, RubberBall, CarCrash, LavaMelt, FracturableCube, PlasticCube, BreakTest };
    void spawnScene(Scene scene);
    // Builds a w x h x d box of tets, bakes a fracture pattern into it
    // (kke::bakeFracture, `pattern` = FracturePattern) and spawns it as a
    // breakable: the brick and glass scenes, and any game's breakable
    // walls/panes. armSeconds > 0: settle, then arm (see TetSpawnOptions).
    ObjectHandle spawnPatternedBox(const glm::ivec3& cells, const glm::vec3& size, const glm::vec3& position, const Material& material,
                                   int pattern, float chunkSize, int cellsPerCluster, const glm::vec3& velocity, float armSeconds = 0.0f,
                                   const glm::vec3* impactPoint = nullptr, uint32_t seed = 0);
    // (`seed` 0 = from the world seed and the handle, as described below;
    // else exactly this one: a network client rebuilding the host's.)

    // ---- Multiplayer (kke::NetModule, docs/NETWORKING.md "Breakables")
    // The host breaks a breakable from its stresses and sends which
    // borders broke; a client's copy is a "follower": it never breaks on
    // its own, only along the borders applyBrokenBorders() is given, so
    // both end up with the same pieces (the debris then flies its own way
    // on each machine: FEMFX isn't deterministic across machines).
    uint32_t breakableSeed(ObjectHandle handle) const;    // the fracture seed it was baked with; 0 = not a breakable
    size_t brokenBorderCount(ObjectHandle handle) const;  // cheap: poll it, fetch brokenBorders() when it changes
    std::vector<std::pair<uint32_t, uint32_t>> brokenBorders(ObjectHandle handle) const; // (piece, piece), see kke::BreakGraph
    void setBreakableFollower(ObjectHandle handle, bool follow);
    // Breaks these borders now, splitting every part that came apart.
    // Pairs that aren't borders of this object are ignored. Returns how
    // many borders were newly broken.
    size_t applyBrokenBorders(ObjectHandle handle, const std::vector<std::pair<uint32_t, uint32_t>>& borders);

    // The world's fracture seed (see kke::fractureSeed): every breakable
    // this module builds mixes it with its own handle, so each object
    // breaks its own way, and the same world breaks the same way on
    // every run (and, later, on every client of a multiplayer game).
    uint32_t fractureWorldSeed() const { return m_fractureWorldSeed; }
    // Debris budget (docs/OPTIMIZATION.md rule 5): at most this many pieces of
    // broken breakables at once. Past it the oldest *sleeping* piece is
    // removed (then the oldest piece). FEMFX costs ~0.15-0.2 ms per awake
    // body on one core (tools/physics_lab volcano), so this is the knob
    // that keeps a big break inside the frame. 0 = unlimited.
    void setDebrisBudget(uint32_t maxPieces) { m_debrisBudget = maxPieces; }
    uint32_t debrisBudget() const { return m_debrisBudget; }
    void setFractureWorldSeed(uint32_t seed) { m_fractureWorldSeed = seed; }

    // ---- IRagdollPhysics (see kke/Capabilities.h, kke/Ragdoll.h)
    // Bodies are FEMFX rigid boxes joined by glue (ball) constraints, with
    // hinge constraints where RagdollJoint::hinge is set. They collide with
    // the floor and with FEMFX deformable/fracturable objects, but not with
    // each other: FEMFX's built-in rigid body solver doesn't do rigid-vs-
    // rigid contacts (AMD's own samples disable them the same way). No
    // cone/twist joint limits exist in FEMFX, so limbs can over-rotate.
    RagdollHandle createRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity) override;
    void destroyRagdoll(RagdollHandle handle) override;
    bool ragdollBodyTransforms(RagdollHandle handle, std::vector<glm::mat4>& out) const override;
    void pushRagdollBody(RagdollHandle handle, int body, const glm::vec3& deltaVelocity) override;

    // ---- The other physics world (kke/PhysicsBridge.h, PhysicsBridgeModule)
    // Boxes FEMFX objects collide with from the next step on, replacing
    // the last set (proxies not listed again are removed). They're
    // kinematic in FEMFX: pieces bounce off them but can't move them;
    // that way round goes through externalImpulses(). Keep the list to
    // what's near moving pieces: FEMFX treats rigid bodies as always
    // awake, so a proxy touching a sleeping piece keeps waking it.
    void setExternalBoxes(const std::vector<BridgeBox>& boxes);
    size_t externalBoxCount() const { return m_external.size(); }
    struct ExternalImpulse {
        uint64_t key = 0;
        glm::vec3 impulse{0.0f}; // N s, world
        glm::vec3 point{0.0f};   // where it acts
    };
    // What the last step's contacts gave each movable external box.
    const std::vector<ExternalImpulse>& externalImpulses() const { return m_externalImpulses; }
    // World bounds of every piece of every object; `awakeOnly` skips the
    // sleeping ones.
    void pieceBounds(std::vector<std::pair<glm::vec3, glm::vec3>>& out, bool awakeOnly) const;

    // ---- Rubble handoff (issue #31, PhysicsBridgeModule, docs/PHYSICS_BRIDGE.md)
    // Small pieces broken off a breakable leave FEMFX and become rigid
    // bodies in the other world: FEMFX costs ~0.15-0.2 ms per awake piece
    // a step, Jolt a few microseconds per body. A piece that has left
    // stays drawn here (and its breakable's embedded render points keep
    // following it), wherever setRubbleTransform() puts it; it no longer
    // deforms or breaks.
    struct RubblePiece {
        ObjectHandle handle = kInvalidHandle;
        glm::vec3 center{0.0f};              // world, the mass centre
        glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
        float mass = 0.0f;                   // kg
        std::vector<glm::vec3> hull;         // the piece's vertices, relative to center
        Material material;
        float size = 0.0f;                   // m, largest extent
    };
    // Awake pieces of broken breakables that are done splitting (their
    // break has played out) and either small (at most maxSize m across
    // and maxTets tets) or a single baked chunk that can't break any
    // further, up to maxChunkSize m across. Appended.
    void rubbleCandidates(float maxSize, uint32_t maxTets, float maxChunkSize, std::vector<ObjectHandle>& out) const;
    // The piece as a rigid body right now; false if it isn't a candidate.
    bool describeRubble(ObjectHandle piece, RubblePiece& out) const;
    // Takes the piece out of the FEMFX simulation. Its frame from now on:
    // origin at RubblePiece::center, axes the world's (a body created at
    // center with no rotation matches). False (nothing changed) if it
    // isn't a candidate.
    bool convertToRubble(ObjectHandle piece);
    void setRubbleTransform(ObjectHandle piece, const glm::vec3& position, const glm::quat& rotation);
    void removeRubble(ObjectHandle piece);
    bool isRubble(ObjectHandle piece) const { return m_rubble.count(piece) != 0; }
    size_t rubbleCount() const { return m_rubble.size(); }

    // ---- IPhysicsWorld (kke/PhysicsWorld.h). Rubble isn't counted or hit
    // here: it belongs to the rigid-body world now.
    const char* physicsEngineName() const override { return "FEMFX"; }
    Stats physicsStats() const override;
    Hit physicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const override;
    size_t physicsBlast(const glm::vec3& center, float radius, float speed) override;
    void physicsBoundsInBox(const glm::vec3& min, const glm::vec3& max, std::vector<std::pair<glm::vec3, glm::vec3>>& out) const override;

    // Procedural tet meshes, centered on the origin: a box of cells (6
    // tets each) and a "spherified cube" ball. Public so games can spawn
    // their own shapes (projectiles, crates) through spawn*TetMesh().
    static TetMeshData buildGridBox(int cellsX, int cellsY, int cellsZ, float sizeX, float sizeY, float sizeZ);
    static TetMeshData buildSphere(int cells, float radius);

    // The visual ground slab this module draws at y=0. Turn it off when
    // the game draws its own floor (it would z-fight).
    void setDrawGround(bool draw) { m_drawGround = draw; }
    // Draw each ragdoll rigid body as a box (physics debug view).
    void setShowRagdollBodies(bool show) { m_showRagdollBodies = show; }

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
    // Rebuilds each awake object's exterior-face vertices and uploads
    // them into this frame's buffer slot. Runs once per frame, from
    // whichever of renderShadow()/render() comes first.
    void prepareRenderData(uint32_t frameIndex);
    uint64_t m_frameCounter = 0;       // bumped at the end of render()
    uint64_t m_preparedFrame = ~0ull;  // m_frameCounter value prepareRenderData() last ran for
    uint32_t m_awakeObjects = 0;       // stats, from the last prepareRenderData()
    uint32_t m_renderedFaces = 0;
    uint32_t m_totalPieces = 0;
    uint32_t m_awakePieces = 0;
    uint32_t m_lastWarningFlags = 0;
    double m_pendingPrepMs = 0.0;      // prepareRenderData() time, folded into the frame's render-prep stat   // FEMFX FM_WARNING_FLAG_* seen in the last second, see fixedUpdate()


    // Shared implementation behind both spawnTetMesh() (enableFracture
    // always false) and spawnFracturableTetMesh() (always true) — see
    // that method's own header comment for why fracture support needed
    // a real, separate entry point rather than a bool parameter on the
    // existing public method.
    ObjectHandle spawnTetMeshInternal(const TetMeshData& mesh, const glm::vec3& position, const Material& material, bool enableFracture,
                                       const glm::vec3& initialVelocity = glm::vec3(0.0f), bool enablePlasticity = false,
                                       const std::vector<uint16_t>* tetFlags = nullptr, bool drawOnlyCracks = false,
                                       float armFractureAfterSeconds = 0.0f);

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
        uint32_t sceneBufferId = 0;                          // needed to remove it from the scene later
        Material material;
        // Render data. Only the *exterior* faces of the tet mesh are
        // drawn — interior faces are shared by two tets and can never be
        // seen, and they were most of the triangles (a 6x2x6 glass sheet
        // has 432 tets = 1,728 faces, of which ~150 are on the outside).
        // Fracture turns interior faces into new exterior ones, and
        // FEMFX's own exterior-face list tracks that, so crack surfaces
        // still show up. Flat-shaded, non-indexed: 3 unique vertices per
        // face (see BUGS.md BUG-006 for why vertices aren't shared).
        //
        // cpuVerts is rebuilt only while some piece of the object is
        // awake; once FEMFX puts every piece to sleep, the last build is
        // reused as-is. One GPU buffer per frame in flight, so writing
        // this frame's copy never races the GPU reading last frame's.
        std::vector<Vertex> cpuVerts;
        uint64_t cpuVersion = 0;
        std::unique_ptr<Buffer> vertexBuffers[Renderer::kMaxFramesInFlight];
        uint64_t gpuVersion[Renderer::kMaxFramesInFlight] = {};
        uint32_t maxRenderVerts = 0;  // capacity of each vertexBuffers[] entry
        bool asleep = false;          // every piece asleep as of the last prepareRenderData()
        glm::vec3 color{1.0f};
        uint32_t numVerts = 0;
        uint32_t numTets = 0;

        // Real fracture support — see spawnTetMesh()'s own comment for
        // the full account of what this needed. A fracturable object can
        // split into many independently-moving FmTetMesh pieces at
        // runtime (FmGetNumTetMeshes() grows past 1); prepareRenderData()
        // walks every piece either way, so rendering no longer branches
        // on this flag.
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
        std::vector<AMD::FmArray<uint32_t>> vertIncidentTets;
        // Fracture-specific, only populated/used when fracturable —
        // same lifetime reasoning as the arrays above.
        std::vector<AMD::FmFractureGroupCounts> fractureGroupCounts;
        std::vector<uint32_t> tetFractureGroupIds;
        std::vector<uint16_t> tetFlags;                  // FM_TET_FLAG_* per tet, kept alive for FEMFX

        // Embedded render meshes (spawnTetMeshWithOptions drawOnlyCracks):
        // only faces that were *inside* the original mesh are drawn —
        // bit f of originalExterior[t] = face f of buffer tet t was on the
        // outside at spawn. restInverse[t] = inverse of the tet's rest
        // edge matrix, for deformation gradients.
        bool drawOnlyCracks = false;
        uint32_t loggedPieces = 1;                       // last piece count written to the log
        bool armPending = false;                         // see TetSpawnOptions::armFractureAfterSeconds
        uint32_t armAge = 0, armMaxTicks = 0;            // ticks since spawn, deadline
        AMD::FmTetMaterialParams armedParams{};          // the real material, applied when armed
        std::vector<float> settleStress;                 // per tet: peak stress while settling
        std::vector<float> tetStrength;                  // per tet threshold multiplier (TetSpawnOptions)
        VkDescriptorSet textureSet = VK_NULL_HANDLE;     // image file texture (TetSpawnOptions), else the material's own
        std::vector<glm::vec2> vertexUVs;                // per original vertex (optional)
        InteriorFill interior;                           // crack face colour (TetSpawnOptions::interior)
        std::vector<uint8_t> originalExterior;
        std::vector<glm::mat3> restInverse;
        // Part of a Breakable (below): which one, and the baked tet /
        // vertex each local tet / vertex is.
        ObjectHandle breakable = kInvalidHandle;
        std::vector<uint32_t> bakedTetOf, bakedVertOf;
        int breakGrace = 0;     // ticks before a freshly split part may break (see updateBreakables())
        int breakPending = -1;  // >= 0: overloaded, splits in this many ticks
        uint64_t bornTick = 0;  // for the debris budget (oldest goes first)
    };

    // An object with pre-baked pieces (TetSpawnOptions::chunkOfTet).
    //
    // Why not FEMFX's own fracture with "don't crack inside a piece" face
    // flags? Measured with tools/physics_lab: every chunked brick dropped
    // at 20 m/s blew up (pieces at FEMFX's 100 m/s failsafe, some NaN) -
    // with perfect grid tets, with or without piece-piece collisions,
    // with 4x substeps, 7x damping or 3x solver iterations. Its vertex-
    // split fracture handles single-tet shards but not big pieces.
    //
    // So KKE breaks it itself, the Chaos / Blast way: the object is a set
    // of plain FEMFX bodies ("parts"), at first one. Each step, the
    // stress in tets on a piece border is compared with that border's
    // threshold; overloaded borders break, and a part whose pieces are no
    // longer all connected is swapped for one part per connected group,
    // each starting from the old part's exact vertex positions and
    // velocities (nothing pops or stops). Only the borders that were
    // overloaded break: a corner can chip off and the rest stays whole.
    // A break is an explicit event (which borders, which step) - the
    // thing a multiplayer game sends instead of debris.
    struct Breakable {
        TetMeshData mesh;                                 // baked rest shape, spawn-local
        glm::vec3 origin{0.0f};                           // spawn position
        BreakGraph graph;                                 // pieces, borders, thresholds, broken borders
        std::vector<float> settleStress;                  // per baked tet, while arming
        std::vector<glm::vec2> vertexUVs;                 // per baked vertex (optional)
        InteriorFill interior;                            // crack face colour, copied to every part
        std::vector<ObjectHandle> parts;
        std::vector<ObjectHandle> rubble;                 // pieces handed to the rigid-body world (m_rubble)
        std::vector<ObjectHandle> partOfTet;              // baked tet -> part (or rubble)
        std::vector<uint32_t> localOfTet;                 // baked tet -> tet index in that part
        std::vector<glm::mat3> restInverse;               // per baked tet
        Material material;
        bool plastic = false, drawOnlyCracks = false;
        VkDescriptorSet textureSet = VK_NULL_HANDLE;
        glm::vec3 color{1.0f};
        bool armPending = false;
        uint32_t armAge = 0, armMaxTicks = 0;
        uint32_t breaks = 0;                              // split events so far
        uint32_t seed = 0;                                // fracture seed (spawnPatternedBox), for multiplayer
        bool follower = false;                            // see setBreakableFollower()
    };
    std::unordered_map<ObjectHandle, Breakable> m_breakables;
    // A piece handed off as rubble (convertToRubble): frozen shape, drawn
    // with a transform instead of rebuilt from FEMFX every frame.
    struct Rubble {
        ObjectHandle breakable = kInvalidHandle;
        std::vector<std::array<glm::vec3, 4>> tetCorners; // per part-local tet, relative to the handoff centre (render units)
        std::vector<glm::mat3> tetNormal;                 // per part-local tet: normal matrix at handoff
        std::vector<Vertex> verts;                        // surface, relative to the handoff centre (render units)
        std::unique_ptr<Buffer> vertexBuffer;
        Material material;
        VkDescriptorSet textureSet = VK_NULL_HANDLE;
        glm::vec3 position{0.0f};                         // physics units
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    };
    std::unordered_map<ObjectHandle, Rubble> m_rubble;
    bool isRubbleCandidate(const SpawnedTet& part, float maxSize, uint32_t maxTets, float maxChunkSize) const;
    void retireRubble(Rubble& r);
    void clearRubble();
    glm::mat4 rubbleModel(const Rubble& r) const;
    // Exterior faces of `obj` (every piece), world render units, as drawn.
    void buildSurface(const SpawnedTet& obj, std::vector<Vertex>& out, bool& truncated) const;
    // TetSpawnOptions::interior, or worked out from its texture (invalid = none).
    InteriorFill resolveInterior(const TetSpawnOptions& options, bool textured);
    ObjectHandle spawnBreakable(const TetMeshData& mesh, const glm::vec3& position, const Material& material, const TetSpawnOptions& options);
    // Spawns one part holding `tets` (baked ids). `from` = the part it
    // splits off (copies its current vertex state), or kInvalidHandle.
    ObjectHandle spawnBreakablePart(ObjectHandle breakable, const std::vector<uint32_t>& tets, ObjectHandle from, const glm::vec3& velocity);
    void updateBreakables();
    void collectImpacts(float dt);
    void splitBreakablePart(ObjectHandle breakable, ObjectHandle part);

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
    // Each piece of a broken Breakable is its own FEMFX body, so this
    // counts pieces too (a brick breaks into ~16).
    static constexpr uint32_t kMaxObjects = 512;
    // Fracture pieces across the whole scene — each one is its own FEMFX
    // tet mesh. See init()'s comment on scene capacities.
    static constexpr uint32_t kMaxScenePieces = 4096;

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
    Application* m_app = nullptr;         // needed by spawnTetrahedron() if called after init(), e.g. from renderUi()

    std::unordered_map<ObjectHandle, std::unique_ptr<SpawnedTet>> m_objects;
    std::vector<BreakEvent> m_frameBreaks;
    std::vector<ImpactEvent> m_frameImpacts;
    static constexpr uint32_t kMaxReportedContacts = 256;
    std::vector<AMD::FmCollisionReportDistanceContact> m_contactReport; // FEMFX writes into it during FmUpdateScene
    struct PieceFall { float y = 0.0f, vy = 0.0f; uint64_t tick = 0; };
    std::unordered_map<uint32_t, PieceFall> m_pieceFall;               // by FEMFX object id: landing detection
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
    uint32_t m_fractureWorldSeed = 1;
    uint32_t m_debrisBudget = 200;
    uint64_t m_physicsTick = 0;
    uint32_t m_debrisRemoved = 0;
    void enforceDebrisBudget();
    bool m_startScenesDone = false; // KKE_PHYSICS_SCENES, see fixedUpdate()

    // Scene::BreakTest: ticks until the balls drop, and where.
    int m_breakTestTicks = -1;
    std::vector<glm::vec3> m_breakTestTargets;
    glm::vec3 m_breakTestWallTarget{0.0f};

    struct RagdollInstance {
        std::vector<AMD::FmRigidBody*> bodies;
        std::vector<uint32_t> bodyIds, glueIds, hingeIds;
        std::vector<glm::vec3> halfExtents;
    };
    std::unordered_map<RagdollHandle, RagdollInstance> m_ragdolls;
    RagdollHandle m_nextRagdoll = 1;
    static constexpr uint32_t kRagdollCollisionGroup = 3;
    static constexpr uint32_t kMaxRigidBodies = 256;
    static constexpr uint32_t kMaxExternalBoxes = 96; // of kMaxRigidBodies
    static constexpr uint32_t kExternalCollisionGroup = 4;
    struct ExternalProxy {
        AMD::FmRigidBody* body = nullptr;
        uint32_t id = 0;
        BridgeBox box;
    };
    std::unordered_map<uint64_t, ExternalProxy> m_external;
    std::vector<ExternalImpulse> m_externalImpulses;
    // Vertices near the movable proxies, sampled before a step.
    struct ExternalSample {
        const AMD::FmTetMesh* mesh = nullptr;
        uint32_t vert = 0;
        glm::vec3 velocity{0.0f};
    };
    std::vector<std::vector<ExternalSample>> m_externalSamples; // per movable proxy, in m_externalOrder
    std::vector<uint64_t> m_externalOrder;
    void sampleExternalContacts();
    void measureExternalContacts(float dt);
    void destroyExternalProxies();
    bool m_drawGround = true;
    bool m_showRagdollBodies = false;

    // Real, measured cost, shown in renderUi() and logged once a second —
    // so "physics is slow" is a number, not an impression. Accumulated
    // over a one-second window, then published into the m_last* fields.
    struct TimingWindow {
        uint32_t ticks = 0;
        uint32_t frames = 0;
        double stepMsTotal = 0.0;
        double stepMsMax = 0.0;
        double renderPrepMsTotal = 0.0;
    };
    TimingWindow m_timing;
    double m_timingWindowStart = 0.0; // seconds, steady_clock
    double m_lastStepMsAvg = 0.0;
    double m_lastStepMsMax = 0.0;
    double m_lastRenderPrepMsAvg = 0.0;
    float m_lastTicksPerSecond = 0.0f;
    float m_lastFramesPerSecond = 0.0f;
    void publishTimingWindow(double nowSeconds);

    // Scripted benchmark — set KKE_PHYSICS_BENCH=<ticks> in the
    // environment and physics_demo spawns a fixed sequence of scenes on
    // a fixed tick schedule, logs a summary, and quits. Same scenes at
    // the same simulation ticks on every machine, so logs from different
    // hardware (or before/after a change) compare directly.
    uint64_t m_benchTicks = 0; // 0 = disabled
    double m_benchStartSeconds = 0.0;
    uint64_t m_benchFrames = 0;
    double m_benchStepMsTotal = 0.0;
    double m_benchStepMsMax = 0.0;
    double m_benchRenderPrepMsTotal = 0.0;
    bool m_benchDone = false;
    std::vector<double> m_benchStepSamples;            // every tick's step time, for percentiles
    std::vector<std::vector<double>> m_benchWindows;   // one row per second, see publishTimingWindow()
    int m_workerThreads = 1;
    unsigned m_hardwareThreads = 0;
    void writeBenchReport(double wallSeconds, uint32_t totalPieces, uint32_t totalTets);
    void benchTick(uint64_t tickIndex);
};

} // namespace kke

#endif // KKE_ENABLE_FEMFX

