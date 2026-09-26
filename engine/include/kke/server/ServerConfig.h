#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace kke::server {

// What a kke_server runs with (docs/SERVER_HOSTING.md "Run one"). Layers,
// later winning: defaults, server.json, KKE_SERVER_* environment
// variables (Docker), command-line flags. Nothing here ever writes the
// file back: what the owner typed stays as they typed it.
struct ServerConfig {
    std::string name = "KKE server";
    std::string game = "kke";          // clients of another game are turned away (NetConfig::gameId)
    uint16_t port = 27960;             // UDP, the game
    uint16_t maxPlayers = 16;
    std::string password;              // "" = anyone may join
    std::string motd;                  // shown to each player who joins
    std::vector<std::string> roles{ "players" };
    std::string scene;                 // a scenes/*.json whose collision the physics role loads
    std::string saveDir = "save";      // access.json, leaderboards.json, saves
    std::vector<std::string> directories; // "host:port" of directories to register with (public servers)
    bool isPublic = false;             // "public": register with `directories`
    uint16_t directoryPort = 27950;    // UDP, when this server has the directory role
    uint16_t tickRate = 60;            // server updates per second
    std::string storage;               // where the server keeps data (docs/STORAGE.md); "" = sqlite:<saveDir>/server.db
    bool clientScores = false;         // leaderboard: players may send their own scores (easy to cheat; see the docs)
    uint16_t backups = 5;              // copies of the store kept in <saveDir>/backups (0 = none); the oldest goes
    uint16_t backupMinutes = 60;       // how often one is made (and one on stop)

    bool hasRole(const std::string& role) const;
    // Players join (UDP `port`) when any of players, physics or leaderboard is on;
    // a directory-only server has no players.
    bool hasGameSocket() const;

    // Each returns false and appends "where: what" lines to `errors` for
    // what it couldn't use; what it could use is applied either way.
    bool loadJson(const std::string& text, std::vector<std::string>& errors);
    bool loadFile(const std::string& path, std::vector<std::string>& errors); // a missing file is fine: defaults
    bool applyEnv(const std::function<const char*(const char*)>& getenv, std::vector<std::string>& errors);
    // argv without the program name: --name X --port N --max-players N
    // --password X --game X --roles a,b --scene X --save-dir X --public
    // --directory host:port (repeatable) --directory-port N --client-scores --storage URL
    // --backups N --backup-minutes N
    // --config path
    // (read by the caller first) --help.
    bool applyArgs(const std::vector<std::string>& args, std::vector<std::string>& errors);
    // The whole thing sane together (ports, roles known, a physics role
    // with a scene that exists is the caller's to check).
    bool validate(std::vector<std::string>& errors) const;
    // Multi-line summary for the log; the password as *** .
    std::string describe() const;

    static const std::vector<std::string>& knownRoles();
    static std::string usage();
};

// "host:port" -> parts (IPv4, names). False when it isn't one.
bool splitHostPort(const std::string& s, std::string& host, uint16_t& port);

} // namespace kke::server
