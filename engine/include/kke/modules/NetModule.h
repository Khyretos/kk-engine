#pragma once

#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/net/NetSession.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kke {

class RigidBodyModule;
namespace net { class EnetTransport; class ConditionedTransport; }

// Multiplayer for a game (NETWORKING.md): host a game (your game is the
// server, you play in it), join one by address or from the LAN list, or
// stay offline. Owns the transport (ENet) and a NetServer or NetClient;
// the game tells it about its player and its bodies and draws the others.
//
//   Game each frame:  setLocalPlayer(state), then remotePlayers() to draw
//                     the others (interpolated, ~100 ms behind).
//   Bodies:           replicateBody(id) for every Jolt body that should be
//                     the same everywhere (same order on every machine).
//                     On the host they're simulated and sent; a client
//                     simulates its copies too (pushing needs no round
//                     trip) and steers them toward the host's.
//   Other players:    get a kinematic capsule in the local Jolt world, so
//                     they push crates (on the host, where it counts) and
//                     block the local player.
//   Events:           sendEvent(kind, bytes) for one-off things (a shot,
//                     a push); onEvent receives them. The host decides
//                     what to relay (relayEvent).
//
// Several games on one PC: hosts take the first free port from
// kDefaultPort up (16 ports), and the LAN search asks all of them.
//
// Start from the command line: KKE_NET=host | host:PORT | join:ADDRESS[:PORT],
// KKE_NET_NAME=Kees. Feel a bad connection on a LAN: KKE_NET_LAG=ms,
// KKE_NET_JITTER=ms, KKE_NET_LOSS=percent (also sliders in the panel).
class NetModule : public Module {
public:
    enum class Role { Offline, Host, Client };
    static constexpr uint16_t kDefaultPort = 27960;
    static constexpr uint16_t kPortRange = 16;

    explicit NetModule(const net::NetConfig& config = {});
    ~NetModule() override;

    const char* name() const override { return "Network"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void fixedUpdate(const FixedUpdateContext& ctx) override;
    void update(const UpdateContext& ctx) override;
    void renderUi() override;
    void shutdown() override;

    // --- sessions
    bool host(uint16_t port = 0, std::string* error = nullptr); // 0 = first free from kDefaultPort
    bool join(const std::string& address, uint16_t port = kDefaultPort, std::string* error = nullptr);
    void leave();
    Role role() const { return m_role; }
    bool authority() const { return m_role != Role::Client; } // offline or host: this game's physics is the truth
    bool connected() const;
    const std::string& statusText() const { return m_status; }
    uint8_t localPlayerId() const;

    std::string playerName = "Player";
    std::string playerCharacter;     // what others should draw you as ("" = the game's default)

    // --- the game's side
    void setLocalPlayer(const net::NetPlayerState& state);
    const std::vector<net::RemotePlayer>& remotePlayers() const { return m_remote; }
    // A body the same on every machine; returns its network id. Call in
    // the same order everywhere (e.g. right after spawning a level's crates).
    uint16_t replicateBody(RigidWorld::BodyId body);
    // The set changed (a level reset): forget them all, everywhere.
    void clearBodies();
    void sendEvent(uint16_t kind, const std::vector<uint8_t>& payload);
    void relayEvent(const net::GameEventMsg& e); // host: pass a client's event on to the others

    std::function<void(const net::GameEventMsg&)> onEvent;
    std::function<void(const glm::vec3&)> onCorrection;
    std::function<void(uint8_t id, bool joined)> onPlayer;

    net::LinkConditions simulated; // applied to what this game sends

private:
    void openTransport();
    void syncRemoteCapsules(float dt);
    void driveClientBodies(float dt);
    void lanSearchUi();
    double now() const;
    std::string discoveryInfo() const;

    Application* m_app = nullptr;
    RigidBodyModule* m_rigid = nullptr;
    net::NetConfig m_config;
    Role m_role = Role::Offline;
    std::string m_status = "offline";
    std::unique_ptr<net::ConditionedTransport> m_transport;
    net::EnetTransport* m_enet = nullptr; // inside m_transport
    std::unique_ptr<net::NetServer> m_server;
    std::unique_ptr<net::NetClient> m_client;
    std::unique_ptr<net::EnetTransport> m_search; // LAN search while offline
    double m_searchUntil = 0.0;

    net::NetPlayerState m_local;
    bool m_hasLocal = false;
    std::vector<net::RemotePlayer> m_remote;
    std::vector<RigidWorld::BodyId> m_bodies; // index = network id
    std::map<uint8_t, RigidWorld::BodyId> m_capsules; // remote player -> kinematic capsule

    // Panel
    char m_nameInput[32] = "Player";
    char m_addressInput[128] = "127.0.0.1";
    int m_portInput = kDefaultPort;
    double m_statTime = 0.0;
    uint64_t m_lastSent = 0, m_lastReceived = 0;
    float m_upKbps = 0.0f, m_downKbps = 0.0f;
};

} // namespace kke
