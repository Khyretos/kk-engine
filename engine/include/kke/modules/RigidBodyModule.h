#pragma once

#include "kke/Capabilities.h"
#include "kke/Module.h"
#include "kke/RigidWorld.h"

#include <memory>

namespace kke {

// The engine module around kke::RigidWorld (Jolt): steps it on the fixed
// tick, shows its cost in a panel. Games reach the world with
// app.getModule<RigidBodyModule>()->world(). Rendering stays with the game
// (bodies usually drive ModelModule instances); debugDraw draws every
// body's bounds through DebugDrawModule when it's present.
//
// Also a ragdoll provider (IRagdollPhysics: Jolt ragdolls with joint
// limits and limbs that collide, preferred over FEMFX's by
// bestRagdollPhysics) and an IPhysicsWorld (kke/PhysicsWorld.h).
// Ragdolls need something to land on in this world: a static floor or
// level collision.
class RigidBodyModule : public Module, public IRagdollPhysics, public IPhysicsWorld {
public:
    explicit RigidBodyModule(const RigidWorld::Settings& settings = RigidWorld::Settings{});
    const char* name() const override { return "RigidBodies"; }
    void init(Application& app) override;
    void frameStart(const UpdateContext& ctx) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void renderUi() override;

    RigidWorld& world() { return *m_world; }
    // Every contact reported by the steps of this frame (cleared at the
    // start of the next). The one place contacts are taken from the world,
    // so several listeners (audio, particles, damage) can all read them;
    // don't call world().takeContacts() yourself when this module runs.
    const std::vector<RigidWorld::Contact>& frameContacts() const { return m_frameContacts; }
    bool paused = false;

    // ---- IRagdollPhysics (kke/Ragdoll.h)
    RagdollHandle createRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity) override;
    void destroyRagdoll(RagdollHandle handle) override;
    bool ragdollBodyTransforms(RagdollHandle handle, std::vector<glm::mat4>& out) const override;
    void pushRagdollBody(RagdollHandle handle, int body, const glm::vec3& deltaVelocity) override;
    int ragdollQuality() const override { return 1; }

    // ---- IPhysicsWorld (kke/PhysicsWorld.h)
    const char* physicsEngineName() const override { return "Jolt"; }
    Stats physicsStats() const override;
    Hit physicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const override;
    size_t physicsBlast(const glm::vec3& center, float radius, float speed) override;
    void physicsBoundsInBox(const glm::vec3& min, const glm::vec3& max, std::vector<std::pair<glm::vec3, glm::vec3>>& out) const override;

private:
    RigidWorld::Settings m_settings;
    std::unique_ptr<RigidWorld> m_world;
    std::vector<RigidWorld::Contact> m_frameContacts;
    double m_msAvg = 0.0, m_msMax = 0.0;
};

} // namespace kke
