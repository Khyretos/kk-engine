#include "kke/modules/RigidBodyModule.h"

#include "kke/Application.h"
#include "kke/Log.h"

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

void RigidBodyModule::fixedUpdate(const FixedUpdateContext& ctx) {
    if (paused) return;
    m_world->step(ctx.fixedDt);
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
    ImGui::Checkbox("Paused", &paused);
    ImGui::End();
}

} // namespace kke
