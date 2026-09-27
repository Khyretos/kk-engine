#pragma once

// Private to kke::RigidWorld: the cloth it owns (Jolt soft bodies) and the
// engine's clipping protection pass, run before every Jolt step.
// kke/Cloth.h and docs/CLOTH.md describe what it does.

#include "kke/Cloth.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/PhysicsStepListener.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace kke::detail {

class ClothSystem final : public JPH::PhysicsStepListener {
public:
    ClothSystem(JPH::PhysicsSystem& system, JPH::ObjectLayer layer, JPH::TempAllocator& temp);
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
    void endStep() { m_lastMs = m_stepMs; }

    void OnStep(const JPH::PhysicsStepListenerContext& context) override;

private:
    struct Cloth {
        JPH::BodyID body;
        ClothProtection level = ClothProtection::Full;
        Fabric fabric;
        float thickness = 0.008f;
        std::vector<uint32_t> tris;          // faces (protection, air)
        std::vector<glm::vec3> rest;         // rest pose, world
        std::vector<float> restArea;         // per triangle, m^2
        float meanEdge = 0.05f;
        std::vector<uint32_t> ringStart, ring; // each vertex's neighbours (CSR)
        std::vector<glm::vec3> prev;         // world positions after the last pass
        std::vector<glm::vec3> pos, vel;     // scratch: this pass
        std::vector<float> invMass;
        std::vector<glm::mat4> bindPose;
        bool skinned = false;                // has Jolt skinned constraints
        bool prevValid = false;
        ClothStats stats;
        JPH::Body* stepBody = nullptr;       // this step's body when awake (OnStep only)
    };
    void load(Cloth& c, JPH::Body& body);
    void store(Cloth& c, JPH::Body& body);
    void air(Cloth& c, float dt);
    void protect();
    bool nearInTopology(const Cloth& c, uint32_t v, uint32_t tri) const;

    JPH::PhysicsSystem& m_system;
    JPH::ObjectLayer m_layer;
    JPH::TempAllocator& m_temp;
    std::unordered_map<uint32_t, Cloth> m_cloths;
    uint32_t m_next = 1;
    glm::vec3 m_wind{0.0f};
    double m_stepMs = 0.0, m_lastMs = 0.0;
    // Protection scratch, reused.
    struct CellEntry { uint64_t cell; uint32_t cloth, tri; };
    std::vector<CellEntry> m_cells;  // sorted by cell
    float m_cellSize = 0.05f;
    std::vector<Cloth*> m_active;
};

} // namespace kke::detail
