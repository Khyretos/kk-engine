#include "NetworkModule.h"
#include "kke/Application.h"

#include <imgui.h>

namespace kke_demo {

void NetworkModule::init(kke::Application& app) {
    for (auto* replicable : app.findCapability<kke::INetworkReplicable>()) {
        m_replicables.push_back({ replicable, replicable->replicationChannelName(), 0 });
    }
}

void NetworkModule::update(const kke::UpdateContext& ctx) {
    m_timeSinceLastSync += ctx.dt;
    if (m_timeSinceLastSync < 1.0f) return;
    m_timeSinceLastSync = 0.0f;

    for (auto& status : m_replicables) {
        // "Sending": in a real transport this is where the payload goes
        // into a packet. Here we just measure it, which is the point —
        // this module has no idea what a destruction system, a physics
        // body, or anything else actually IS, only that something handed
        // it a channel name and a byte vector.
        auto payload = status.replicable->serializeReplicatedState();
        status.lastPayloadBytes = payload.size();
    }
}

void NetworkModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 400), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Network (stub)");
    ImGui::TextWrapped(
        "No real transport — this demonstrates discovery only. Found %zu "
        "INetworkReplicable module(s) via Application::findCapability<>(), "
        "without knowing what any of them are.", m_replicables.size());
    ImGui::Separator();

    if (m_replicables.empty()) {
        ImGui::TextDisabled("(none found — works fine standalone too)");
    }
    for (const auto& status : m_replicables) {
        ImGui::Text("%s: %zu bytes", status.channelName.c_str(), status.lastPayloadBytes);
    }
    ImGui::End();
}

} // namespace kke_demo
