#pragma once

#include "kke/Module.h"
#include "kke/PhysicsBridge.h"
#include "kke/RigidWorld.h"

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

    size_t boxesLastStep() const { return m_boxes.size(); }
    size_t pushesLastStep() const { return m_pushes; }

private:
    PhysicsModule* m_physics = nullptr;
    RigidBodyModule* m_rigid = nullptr;
    std::unordered_set<RigidWorld::BodyId> m_ignored;
    std::vector<std::pair<glm::vec3, glm::vec3>> m_awake, m_all;
    std::vector<RigidWorld::BodyBox> m_found;
    std::vector<BridgeBox> m_boxes;
    size_t m_pushes = 0;
    bool m_log = false;
    int m_logTicks = 0;
};

} // namespace kke
