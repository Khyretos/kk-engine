#pragma once

// Private to kke::RigidWorld: the cloth and hair it owns (Jolt soft
// bodies), air drag and wind on them, and the engine's clipping protection
// pass, run before every Jolt step. kke/Cloth.h, kke/Hair.h, docs/CLOTH.md
// and docs/HAIR.md describe what it does.

#include "kke/Cloth.h"
#include "kke/Hair.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystem.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/PhysicsStepListener.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/SoftBody/SoftBodyContactListener.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace kke {
class ClothGpu;
}

namespace kke::detail {

class ClothSystem final : public JPH::PhysicsStepListener, public JPH::SoftBodyContactListener {
public:
    // jobs: the protection pass's broad phase runs on it (nullptr: one thread).
    // substeps: RigidWorld::Settings::clothSubsteps.
    ClothSystem(JPH::PhysicsSystem& system, JPH::ObjectLayer layer, JPH::TempAllocator& temp, JPH::JobSystem* jobs, int substeps);
    ~ClothSystem() override;
    ClothSystem(const ClothSystem&) = delete;
    ClothSystem& operator=(const ClothSystem&) = delete;

    uint32_t add(const ClothDesc& desc);
    // Hair: guide strands (chains of distance constraints), in soft bodies of up to
    // kHairPart guides each (Jolt steps each body on its own thread).
    uint32_t addHair(const HairDesc& desc);
    void remove(uint32_t id);
    size_t count() const { return m_cloths.size(); } // soft bodies: cloth and hair parts
    size_t hairCount() const { return m_hairs.size(); }
    size_t clothCount() const;
    bool isHair(uint32_t id) const { return m_hairs.count(id) != 0; }
    bool hairPositions(uint32_t id, std::vector<glm::vec3>& out) const;
    void setHairJoint(uint32_t id, const glm::mat4& head);
    void setHairMotion(uint32_t id, float motion); // RigidWorld::setHairMotion
    float hairMotion(uint32_t id) const;
    void resetHair(uint32_t id);
    void removeHair(uint32_t id);
    HairStats hairStats(uint32_t id) const;
    bool positions(uint32_t id, std::vector<glm::vec3>& out) const;
    // hard: snap every skinned vertex onto its skinned position (creation, reset).
    void setJoints(uint32_t id, const std::vector<glm::mat4>& joints, bool hard = false);
    void setProtection(uint32_t id, ClothProtection level);
    ClothProtection protection(uint32_t id) const;
    void reset(uint32_t id);
    ClothStats stats(uint32_t id) const;
    void setWind(const glm::vec3& v);
    glm::vec3 wind() const { return m_wind; }
    // Full protection's pair search on the GPU (null: the CPU).
    void setGpu(std::shared_ptr<ClothGpu> gpu) { m_gpu = std::move(gpu); }
    double lastMs() const { return m_lastMs; }
    // Called by RigidWorld::step before Jolt's update. Returns how many
    // sub-steps to cut each collision step into: more than one while the
    // protection pass is undoing crossings (RigidWorld::Settings::clothSubsteps).
    int beginStep();
    void endStep(); // after the Jolt update: the protection pass on what it left

    void OnStep(const JPH::PhysicsStepListenerContext& context) override;
    JPH::SoftBodyValidateResult OnSoftBodyContactValidate(const JPH::Body& softBody, const JPH::Body& other, JPH::SoftBodyContactSettings& settings) override;

private:
    struct Cloth {
        JPH::BodyID body;
        ClothProtection level = ClothProtection::Full;
        float contactInvMassScale = 1.0f;    // ClothDesc::contactMass as Jolt's inverse mass scale
        Fabric fabric;
        float wind = 1.0f;                   // ClothDesc::wind
        float thickness = 0.008f;
        int iterations = 6;                  // solver sub-steps per physics step (ClothDesc / HairStyle)
        std::vector<uint32_t> tris;          // faces (protection, air)
        std::vector<glm::vec3> rest;         // rest pose, world
        std::vector<float> restArea;         // per triangle, m^2
        float meanEdge = 0.05f;
        std::vector<uint32_t> ringStart, ring; // each vertex's neighbours (CSR)
        std::vector<glm::vec3> prev;         // world positions after the last pass
        std::vector<glm::vec3> pos, vel;     // scratch: this pass
        // Per vertex: how often it was put back through a triangle lately
        // (+2 an update it was, -1 one it wasn't). Without sub-steps
        // (clothSubsteps = 1), a vertex undone again and again is let go
        // until it settles on a side (tangledAt): layers a solid presses
        // together would otherwise fight the solver until it blows up. With
        // sub-steps they are kept apart instead, and nothing is let go.
        std::vector<uint8_t> undoneStreak, undoneNow;
        std::vector<uint16_t> movedIn;       // scratch: the protection pass (1-based) that last moved it, 0 = none
        std::vector<glm::vec3> solved;       // scratch: pos as the solver left it, before protect()
        std::vector<uint8_t> nearOther;      // scratch: within an edge of a triangle not its own neighbourhood
        std::vector<float> motion, reach;    // scratch: how far each vertex moved this step; the most it or anything it touched moved
        std::vector<float> invMass;
        std::vector<glm::mat4> bindPose;
        bool skinned = false;                // has Jolt skinned constraints
        bool prevValid = false;
        ClothStats stats;
        JPH::Body* stepBody = nullptr;       // this step's body when awake (OnStep only)
        std::vector<glm::vec3> triN, triNPrev; // per triangle, this pass: unit normal now and at the last pass
        std::vector<glm::vec4> triSphere;    // per triangle, this pass: bounding sphere (centre, radius), now and at the last pass
        std::vector<glm::vec3> triMove;      // per triangle, this pass: how far its centroid moved since the last pass
        std::vector<glm::vec3> triMixed;     // per triangle, this step: the cross term of its normal now and at the last pass (unit)
        uint32_t triBase = 0;                // first triangle's index in the pass's triangle arrays
        std::vector<uint32_t> edges;         // every edge once, pairs of vertices
        // Patches: small pieces of the surface (kPatchTriangles triangles
        // grown from a seed across shared edges), for skipping the parts of
        // the pass that can't find anything (ClothSystem::markPatches).
        uint32_t patches = 0;
        std::vector<uint32_t> triPatch, vertPatch, edgePatch; // the patch each is part of
        std::vector<uint8_t> vertLooked;     // scratch, this step: part of a patch the broad phase looks at
        std::vector<uint32_t> patchNearStart, patchNear;      // per patch: the patches sharing an edge with it (CSR)
        uint32_t edgeBase = 0;               // first edge's index in the pass's edge arrays
        // Hair: strands of `strandVerts` vertices (root, follicle, segments);
        // the air pushes on each segment as a cylinder `hairWidth` wide.
        bool hair = false;
        float hairMotion = 1.0f;             // setHairMotion: 1 natural .. 0 solid (not simulated)
        bool hairFree = false;               // hold 0: hangs free at natural
        int baseIterations = 6;              // the style's, before hairMotion
        uint32_t strandVerts = 0;
        float hairWidth = 0.0f;
    };
    // Cells an item's swept bounds cover (empty: skip it).
    struct CellBox {
        glm::ivec3 lo{0}, hi{-1};
        bool valid() const { return hi.x >= lo.x; }
    };
    // Spatial hash of items by cell (counting sort, Teschner et al. 2003).
    struct Grid {
        std::vector<uint32_t> start, fill, items;
        uint32_t mask = 0;
        void build(const std::vector<CellBox>& boxes);
        // Optional: what a query tests first, next to every entry in bucket
        // order, so it rejects most of a bucket reading memory in a row
        // (the edges' grid: its bounds, m_edgeMoves and CellBox lo; filled
        // after build()). Measured: 30% off the edge search in a heap; the
        // triangles' grid gained nothing (fewer candidates per bucket).
        struct Packed {
            glm::vec3 lo;
            uint32_t index;
            glm::vec3 hi;
            uint32_t moves;
            glm::ivec3 cell;
            uint32_t unused;
        };
        std::vector<Packed> packed;
    };
    void load(Cloth& c, JPH::Body& body);
    void store(Cloth& c, JPH::Body& body);
    uint32_t addHairPart(const HairDesc& desc, const std::vector<glm::vec3>& rest, size_t first, size_t count);
    void air(Cloth& c, float dt);
    void airOnStrands(Cloth& c, float dt);
    void protectAll(const JPH::BodyLockInterface& locks);
    void setHairMotionOf(Cloth& c, float motion); // load, protect(), store every Full cloth
    void protect();
    void shapeTriangles(); // triN, triNPrev, triSphere of every active cloth
    bool nearInTopology(const Cloth& c, uint32_t v, uint32_t tri) const;
    bool edgesNear(const Cloth& c, uint32_t e, uint32_t f) const;
    static bool edgesApart(const Cloth& c, uint32_t e, const Cloth& o, uint32_t f, float gap);
    static bool edgesFar(const Cloth& c, uint32_t e, const Cloth& o, uint32_t f, float gap);
    // Both return true when they undid a crossing.
    bool testVertexTriangle(Cloth& c, uint32_t v, Cloth& o, uint32_t tri);
    bool testEdgeEdge(Cloth& c, uint32_t e, Cloth& o, uint32_t f);

    JPH::PhysicsSystem& m_system;
    JPH::ObjectLayer m_layer;
    JPH::TempAllocator& m_temp;
    std::unordered_map<uint32_t, Cloth> m_cloths;
    uint32_t m_next = 1;
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_hairs; // hair id -> its parts (ids in m_cloths)
    glm::vec3 m_wind{0.0f};
    float m_dt = 1.0f / 60.0f; // this collision step (OnStep), for friction
    uint16_t m_pass = 0;       // protect(): the narrow-phase pass running (1-based)
    bool m_protectedAfterStep = false; // endStep() ran the pass: the next step's first OnStep needn't
    int m_substeps = 1;        // while undoing crossings (RigidWorld::Settings::clothSubsteps)
    int m_sub = 1;             // sub-steps per collision step this update (beginStep)
    uint32_t m_calm = 1u << 20; // updates in a row in which the pass undid no crossing
    bool m_undid = false;      // this update's passes undid a crossing
    void setIterations();
    bool tangledAt(uint8_t streak) const;
    double m_stepMs = 0.0, m_lastMs = 0.0;
    // Protection scratch, reused.
    std::vector<CellBox> m_triBox, m_edgeBox;
    std::vector<glm::vec3> m_triLo, m_triHi;        // per triangle: swept bounds + thickness
    std::vector<uint32_t> m_triCloth;               // per triangle (all active cloths): index in m_active
    std::vector<uint32_t> m_edgeCloth;              // per edge: index in m_active
    std::vector<glm::vec3> m_edgeLo, m_edgeHi;      // per edge: swept bounds + thickness
    std::vector<uint8_t> m_edgeMoves;               // per edge: 1 = an end can move, 2 = an end is near another surface
    struct VtPair { uint32_t cloth, vertex, tri; }; // m_active index, vertex, triangle (pass-wide index)
    struct EePair { uint32_t a, b; };               // pass-wide edge indices
    std::vector<VtPair> m_vtPairs;                  // this step's broad phase
    std::vector<EePair> m_eePairs;
    std::vector<uint32_t> m_vertBase;               // first vertex of each active cloth in the pass's vertex numbering
    // Patches of every active cloth, numbered through (m_patchBase per
    // cloth): this step's bounds and normal cones, and which pairs of them
    // the broad phase has to look at (a bit matrix).
    std::vector<uint32_t> m_patchBase;
    std::vector<glm::vec3> m_patchLo, m_patchHi;
    std::vector<glm::vec4> m_patchCone;             // axis, half-angle (radians)
    std::vector<uint8_t> m_patchLooked;             // in any pair the broad phase looks at
    std::vector<uint64_t> m_patchPairs;             // bit a * total + b: look for contacts between patches a and b
    std::vector<uint32_t> m_edgePatch;              // per edge (pass-wide): its patch (pass-wide)
    std::vector<glm::vec3> m_patchSum;              // scratch: sum of each patch's normals
    std::vector<float> m_patchLowest;               // scratch: the lowest cosine from each patch's axis (-2: no cone)
    uint32_t m_patchTotal = 0;
    bool m_allPairs = false;                        // too many patches for the bit matrix: look at every pair
    static void buildPatches(Cloth& c);
    void markPatches();
    bool patchPair(uint32_t a, uint32_t b) const {
        if (m_allPairs) return true;
        const uint64_t bit = uint64_t(a) * m_patchTotal + b;
        return (m_patchPairs[bit >> 6] >> (bit & 63)) & 1u;
    }
    struct Worker {                                 // one thread's broad-phase scratch and results
        std::vector<uint32_t> stamp;
        std::vector<VtPair> vt;
        std::vector<EePair> ee;
    };
    std::vector<Worker> m_workers;
    JPH::JobSystem* m_jobs = nullptr;
    bool m_between = false; // endStep(): after Jolt's update, not between its sub-steps
    void parallel(uint32_t count, const std::function<void(uint32_t, uint32_t, Worker&)>& fn);
    Grid m_triGrid, m_edgeGrid;
    std::vector<Cloth*> m_active;
    // The GPU's search (kke::ClothGpu): the queries packed for it, its
    // answers, and every edge in a hash of its own (the CPU's holds only
    // those near another surface, known only after the vertex queries).
    std::shared_ptr<ClothGpu> m_gpu;
    bool m_gpuCheck = false; // KKE_CLOTH_GPU_CHECK=1: the CPU searches too, and differences are logged
    std::vector<uint32_t> m_gpuWords, m_gpuVt, m_gpuEe, m_gpuFlags;
    std::vector<CellBox> m_vertBox, m_edgeAllBox;
    Grid m_edgeAllGrid;
    bool searchOnGpu(size_t triTotal, size_t edgeTotal, float cell);
    void searchOnCpu(size_t triTotal, size_t edgeTotal, float cell);
    std::vector<uint8_t> m_edgeLooked; // per edge: its patch is looked at
    uint64_t m_gpuChecks = 0, m_gpuDiffered = 0;
    // Seen from cell c only when it is the first cell (per axis) that the
    // query's box (from qa) and this box both cover, and the box covers it
    // (it is not in the bucket only by sharing a hash): once per pair,
    // whichever cells they share, the same on the CPU and the GPU.
    static bool firstShared(const glm::ivec3& c, const glm::ivec3& qa, const CellBox& box) {
        if (glm::any(glm::lessThan(c, box.lo)) || glm::any(glm::greaterThan(c, box.hi))) return false;
        return c == glm::max(qa, box.lo);
    }
};

} // namespace kke::detail
