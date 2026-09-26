#pragma once

#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/net/NetSession.h"
#include "kke/net/WorldMoveCheck.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace kke {

class RigidBodyModule;
class PhysicsModule;
namespace net { class EnetTransport; class ConditionedTransport; }

// Multiplayer for a game (docs/NETWORKING.md): host a game (your game is the
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
//   Breakables:       replicateBreakable(handle) for every FEMFX breakable
//                     of the level (same order everywhere). The host's
//                     break; a client's copies only break along the
//                     borders the host's broke, so the pieces match.
//   Spawned objects:  the host's spawn(kind, description) makes an object
//                     on every client (a server script's body or
//                     breakable: ScriptModule listens); bindSpawnedBody /
//                     bindSpawnedBreakable tie it to the local copy on
//                     both sides so it moves and breaks with the host's.
//   Movement checks:  the host refuses a client's move through a wall or
//                     a flight (kke/net/WorldMoveCheck.h) and sends it
//                     back to where it last was legitimately.
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
    // More receivers of the same events, for modules other than the game's
    // own (ScriptModule's net.* takes kScriptEventKind). Each sees every event.
    void addEventListener(std::function<void(const net::GameEventMsg&)> listener) { m_listeners.push_back(std::move(listener)); }
    static constexpr uint16_t kScriptEventKind = 0x4C00; // Lua net.send (docs/SCRIPTING.md)
    std::function<void(const glm::vec3&)> onCorrection;
    std::function<void(uint8_t id, bool joined)> onPlayer;

    // --- breakables (FEMFX PhysicsModule handles; docs/NETWORKING.md "Breakables")
    // A level breakable the same on every machine; returns its network id.
    // Call in the same order everywhere, like replicateBody.
    uint16_t replicateBreakable(uint32_t handle);
    void clearBreakables(); // the level's breakables are gone (spawned ones stay)

    // --- objects made while playing (docs/NETWORKING.md "Spawned objects")
    // Ids from here up; the level's bodies and breakables number from 0.
    static constexpr uint16_t kFirstSpawnId = 0x8000;
    // Host: every client (and, if persistent, every later one) gets
    // `kind` + `desc` in its spawn listeners. Returns the network id, or
    // 0 when not hosting (offline: nothing to tell anyone).
    uint16_t spawn(uint16_t kind, const std::vector<uint8_t>& desc, bool persistent = true);
    void despawn(uint16_t id); // host: gone everywhere; also unbinds it here
    // Either side: `id` is this body / breakable here (host: snapshots and
    // breaks go out for it; client: it follows the host's).
    void bindSpawnedBody(uint16_t id, RigidWorld::BodyId body);
    void bindSpawnedBreakable(uint16_t id, uint32_t handle);
    // Client: build / remove your copy. Listeners that don't know the
    // kind ignore it.
    void addSpawnListener(std::function<void(const net::SpawnMsg&)> listener) { m_spawnListeners.push_back(std::move(listener)); }
    void addDespawnListener(std::function<void(uint16_t id)> listener) { m_despawnListeners.push_back(std::move(listener)); }

    // --- movement checks (host)
    bool checkMoves = true;
    net::MoveCheckSettings moveCheckSettings;
    size_t refusedMoves() const { return m_server ? m_server->refusedMoves() : 0; }

    net::LinkConditions simulated; // applied to what this game sends

private:
    void openTransport();
    void syncRemoteCapsules(float dt);
    void pollBreaks();                         // host: send what broke
    void applyBreak(const net::BreakMsg& m);   // client: break along the host's borders
    void applyFollowers();                     // client: breakables break only when told
    void onSpawnMsg(const net::SpawnMsg& m);
    void onDespawnMsg(uint16_t id);
    void dropSpawned();                        // left a game: spawned ids mean nothing any more
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
    std::map<uint16_t, RigidWorld::BodyId> m_bodies; // network id -> body
    uint16_t m_nextLevelBody = 0;
    struct NetBreakable {
        uint32_t handle = 0;
        size_t sentBorders = 0;  // host: broken borders already handed to the server
        bool seedWarned = false;
    };
    std::map<uint16_t, NetBreakable> m_breakables; // network id -> FEMFX breakable
    uint16_t m_nextLevelBreakable = 0;
    std::map<uint16_t, std::vector<net::BreakMsg>> m_pendingBreaks; // client: for breakables not bound yet
    std::set<uint16_t> m_spawned;               // host: live spawn ids
    uint16_t m_nextSpawn = kFirstSpawnId;
    std::vector<std::function<void(const net::SpawnMsg&)>> m_spawnListeners;
    std::vector<std::function<void(uint16_t)>> m_despawnListeners;
    std::unique_ptr<net::WorldMoveCheck> m_moveCheck;
    std::map<uint8_t, double> m_moveLogAt;      // player -> when a refusal was last logged
    PhysicsModule* m_physics = nullptr;         // FEMFX, for breakables (null without it)
    std::vector<std::function<void(const net::GameEventMsg&)>> m_listeners;
    void dispatchEvent(const net::GameEventMsg& e);
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
