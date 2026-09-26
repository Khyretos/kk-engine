#pragma once

// The heart of kke_server (docs/SERVER_HOSTING.md): a headless game server
// with the roles its ServerConfig names, its access list, leaderboards and
// console commands. kke_server's main() adds the config layers, signals
// and stdin; tests drive this class directly over a LoopbackTransport.
// Built with KKE_ENABLE_NET.

#include "kke/net/NetSession.h"
#include "kke/server/DirectoryNet.h"
#include "kke/server/Leaderboard.h"
#include "kke/server/ServerAccess.h"
#include "kke/server/ServerConfig.h"
#include "kke/storage/Store.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kke {
class RigidWorld;
namespace net {
class WorldMoveCheck;
}
} // namespace kke

namespace kke::server {

class DedicatedServer {
public:
    // `transport`: the game socket (an EnetTransport in kke_server);
    // unused by a directory-only server. `assetsDir`: where the scene's
    // models are (the physics role).
    DedicatedServer(ServerConfig config, net::ITransport& transport, std::string assetsDir = {});
    ~DedicatedServer();

    // Loads the save folder's files and the scene, opens the sockets.
    // false: it can't run; `errors` says why. Problems it can run with
    // (a damaged ban entry) are logged as warnings.
    bool start(std::vector<std::string>& errors);
    void update(double now);
    // Tells the players, saves, closes. Safe to call twice.
    void stop(const std::string& reason = "the server is stopping");
    bool stopRequested() const { return m_stopRequested; }

    // One console line ("help" lists them); what to print back.
    std::string command(const std::string& line);
    bool save(std::string* error = nullptr);
    // A copy of the store in <saveDir>/backups now, keeping the newest
    // config().backups; the file's path, or "" with `error`.
    std::string backup(std::string* error = nullptr);

    // What the server remembers of each player who has been here
    // (collection "players", key: the folded name; docs/SERVER_HOSTING.md "Saves").
    struct PlayerRecord {
        std::string name;               // as they last spelled it
        uint64_t firstSeen = 0, lastSeen = 0; // seconds since 1970
        uint32_t visits = 0;
        double playSeconds = 0;
        bool hasPosition = false;
        glm::vec3 position{ 0.0f };     // where they were when they left
    };
    std::optional<PlayerRecord> playerRecord(const std::string& name);

    std::function<void(const std::string& line)> log;  // info
    std::function<void(const std::string& line)> warn; // something to look at

    const ServerConfig& config() const { return m_config; }
    net::NetServer* game() { return m_net.get(); }
    ServerAccess& access() { return m_access; }
    Leaderboard& leaderboards() { return m_leaderboards; }
    const DirectoryService* directory() const { return m_directory.get(); }
    size_t collisionBodies() const { return m_collisionBodies; }
    // Where this server keeps data (config().storage); games and future roles (#45) use it.
    storage::Store* store() { return m_store.get(); }
    DirectoryEntry directoryEntry() const; // what it tells directories

private:
    void onEvent(const net::GameEventMsg& e);
    std::string accessPath() const;
    std::string leaderboardPath() const;
    std::string backupDir() const;
    void pruneBackups();
    void seePlayer(uint8_t id);                // joined: counts the visit
    void savePlayer(uint8_t id, bool leaving); // play time and position so far
    void trackPositions();
    int findPlayer(const std::string& idOrName) const; // -1: none
    void info(const std::string& s) const { if (log) log(s); }
    void warning(const std::string& s) const { if (warn) warn(s); else info(s); }

    ServerConfig m_config;
    net::ITransport& m_transport;
    std::string m_assetsDir;
    std::unique_ptr<net::NetServer> m_net;
    ServerAccess m_access;
    Leaderboard m_leaderboards;
    std::unique_ptr<DirectoryService> m_directory;
    std::unique_ptr<storage::Store> m_store;
    std::unique_ptr<DirectoryPublisher> m_publisher;
    std::unique_ptr<RigidWorld> m_world;
    std::unique_ptr<net::WorldMoveCheck> m_moveCheck;
    size_t m_collisionBodies = 0;
    struct Present {
        std::string name;
        double since = 0; // play time counted up to here
        bool hasPosition = false;
        glm::vec3 position{ 0.0f };
    };
    std::map<uint8_t, Present> m_present;
    bool m_backups = false; // on, and the store can make them
    double m_now = 0, m_nextSave = 0, m_nextBackup = 0, m_nextTrack = 0;
    bool m_started = false, m_stopRequested = false;
};

} // namespace kke::server
