// PhysicsModule's side of the FEMFX <-> Jolt bridge (kke/PhysicsBridge.h,
// docs/PHYSICS_BRIDGE.md): external boxes as kinematic FEMFX rigid
// bodies, and the momentum FEMFX's contacts gave them, measured from the
// velocity change of the vertices touching them.

#include "kke/modules/PhysicsModule.h"

#if KKE_ENABLE_FEMFX

#include "kke/Log.h"

#include <algorithm>

namespace kke {

namespace {
glm::vec3 toG(const AMD::FmVector3& v) { return glm::vec3(v.x, v.y, v.z); }
AMD::FmVector3 toF(const glm::vec3& v) { return AMD::FmInitVector3(v.x, v.y, v.z); }
// Vertices this close to a box's surface (or inside it) count as touching.
constexpr float kContactSkin = 0.03f;
} // namespace

void PhysicsModule::setExternalBoxes(const std::vector<BridgeBox>& boxes) {
    if (!m_scene) return;
    // Gone: out of the scene.
    for (auto it = m_external.begin(); it != m_external.end();) {
        const bool listed = std::any_of(boxes.begin(), boxes.end(), [&](const BridgeBox& b) { return b.key == it->first; });
        if (listed) { ++it; continue; }
        AMD::FmRemoveRigidBodyFromScene(m_scene, it->second.id);
        AMD::FmDestroyRigidBody(it->second.body);
        it = m_external.erase(it);
    }
    for (const BridgeBox& b : boxes) {
        AMD::FmRigidBodyState st;
        st.pos = toF(b.center);
        st.quat = AMD::FmInitQuat(b.rotation.x, b.rotation.y, b.rotation.z, b.rotation.w);
        st.vel = toF(b.velocity);
        st.angVel = toF(b.angularVelocity);
        auto it = m_external.find(b.key);
        if (it != m_external.end()) {
            // A box that changed size (a character crouching) is rebuilt.
            if (glm::any(glm::greaterThan(glm::abs(it->second.box.halfExtents - b.halfExtents), glm::vec3(1e-4f)))) {
                AMD::FmRemoveRigidBodyFromScene(m_scene, it->second.id);
                AMD::FmDestroyRigidBody(it->second.body);
                m_external.erase(it);
            } else {
                AMD::FmSetState(m_scene, it->second.body, st);
                it->second.box = b;
                continue;
            }
        }
        if (m_external.size() >= kMaxExternalBoxes) continue;
        AMD::FmRigidBodySetupParams params;
        params.state = st;
        params.halfDimX = b.halfExtents.x;
        params.halfDimY = b.halfExtents.y;
        params.halfDimZ = b.halfExtents.z;
        params.isKinematic = true;
        params.collisionGroup = static_cast<uint8_t>(kExternalCollisionGroup);
        AMD::FmRigidBody* body = AMD::FmCreateRigidBody(params);
        if (!body) continue;
        ExternalProxy proxy;
        proxy.body = body;
        proxy.id = AMD::FmAddRigidBodyToScene(m_scene, body);
        AMD::FmEnableSleeping(m_scene, body, true); // let pieces resting on it sleep
        proxy.box = b;
        m_external.emplace(b.key, proxy);
    }
}

void PhysicsModule::destroyExternalProxies() {
    for (auto& [key, proxy] : m_external) {
        if (m_scene) AMD::FmRemoveRigidBodyFromScene(m_scene, proxy.id);
        AMD::FmDestroyRigidBody(proxy.body);
    }
    m_external.clear();
    m_externalSamples.clear();
    m_externalOrder.clear();
}

void PhysicsModule::pieceBounds(std::vector<std::pair<glm::vec3, glm::vec3>>& out, bool awakeOnly) const {
    for (const auto& [handle, obj] : m_objects) {
        const uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
        for (uint32_t m = 0; m < n; ++m) {
            const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
            if (!piece || (awakeOnly && AMD::FmIsTetMeshSleeping(*piece))) continue;
            out.emplace_back(toG(AMD::FmGetMinPosition(*piece)), toG(AMD::FmGetMaxPosition(*piece)));
        }
    }
}

// Before the step: the velocities of the awake vertices near each
// movable proxy (the step's velocity change is what measures the push).
void PhysicsModule::sampleExternalContacts() {
    m_externalOrder.clear();
    m_externalSamples.clear();
    for (const auto& [key, proxy] : m_external) {
        if (!proxy.box.movable) continue;
        glm::vec3 lo, hi;
        boxBounds(proxy.box, lo, hi);
        lo -= glm::vec3(kContactSkin * 4.0f);
        hi += glm::vec3(kContactSkin * 4.0f);
        std::vector<ExternalSample> samples;
        for (const auto& [handle, obj] : m_objects) {
            const uint32_t n = AMD::FmGetNumTetMeshes(*obj->tetMeshBuffer);
            for (uint32_t m = 0; m < n; ++m) {
                const AMD::FmTetMesh* piece = AMD::FmGetTetMesh(*obj->tetMeshBuffer, m);
                if (!piece || AMD::FmIsTetMeshSleeping(*piece)) continue;
                if (!boundsOverlap(lo, hi, toG(AMD::FmGetMinPosition(*piece)), toG(AMD::FmGetMaxPosition(*piece)))) continue;
                const uint32_t verts = AMD::FmGetNumVerts(*piece);
                for (uint32_t v = 0; v < verts; ++v) {
                    const glm::vec3 p = toG(AMD::FmGetVertPosition(*piece, v));
                    if (!boundsOverlap(lo, hi, p, p)) continue;
                    samples.push_back({ piece, v, toG(AMD::FmGetVertVelocity(*piece, v)) });
                }
            }
        }
        if (samples.empty()) continue;
        m_externalOrder.push_back(key);
        m_externalSamples.push_back(std::move(samples));
    }
}

// After the step: which of those vertices were pushed out by the box.
void PhysicsModule::measureExternalContacts(float dt) {
    m_externalImpulses.clear();
    if (m_externalOrder.empty()) return;
    const glm::vec3 gravity = toG(AMD::FmGetSceneControlParams(*m_scene).gravityVector);
    std::vector<BridgeVertex> verts;
    for (size_t i = 0; i < m_externalOrder.size(); ++i) {
        auto it = m_external.find(m_externalOrder[i]);
        if (it == m_external.end()) continue;
        verts.clear();
        for (const ExternalSample& s : m_externalSamples[i]) {
            // A piece that split during the step renumbers its vertices:
            // skip what no longer exists (one noisy step at worst).
            if (s.vert >= AMD::FmGetNumVerts(*s.mesh)) continue;
            BridgeVertex v;
            v.position = toG(AMD::FmGetVertPosition(*s.mesh, s.vert));
            v.deltaVelocity = toG(AMD::FmGetVertVelocity(*s.mesh, s.vert)) - s.velocity;
            v.mass = AMD::FmGetVertMass(*s.mesh, s.vert);
            verts.push_back(v);
        }
        ExternalImpulse e;
        e.key = it->first;
        e.impulse = boxContactImpulse(it->second.box, verts, gravity, dt, kContactSkin, &e.point);
        if (glm::dot(e.impulse, e.impulse) > 1e-12f) m_externalImpulses.push_back(e);
    }
    m_externalOrder.clear();
    m_externalSamples.clear();
}

} // namespace kke

#endif // KKE_ENABLE_FEMFX
