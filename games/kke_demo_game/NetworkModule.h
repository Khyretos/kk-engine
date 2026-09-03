#pragma once

#include "kke/Module.h"
#include "kke/Capabilities.h"

#include <string>
#include <vector>

namespace kke_demo {

// A stub that demonstrates capability-based discovery: it never includes
// DestructionModule.h (or any other module's header) and declares no
// dependencies() — it works whether zero, one, or several
// kke::INetworkReplicable modules exist in the same Application.
//
// This is NOT a real transport — no socket is opened, nothing actually
// leaves the process. What it does do, every ~1 second: ask
// Application::findCapability<INetworkReplicable>() who's out there,
// call serializeReplicatedState() on each, and report the channel name +
// payload size in its ImGui panel — i.e. exactly the "what would go over
// the wire, and how big is it" information you'd want before wiring up a
// real transport (a UDP socket, a WebRTC data channel, whatever).
class NetworkModule : public kke::Module {
public:
    const char* name() const override { return "Network"; }

    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void renderUi() override;

private:
    struct ReplicableStatus {
        kke::INetworkReplicable* replicable;
        std::string channelName;
        size_t lastPayloadBytes = 0;
    };

    std::vector<ReplicableStatus> m_replicables;
    float m_timeSinceLastSync = 0.0f;
};

} // namespace kke_demo
