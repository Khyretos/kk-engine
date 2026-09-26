#include "kke/modules/PhysicsBridgeModule.h"

#if KKE_ENABLE_FEMFX && KKE_ENABLE_JOLT

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cstdlib>

namespace kke {

namespace {
// Keys: Jolt body ids as they are; characters with this bit set.
constexpr uint64_t kCharacterKey = 1ull << 40;
// A body or character moving faster than this (m/s) also meets
// sleeping pieces (and wakes them by touching them).
constexpr float kMovingSpeed = 0.2f;

bool touchesAny(const glm::vec3& lo, const glm::vec3& hi, const std::vector<std::pair<glm::vec3, glm::vec3>>& bounds, float margin) {
    const glm::vec3 m(margin);
    for (const auto& [bmin, bmax] : bounds)
        if (boundsOverlap(lo, hi, bmin - m, bmax + m)) return true;
    return false;
}

bool unionOf(const std::vector<std::pair<glm::vec3, glm::vec3>>& bounds, float margin, glm::vec3& lo, glm::vec3& hi) {
    if (bounds.empty()) return false;
    lo = glm::vec3(FLT_MAX);
    hi = glm::vec3(-FLT_MAX);
    for (const auto& [bmin, bmax] : bounds) {
        lo = glm::min(lo, bmin);
        hi = glm::max(hi, bmax);
    }
    lo -= glm::vec3(margin);
    hi += glm::vec3(margin);
    return true;
}
} // namespace

std::vector<ModuleDependency> PhysicsBridgeModule::dependencies() const {
    return { { typeid(PhysicsModule), true, "the FEMFX side" }, { typeid(RigidBodyModule), true, "the Jolt side" } };
}

void PhysicsBridgeModule::init(Application& app) {
    m_physics = app.getModule<PhysicsModule>();
    m_rigid = app.getModule<RigidBodyModule>();
    const char* logEnv = std::getenv("KKE_BRIDGE_LOG");
    m_log = logEnv && *logEnv && *logEnv != '0';
}

void PhysicsBridgeModule::fixedUpdate(const FixedUpdateContext&) {
    if (!m_physics || !m_rigid) return;
    m_boxes.clear();
    m_pushes = 0;
    if (!enabled) {
        if (m_physics->externalBoxCount()) m_physics->setExternalBoxes(m_boxes);
        return;
    }
    RigidWorld& w = m_rigid->world();
    const auto started = std::chrono::steady_clock::now();

    // FEMFX -> Jolt: what the pieces did to the bodies in FEMFX's last step.
    for (const PhysicsModule::ExternalImpulse& e : m_physics->externalImpulses()) {
        if (e.key & kCharacterKey) continue; // characters move as they're told
        w.addImpulse(static_cast<RigidWorld::BodyId>(e.key), e.impulse * pushScale, e.point);
        ++m_pushes;
        ++m_logPushes;
        m_logImpulse += glm::length(e.impulse);
    }

    // Jolt -> FEMFX: bodies near moving pieces (and moving bodies near
    // any piece), as boxes for FEMFX's next step.
    m_awake.clear();
    m_all.clear();
    m_physics->pieceBounds(m_awake, true);
    m_physics->pieceBounds(m_all, false);
    glm::vec3 lo, hi;
    m_found.clear();
    if (unionOf(m_all, margin, lo, hi)) w.bodiesInBox(lo, hi, m_found);
    for (const RigidWorld::BodyBox& b : m_found) {
        if (m_ignored.count(b.id)) continue;
        glm::vec3 bmin, bmax;
        BridgeBox box;
        box.key = b.id;
        box.center = b.center;
        box.rotation = b.rotation;
        box.halfExtents = b.halfExtents;
        box.velocity = b.velocity;
        box.angularVelocity = b.angularVelocity;
        box.movable = b.mass > 0.0f;
        boxBounds(box, bmin, bmax);
        const bool moving = glm::length(b.velocity) > kMovingSpeed;
        if (touchesAny(bmin, bmax, m_awake, margin) || (moving && touchesAny(bmin, bmax, m_all, margin))) m_boxes.push_back(box);
    }
    for (RigidWorld::CharacterId id : w.characterIds()) {
        const float h = w.characterHeight(id), r = w.characterRadius(id);
        BridgeBox box;
        box.key = kCharacterKey | id;
        box.center = w.characterPosition(id) + glm::vec3(0.0f, h * 0.5f, 0.0f);
        box.halfExtents = glm::vec3(r, h * 0.5f, r);
        box.velocity = w.characterVelocity(id);
        glm::vec3 bmin, bmax;
        boxBounds(box, bmin, bmax);
        const bool moving = glm::length(box.velocity) > kMovingSpeed;
        if (touchesAny(bmin, bmax, m_awake, margin) || (moving && touchesAny(bmin, bmax, m_all, margin))) m_boxes.push_back(box);
    }
    m_physics->setExternalBoxes(m_boxes);
    m_logSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    // KKE_BRIDGE_LOG=1: once a second, what the bridge did (tuning, tests).
    if (m_log && ++m_logTicks >= 60) {
        m_logTicks = 0;
        size_t movable = 0;
        for (const BridgeBox& b : m_boxes) movable += b.movable ? 1 : 0;
        log::get(name())->info("{} Jolt boxes in FEMFX ({} movable), {} pushes into Jolt ({:.2f} N s) this second, {} awake pieces, {:.3f} ms a step",
                               m_boxes.size(), movable, m_logPushes, m_logImpulse, m_awake.size(), m_logSeconds * 1000.0 / 60.0);
        m_logPushes = 0;
        m_logSeconds = 0.0;
        m_logImpulse = 0.0f;
    }
}

void PhysicsBridgeModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowSize(ImVec2(260 * s, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("FEMFX <-> Jolt")) { ImGui::End(); return; }
    ImGui::Checkbox("Bridge on", &enabled);
    ImGui::Text("Jolt boxes in FEMFX: %zu", m_boxes.size());
    ImGui::Text("Pushes into Jolt: %zu", m_pushes);
    ImGui::SliderFloat("Push scale", &pushScale, 0.0f, 3.0f, "%.2f");
    ImGui::End();
}

} // namespace kke

#else

namespace kke {
std::vector<ModuleDependency> PhysicsBridgeModule::dependencies() const { return {}; }
void PhysicsBridgeModule::init(Application&) {}
void PhysicsBridgeModule::fixedUpdate(const FixedUpdateContext&) {}
void PhysicsBridgeModule::renderUi() {}
} // namespace kke

#endif
