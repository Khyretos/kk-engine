#pragma once

#include "kke/Module.h"
#include "kke/PhysicsBridge.h"
#include "kke/RigidWorld.h"

#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace kke {

class PhysicsModule;
class RigidBodyModule;

// FEMFX <-> Jolt (issue #7, kke/PhysicsBridge.h, docs/PHYSICS_BRIDGE.md):
// add it next to PhysicsModule and RigidBodyModule and FEMFX pieces land
// on crates and walls, bounce off the character, and push the crates
// they hit. Each fixed step (after both worlds stepped): the pushes
// FEMFX measured go into the Jolt bodies, then the Jolt bodies and
// characters near moving pieces are mirrored into FEMFX as boxes for its
// next step. Pieces that are asleep have no boxes (FEMFX keeps pieces
// that touch a rigid body awake), unless a Jolt body moves into them.
//
// Rubble (issue #31): small pieces broken off a breakable are handed to
// Jolt as convex rigid bodies once their break has played out
// (PhysicsModule::convertToRubble). PhysicsModule keeps drawing them where
// Jolt moves them. A few per step, so a shattering pane doesn't spike.
class PhysicsBridgeModule : public Module {
public:
    const char* name() const override { return "PhysicsBridge"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void renderUi() override;

    // A Jolt body FEMFX should not see (one that duplicates a FEMFX
    // object, like a support block that exists in both worlds).
    void ignore(RigidWorld::BodyId body) { m_ignored.insert(body); }

    bool enabled = true;
    // How far around a piece bodies are mirrored (m): a piece moves up to
    // speed x step between two looks.
    float margin = 0.5f;
    // Scales the pushes back into Jolt (1 = as measured).
    float pushScale = 1.0f;

    // Rubble handoff (see above).
    bool rubble = true;
    float rubbleMaxSize = 0.35f;   // m, largest extent of a piece that goes
    uint32_t rubbleMaxTets = 64;
    float rubbleMaxChunkSize = 0.8f; // m: a piece that can't break further goes up to this size
    int rubblePerStep = 6;         // handoffs per fixed step, at most
    size_t rubbleBudget = 800;     // Jolt debris kept; past it the oldest resting piece goes

    size_t boxesLastStep() const { return m_boxes.size(); }
    size_t rubbleBodies() const { return m_rubble.size(); }
    size_t rubbleHandedOff() const { return m_rubbleTotal; }
    // The Jolt body a handed-off piece became (kNoBody if it isn't rubble).
    RigidWorld::BodyId rubbleBody(uint32_t piece) const;
    size_t pushesLastStep() const { return m_pushes; }

private:
    PhysicsModule* m_physics = nullptr;
    RigidBodyModule* m_rigid = nullptr;
    std::unordered_set<RigidWorld::BodyId> m_ignored;
    std::vector<std::pair<glm::vec3, glm::vec3>> m_awake, m_all;
    std::vector<RigidWorld::BodyBox> m_found;
    std::vector<BridgeBox> m_boxes;
    size_t m_pushes = 0;
    void syncRubble(RigidWorld& w);
    void handOffRubble(RigidWorld& w);
    std::unordered_map<uint32_t, RigidWorld::BodyId> m_rubble;   // PhysicsModule piece -> Jolt body
    std::unordered_set<RigidWorld::BodyId> m_rubbleBodyIds;
    std::deque<uint32_t> m_rubbleOrder;                          // oldest first
    std::unordered_set<uint32_t> m_rubbleRefused;                // Jolt couldn't make a body of it
    std::vector<uint32_t> m_candidates;
    size_t m_rubbleTotal = 0;
    size_t m_logRubble = 0;
    bool m_log = false;
    int m_logTicks = 0;
    size_t m_logPushes = 0;
    float m_logImpulse = 0.0f;
    double m_logSeconds = 0.0;
};

} // namespace kke
