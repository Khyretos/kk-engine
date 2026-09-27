#include "NetworkModule.h"
#include "kke/Application.h"
#include "kke/modules/DemoPanelModule.h"


namespace kke_demo {

void NetworkModule::init(kke::Application& app) {
    for (auto* replicable : app.findCapability<kke::INetworkReplicable>()) {
        m_replicables.push_back({ replicable, replicable->replicationChannelName(), 0 });
    }
    buildPanel(app);
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

void NetworkModule::buildPanel(kke::Application& app) {
    auto* panel = app.getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Network (stub)");
    s.note("No real transport: this demonstrates discovery only. It finds INetworkReplicable modules via "
           "Application::findCapability<>(), without knowing what any of them are.");
    s.text([this] {
        if (m_replicables.empty()) return std::string("(none found; works fine standalone too)");
        std::string out;
        for (const auto& status : m_replicables)
            out += (out.empty() ? "" : ", ") + status.channelName + ": " + std::to_string(status.lastPayloadBytes) + " bytes";
        return out;
    });
}

} // namespace kke_demo
