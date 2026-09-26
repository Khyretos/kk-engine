#pragma once

#include "kke/Module.h"
#include "kke/RigidWorld.h"
#include "kke/net/NetSession.h"
#include "kke/net/ScriptSpawns.h"
#include "kke/net/Visibility.h"
#include "kke/net/WorldMoveCheck.h"
#include "kke/server/Directory.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace kke {

class RigidBodyModule;
class PhysicsModule;
namespace net { class EnetTransport; class ConditionedTransport; class SecureTransport; class RelayHost; class RelayJoin; }
namespace server { class DirectoryBrowser; }

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
// Every connection is encrypted (kke/net/SecureTransport.h). Join codes
// (kke/net/Relay.h): with a relay set (KKE_NET_RELAY=host[:port], or
// `relay`), hosting gets a code like K7M-Q2P that friends type instead of
// an address, and nobody has to forward a port.
//
// Internet servers: the panel asks a server directory (a kke_server with
// the directory role, docs/SERVER_HOSTING.md) for the public servers of
// this game and joins one with a click. Which directory: `directories`,
// or KKE_DIRECTORIES=host:port[,host:port].
//
// Start from the command line: KKE_NET=host | host:PORT | join:ADDRESS[:PORT] | join:CODE[@RELAY],
// KKE_NET_NAME=Kees, KKE_NET_PASSWORD=secret (to join one; hosting, to
// require it). Feel a bad connection on a LAN: KKE_NET_LAG=ms,
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
    // `address`: an address, or a join code ("K7M-Q2P", or "K7M-Q2P@relay.example.org").
    bool join(const std::string& address, uint16_t port = kDefaultPort, std::string* error = nullptr);
    void leave();
    Role role() const { return m_role; }
    bool authority() const { return m_role != Role::Client; } // offline or host: this game's physics is the truth
    bool connected() const;
    const std::string& statusText() const { return m_status; }
    uint8_t localPlayerId() const;
    const std::string& gameId() const { return m_config.gameId; }

    std::string playerName = "Player";
    // Server directories ("host:port") the panel's server list asks; the
    // first is asked unless the player types another. A game can ship its own.
    std::vector<std::string> directories;
    // Asks `directory` ("host:port") for this game's public servers; the
    // answer arrives over the next frames (directoryServers()).
    bool browseDirectory(const std::string& directory, std::string* error = nullptr);
    const std::vector<server::DirectoryEntry>& directoryServers() const;
    std::string relay;               // "host[:port]": join codes (hosting gets one; joining one asks here unless it names a relay)
    std::string joinCode() const;    // while hosting with a relay: "K7M-Q2P@relay" once it gave one
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
    static constexpr uint16_t kScriptEventKind = script_net::kScriptEvent; // Lua net.send (docs/SCRIPTING.md)
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

    // --- voice chat (docs/NETWORKING.md "Voice"; kke::VoiceModule captures and plays)
    // One Opus frame of ours; the server decides who hears it.
    void sendVoice(net::VoiceChannel channel, uint16_t seq, const std::vector<uint8_t>& opusFrame);
    // Voice for us to play (speaker = their player id).
    void addVoiceListener(std::function<void(const net::VoiceMsg&)> listener) { m_voiceListeners.push_back(std::move(listener)); }
    net::VoiceRules voiceRules; // host: proximity range, channels, team, server mutes

    // --- movement checks (host)
    bool checkMoves = true;
    net::MoveCheckSettings moveCheckSettings;
    size_t refusedMoves() const { return m_server ? m_server->refusedMoves() : 0; }

    // --- fog of war (host; kke/net/Visibility.h, docs/ANTI_CHEAT.md): a
    // client is only sent the players its player could see (through the
    // static level) or hear. Off by default; competitive games turn it on.
    bool fogOfWar = false;
    net::VisibilitySettings visibilitySettings;

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
    void directoryUi();
    double now() const;
    std::string discoveryInfo() const;

    Application* m_app = nullptr;
    RigidBodyModule* m_rigid = nullptr;
    net::NetConfig m_config;
    Role m_role = Role::Offline;
    std::string m_status = "offline";
    std::string m_serverMessage; // the last kEventServerMessage (a dedicated server's MOTD, "say")
    std::unique_ptr<net::ConditionedTransport> m_transport;
    net::EnetTransport* m_enet = nullptr; // inside m_transport
    std::unique_ptr<net::SecureTransport> m_secure;   // over m_transport: what the server / client use
    std::unique_ptr<net::RelayHost> m_relayHost;      // hosting: our join code
    std::unique_ptr<net::RelayJoin> m_relayJoin;      // joining by code, until we know where to connect
    void updateRelayJoin(double t);
    std::unique_ptr<net::NetServer> m_server;
    std::unique_ptr<net::NetClient> m_client;
    std::unique_ptr<net::EnetTransport> m_search; // LAN search while offline
    double m_searchUntil = 0.0;
    std::unique_ptr<server::DirectoryBrowser> m_browser; // the server list, while asking and after
    std::string m_browseStatus;
    double m_browseUntil = 0.0;

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
    std::unique_ptr<net::Visibility> m_visibility;
    std::map<uint8_t, double> m_moveLogAt;      // player -> when a refusal was last logged
    PhysicsModule* m_physics = nullptr;         // FEMFX, for breakables (null without it)
    std::vector<std::function<void(const net::GameEventMsg&)>> m_listeners;
    std::vector<std::function<void(const net::VoiceMsg&)>> m_voiceListeners;
    void dispatchEvent(const net::GameEventMsg& e);
    std::map<uint8_t, RigidWorld::BodyId> m_capsules; // remote player -> kinematic capsule

    // Panel
    char m_nameInput[32] = "Player";
    char m_passwordInput[65] = "";
    char m_addressInput[128] = "127.0.0.1";
    char m_directoryInput[128] = "";
    int m_portInput = kDefaultPort;
    double m_statTime = 0.0;
    uint64_t m_lastSent = 0, m_lastReceived = 0;
    float m_upKbps = 0.0f, m_downKbps = 0.0f;
};

} // namespace kke
