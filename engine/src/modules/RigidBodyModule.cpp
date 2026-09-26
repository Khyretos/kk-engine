#include "kke/modules/RigidBodyModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/PhysicsBridge.h"
#include "kke/PhysicsWorld.h"
#include "kke/Ragdoll.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>

namespace kke {

RigidBodyModule::RigidBodyModule(const RigidWorld::Settings& settings) : m_settings(settings) {}

void RigidBodyModule::init(Application& app) {
    // Threads from the resource governor (Jolt's pool plus the calling
    // thread), unless the game set a count. KKE_RIGID_THREADS overrides
    // both (0 = run on the calling thread only).
    if (m_settings.threads < 0) m_settings.threads = std::max(0, app.resourceBudget().workerThreads - 1);
    if (const char* t = std::getenv("KKE_RIGID_THREADS")) m_settings.threads = std::atoi(t);
    m_world = std::make_unique<RigidWorld>(m_settings);
    log::get(name())->info("Jolt rigid-body world ready ({} max bodies)", m_settings.maxBodies);
}

void RigidBodyModule::frameStart(const UpdateContext&) { m_frameContacts.clear(); }

void RigidBodyModule::fixedUpdate(const FixedUpdateContext& ctx) {
    if (paused) return;
    m_world->step(ctx.fixedDt);
    std::vector<RigidWorld::Contact> c = m_world->takeContacts();
    m_frameContacts.insert(m_frameContacts.end(), c.begin(), c.end());
    const double ms = m_world->lastStepMs();
    m_msAvg = m_msAvg * 0.95 + ms * 0.05;
    m_msMax = std::max(m_msMax * 0.995, ms);
}

void RigidBodyModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(260 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Rigid bodies (Jolt)")) { ImGui::End(); return; }
    ImGui::Text("Bodies: %zu (%zu awake)", m_world->bodyCount(), m_world->activeBodyCount());
    ImGui::Text("Step: %.2f ms avg, %.2f ms peak", m_msAvg, m_msMax);
    if (m_world->ragdollCount()) ImGui::Text("Ragdolls: %zu", m_world->ragdollCount());
    ImGui::Checkbox("Paused", &paused);
    ImGui::End();
}

RigidBodyModule::RagdollHandle RigidBodyModule::createRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity) {
    const RigidWorld::RagdollId id = m_world->addRagdoll(desc, initialVelocity);
    if (!id) log::get(name())->warn("createRagdoll: refused ({} bodies, {} joints)", desc.bodies.size(), desc.joints.size());
    return id;
}

void RigidBodyModule::destroyRagdoll(RagdollHandle handle) { m_world->removeRagdoll(handle); }

bool RigidBodyModule::ragdollBodyTransforms(RagdollHandle handle, std::vector<glm::mat4>& out) const {
    return m_world->ragdollTransforms(handle, out);
}

void RigidBodyModule::pushRagdollBody(RagdollHandle handle, int body, const glm::vec3& deltaVelocity) {
    const std::vector<RigidWorld::BodyId> bodies = m_world->ragdollBodies(handle);
    if (body < 0 || body >= static_cast<int>(bodies.size())) return;
    m_world->addVelocity(bodies[body], deltaVelocity);
}

IPhysicsWorld::Stats RigidBodyModule::physicsStats() const {
    Stats s;
    s.bodies = m_world->bodyCount();
    s.awake = m_world->activeBodyCount();
    s.stepMs = m_world->lastStepMs();
    return s;
}

IPhysicsWorld::Hit RigidBodyModule::physicsRaycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
    Hit h;
    const RigidWorld::RayHit r = m_world->raycast(origin, direction, maxDistance);
    if (!r.hit) return h;
    h.hit = true;
    h.point = r.point;
    h.normal = r.normal;
    h.distance = r.distance;
    h.body = r.body;
    h.world = this;
    return h;
}

size_t RigidBodyModule::physicsBlast(const glm::vec3& center, float radius, float speed) {
    if (radius <= 0.0f) return 0;
    std::vector<RigidWorld::BodyBox> found;
    m_world->bodiesInBox(center - glm::vec3(radius), center + glm::vec3(radius), found);
    size_t pushed = 0;
    for (const RigidWorld::BodyBox& b : found) {
        if (b.motion != RigidWorld::Motion::Dynamic) continue;
        const glm::vec3 dv = blastVelocity(center, radius, speed, b.center);
        if (glm::dot(dv, dv) <= 0.0f) continue;
        m_world->addVelocity(b.id, dv);
        ++pushed;
    }
    return pushed;
}

void RigidBodyModule::physicsBoundsInBox(const glm::vec3& min, const glm::vec3& max, std::vector<std::pair<glm::vec3, glm::vec3>>& out) const {
    std::vector<RigidWorld::BodyBox> found;
    m_world->bodiesInBox(min, max, found);
    for (const RigidWorld::BodyBox& b : found) {
        if (b.motion == RigidWorld::Motion::Static) continue;
        BridgeBox box;
        box.center = b.center;
        box.rotation = b.rotation;
        box.halfExtents = b.halfExtents;
        glm::vec3 lo, hi;
        boxBounds(box, lo, hi);
        out.emplace_back(lo, hi);
    }
}

} // namespace kke
