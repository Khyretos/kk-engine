#pragma once

// Private to kke::RigidWorld: the cloth it owns (Jolt soft bodies) and the
// engine's clipping protection pass, run before every Jolt step.
// kke/Cloth.h and docs/CLOTH.md describe what it does.

#include "kke/Cloth.h"

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
#include <unordered_map>
#include <vector>

namespace kke::detail {

class ClothSystem final : public JPH::PhysicsStepListener, public JPH::SoftBodyContactListener {
public:
    // jobs: the broad phase of the pass between steps runs on it (nullptr: one thread).
    ClothSystem(JPH::PhysicsSystem& system, JPH::ObjectLayer layer, JPH::TempAllocator& temp, JPH::JobSystem* jobs);
    ~ClothSystem() override;
    ClothSystem(const ClothSystem&) = delete;
    ClothSystem& operator=(const ClothSystem&) = delete;

    uint32_t add(const ClothDesc& desc);
    void remove(uint32_t id);
    size_t count() const { return m_cloths.size(); }
    bool positions(uint32_t id, std::vector<glm::vec3>& out) const;
    // hard: snap every skinned vertex onto its skinned position (creation, reset).
    void setJoints(uint32_t id, const std::vector<glm::mat4>& joints, bool hard = false);
    void setProtection(uint32_t id, ClothProtection level);
    ClothProtection protection(uint32_t id) const;
    void reset(uint32_t id);
    ClothStats stats(uint32_t id) const;
    void setWind(const glm::vec3& v);
    glm::vec3 wind() const { return m_wind; }
    double lastMs() const { return m_lastMs; }
    // Called by RigidWorld::step before Jolt's update: the time spent in
    // OnStep is summed over the step's collision steps.
    void beginStep() { m_stepMs = 0.0; }
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
        std::vector<uint32_t> tris;          // faces (protection, air)
        std::vector<glm::vec3> rest;         // rest pose, world
        std::vector<float> restArea;         // per triangle, m^2
        float meanEdge = 0.05f;
        std::vector<uint32_t> ringStart, ring; // each vertex's neighbours (CSR)
        std::vector<glm::vec3> prev;         // world positions after the last pass
        std::vector<glm::vec3> pos, vel;     // scratch: this pass
        // Per vertex: how often it was put back through a triangle lately
        // (+2 a step it was, -1 a step it wasn't). A real crossing is undone
        // once; a vertex undone again and again is stuck in a tangle the
        // pass can't see the start of (two edges that slid through each
        // other), and is let go until it settles on a side.
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
        std::vector<glm::vec4> triSphere;    // per triangle, this pass: bounding sphere (centre, radius)
        uint32_t triBase = 0;                // first triangle's index in the pass's triangle arrays
        std::vector<uint32_t> edges;         // every edge once, pairs of vertices
        uint32_t edgeBase = 0;               // first edge's index in the pass's edge arrays
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
    };
    void load(Cloth& c, JPH::Body& body);
    void store(Cloth& c, JPH::Body& body);
    void air(Cloth& c, float dt);
    void protectAll(const JPH::BodyLockInterface& locks); // load, protect(), store every Full cloth
    void protect();
    void shapeTriangles(); // triN, triNPrev, triSphere of every active cloth
    bool nearInTopology(const Cloth& c, uint32_t v, uint32_t tri) const;
    bool edgesNear(const Cloth& c, uint32_t e, uint32_t f) const;
    static bool edgesApart(const Cloth& c, uint32_t e, const Cloth& o, uint32_t f, float gap);
    // Both return true when they undid a crossing.
    bool testVertexTriangle(Cloth& c, uint32_t v, Cloth& o, uint32_t tri);
    bool testEdgeEdge(Cloth& c, uint32_t e, Cloth& o, uint32_t f);

    JPH::PhysicsSystem& m_system;
    JPH::ObjectLayer m_layer;
    JPH::TempAllocator& m_temp;
    std::unordered_map<uint32_t, Cloth> m_cloths;
    uint32_t m_next = 1;
    glm::vec3 m_wind{0.0f};
    float m_dt = 1.0f / 60.0f; // this collision step (OnStep), for friction
    uint16_t m_pass = 0;       // protect(): the narrow-phase pass running (1-based)
    bool m_protectedAfterStep = false; // endStep() ran the pass: the next step's first OnStep needn't
    double m_stepMs = 0.0, m_lastMs = 0.0;
    // Protection scratch, reused.
    std::vector<CellBox> m_triBox, m_edgeBox;
    std::vector<uint32_t> m_triCloth;               // per triangle (all active cloths): index in m_active
    std::vector<uint32_t> m_edgeCloth;              // per edge: index in m_active
    std::vector<glm::vec3> m_edgeLo, m_edgeHi;      // per edge: swept bounds + thickness
    std::vector<uint8_t> m_edgeMoves;               // per edge: 1 = an end can move, 2 = an end is near another surface
    struct VtPair { uint32_t cloth, vertex, tri; }; // m_active index, vertex, triangle (pass-wide index)
    struct EePair { uint32_t a, b; };               // pass-wide edge indices
    std::vector<VtPair> m_vtPairs;                  // this step's broad phase
    std::vector<EePair> m_eePairs;
    std::vector<uint32_t> m_vertBase;               // first vertex of each active cloth in the pass's vertex numbering
    struct Worker {                                 // one thread's broad-phase scratch and results
        std::vector<uint32_t> stamp;
        std::vector<VtPair> vt;
        std::vector<EePair> ee;
    };
    std::vector<Worker> m_workers;
    JPH::JobSystem* m_jobs = nullptr;
    bool m_between = false; // endStep(): outside Jolt's update, may use m_jobs
    void parallel(uint32_t count, const std::function<void(uint32_t, uint32_t, Worker&)>& fn);
    Grid m_triGrid, m_edgeGrid;
    std::vector<Cloth*> m_active;
};

} // namespace kke::detail
