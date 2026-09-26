#pragma once

// The `scripts` role of kke_server (issue #43, docs/SERVER_HOSTING.md
// "Scripts"): a game's server scripts run headless, with no GPU and no
// player of the server's own. The same sandboxed ScriptVM as in a game
// (kke/ScriptVM.h), with the bindings that make sense on a server:
//
//   kke.*       log, time, dt
//   physics.*   box, sphere, remove, position, velocity, setVelocity,
//               impulse, raycast, count          (a build with Jolt)
//   net.*       role() = "server", isServer() = true, connected(),
//               playerId() = 0, players(), send(name, data [, player])
//   server.*    name(), say(text), kick(player, reason), score(board,
//               player, score), top(board [, n])
//   store.*     save, load, add, remove, keys: the server's store, in the
//               game's own collection, as a host's scripts save
//               (kke/ScriptStore.h; docs/SCRIPTING.md "Saving")
//
// What a script spawns is replicated as the in-game host does it
// (docs/NETWORKING.md "Spawned objects"): every client builds its own
// copy from a small description, late joiners get the ones still there,
// and dynamic bodies travel in snapshots. Players are kinematic capsules
// in the server's world, so they push what scripts spawn.
//
// Which files run: sv_*.lua and sh_*.lua, in name order (the realms of
// docs/SCRIPTING.md). Other scripts are the players' (UI, camera, input)
// and don't load here. Hooks: Init, Think(dt), Tick(dt, tick),
// Contact(c), NetMessage(name, data, from), PlayerJoin(id, name),
// PlayerLeave(id, name), Shutdown. Breakables (FEMFX) aren't on the
// server yet: breakable.* is absent.
//
// Built with KKE_ENABLE_NET and Lua.

#include "kke/ScriptStore.h"
#include "kke/ScriptVM.h"
#include "kke/net/NetSession.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kke {
class RigidWorld;
}

namespace kke::server {

class Leaderboard;

class ServerScripts {
public:
    // Everything else the scripts may reach; null = that part is absent.
    struct Services {
        RigidWorld* world = nullptr;             // physics.* (stepped by the caller)
        Leaderboard* leaderboards = nullptr;     // server.score / server.top
        std::string serverName;
        storage::Store* store = nullptr;         // store.* (the server's store)
        std::string game;                        // whose collection in it ("lua.<game>")
        std::function<void(uint8_t id, const std::string& reason)> kick;
    };

    ServerScripts(std::string folder, net::NetServer& net, Services services);
    ~ServerScripts();
    ServerScripts(const ServerScripts&) = delete;
    ServerScripts& operator=(const ServerScripts&) = delete;

    // Loads the folder's server scripts. A script that fails is reported
    // and skipped (the others still run); false only when the folder
    // can't be read at all.
    bool load(std::string* error = nullptr);
    // Once per server tick, before the world steps: the Tick hook and the
    // players' capsules.
    void tick(float dt, uint64_t tickIndex);
    // After the world stepped: Contact hooks, timers, net messages, Think,
    // and the replicated bodies for the next snapshot.
    void update(double now, float dt);
    void shutdown(); // Shutdown hook, then everything the scripts made goes

    // From the server: a player came or went; a script message arrived
    // (kind script_net::kScriptEvent).
    void playerJoined(uint8_t id, const std::string& name);
    void playerLeft(uint8_t id);
    void netMessage(const net::GameEventMsg& e);

    // Console: "scripts" (their state), "reload [file]", "lua <code>".
    std::string command(const std::string& cmd, const std::string& arg);

    // What the server's log shows (print, kke.log, errors).
    std::function<void(const std::string& line)> log;
    std::function<void(const std::string& line)> warn;

    size_t maxBodiesPerScript = 2000;
    size_t maxMessagesWaiting = 256;
    int maxContactsPerTick = 32;

    ScriptVM& vm() { return *m_vm; }
    size_t bodyCount() const { return m_bodies.size(); }
    std::vector<std::string> scripts() const; // loaded file names

private:
    struct Body {
        uint32_t id;
        std::string source;
        uint16_t netId = 0;
        bool dynamic = true;
    };
    void bind();
    void bindPhysics();
    void release(const std::string& source); // everything `source` made
    void syncCapsules(float dt);
    void report();                           // new VM errors -> warn
    uint16_t nextNetId();

    std::string m_folder;
    net::NetServer& m_net;
    Services m_services;
    std::unique_ptr<ScriptVM> m_vm;
    std::unique_ptr<ScriptStore> m_store;
    std::vector<std::string> m_files;
    std::vector<Body> m_bodies;
    std::map<uint8_t, std::string> m_names;   // players here
    std::map<uint8_t, uint32_t> m_capsules;   // player -> kinematic capsule body
    struct Message { std::string name, data; uint8_t from; };
    std::vector<Message> m_inbox;
    uint16_t m_nextNet = 0x8000;              // spawn ids, as NetModule::kFirstSpawnId
    size_t m_errorsSeen = 0;
    double m_time = 0.0;
    float m_dt = 0.0f;
    bool m_inited = false;
};

} // namespace kke::server
