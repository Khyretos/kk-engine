#include "kke/server/ServerConfig.h"

#include "kke/DataFile.h"
#include "kke/net/Protocol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>

namespace kke::server {

namespace {

bool parsePort(const std::string& s, uint16_t& out) {
    if (s.empty() || s.size() > 5 || !std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; })) return false;
    const unsigned long v = std::stoul(s);
    if (v == 0 || v > 65535) return false;
    out = static_cast<uint16_t>(v);
    return true;
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream in(s);
    std::string item;
    while (std::getline(in, item, ',')) {
        item.erase(0, item.find_first_not_of(" \t"));
        item.erase(item.find_last_not_of(" \t") + 1);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

bool parseBool(const std::string& s, bool& out) {
    if (s == "1" || s == "true" || s == "yes" || s == "on") { out = true; return true; }
    if (s == "0" || s == "false" || s == "no" || s == "off" || s.empty()) { out = false; return true; }
    return false;
}

} // namespace

const std::vector<std::string>& ServerConfig::knownRoles() {
    static const std::vector<std::string> roles{ "players", "physics", "leaderboard", "scripts", "directory", "relay" };
    return roles;
}

bool ServerConfig::hasRole(const std::string& role) const { return std::find(roles.begin(), roles.end(), role) != roles.end(); }

bool ServerConfig::hasGameSocket() const { return hasRole("players") || hasRole("physics") || hasRole("leaderboard") || hasRole("scripts"); }

bool splitHostPort(const std::string& s, std::string& host, uint16_t& port) {
    const size_t colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    host = s.substr(0, colon);
    return parsePort(s.substr(colon + 1), port);
}

bool ServerConfig::loadJson(const std::string& text, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    std::string why;
    nlohmann::json j;
    if (!datafile::parseAny(text, j, &why) || !j.is_object()) {
        errors.push_back("server.json: not a JSON or YAML object (check commas, quotes and indents)" + (why.empty() ? std::string() : ": " + why));
        return false;
    }
    auto str = [&](const char* key, std::string& out) {
        if (!j.contains(key)) return;
        if (j[key].is_string() || j[key].is_number()) out = datafile::text(j, key); // password: 1234 in YAML is a number
        else errors.push_back(std::string("server.json \"") + key + "\": expected text");
    };
    auto num = [&](const char* key, uint16_t& out, unsigned lo, unsigned hi) {
        if (!j.contains(key)) return;
        const auto& v = j[key];
        if (!v.is_number_integer() || v.get<long long>() < static_cast<long long>(lo) || v.get<long long>() > static_cast<long long>(hi))
            errors.push_back(std::string("server.json \"") + key + "\": expected a whole number from " + std::to_string(lo) + " to " + std::to_string(hi));
        else
            out = static_cast<uint16_t>(v.get<long long>());
    };
    auto list = [&](const char* key, std::vector<std::string>& out) {
        if (!j.contains(key)) return;
        const auto& v = j[key];
        if (!v.is_array() || !std::all_of(v.begin(), v.end(), [](const nlohmann::json& e) { return e.is_string(); })) {
            errors.push_back(std::string("server.json \"") + key + "\": expected a list of text, like [\"a\", \"b\"]");
            return;
        }
        out.clear();
        for (const auto& e : v) out.push_back(e.get<std::string>());
    };
    str("name", name);
    str("game", game);
    num("port", port, 1, 65535);
    num("maxPlayers", maxPlayers, 1, unsigned(net::kMaxPlayers));
    str("password", password);
    str("motd", motd);
    list("roles", roles);
    str("scene", scene);
    str("scripts", scripts);
    str("saveDir", saveDir);
    str("storage", storage);
    list("directories", directories);
    num("directoryPort", directoryPort, 1, 65535);
    str("relay", relay);
    num("relayPort", relayPort, 1, 65535);
    num("relaySlots", relaySlots, 1, 1000);
    num("tickRate", tickRate, 10, 240);
    num("backups", backups, 0, 1000);
    num("backupMinutes", backupMinutes, 1, 10080);
    if (j.contains("public")) {
        if (j["public"].is_boolean()) isPublic = j["public"].get<bool>();
        else errors.push_back("server.json \"public\": expected true or false");
    }
    if (j.contains("clientScores")) {
        if (j["clientScores"].is_boolean()) clientScores = j["clientScores"].get<bool>();
        else errors.push_back("server.json \"clientScores\": expected true or false");
    }
    if (j.contains("fogOfWar")) {
        if (j["fogOfWar"].is_boolean()) fogOfWar = j["fogOfWar"].get<bool>();
        else errors.push_back("server.json \"fogOfWar\": expected true or false");
    }
    static const std::vector<std::string> keys{ "name", "game", "port", "maxPlayers", "password", "motd", "roles", "scene", "saveDir",
                                                "directories", "directoryPort", "tickRate", "public", "clientScores", "fogOfWar", "storage",
                                                "scripts", "relay", "relayPort", "relaySlots", "backups", "backupMinutes" };
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
            errors.push_back("server.json \"" + it.key() + "\": not a setting (a typo?)");
    return errors.size() == before;
}

bool ServerConfig::loadFile(const std::string& path, std::vector<std::string>& errors) {
    std::string text;
    bool exists = false;
    if (!datafile::readText(path, text, &exists)) {
        if (!exists) return true; // no file: the defaults
        errors.push_back(path + ": can't read it");
        return false;
    }
    return loadJson(text, errors);
}

bool ServerConfig::applyEnv(const std::function<const char*(const char*)>& getenv, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    auto get = [&](const char* key) -> const char* {
        const char* v = getenv(key);
        return v && *v ? v : nullptr;
    };
    auto portVar = [&](const char* key, uint16_t& out) {
        if (const char* v = get(key); v && !parsePort(v, out)) errors.push_back(std::string(key) + "='" + v + "': expected a port, 1 to 65535");
    };
    if (const char* v = get("KKE_SERVER_NAME")) name = v;
    if (const char* v = get("KKE_SERVER_GAME")) game = v;
    portVar("KKE_SERVER_PORT", port);
    if (const char* v = get("KKE_SERVER_MAX_PLAYERS")) {
        uint16_t n = 0;
        if (parsePort(v, n) && n >= 1 && n <= net::kMaxPlayers) maxPlayers = n;
        else errors.push_back(std::string("KKE_SERVER_MAX_PLAYERS='") + v + "': expected 1 to " + std::to_string(net::kMaxPlayers));
    }
    // Empty is meaningful for a password ("no password"), so it is read as set.
    if (const char* v = getenv("KKE_SERVER_PASSWORD")) password = v;
    if (const char* v = get("KKE_SERVER_MOTD")) motd = v;
    if (const char* v = get("KKE_SERVER_ROLES")) roles = splitList(v);
    if (const char* v = get("KKE_SERVER_SCENE")) scene = v;
    if (const char* v = get("KKE_SERVER_SCRIPTS")) scripts = v;
    if (const char* v = get("KKE_SERVER_RELAY")) relay = v;
    portVar("KKE_SERVER_RELAY_PORT", relayPort);
    if (const char* v = get("KKE_SERVER_RELAY_SLOTS")) {
        uint16_t n = 0;
        if (parsePort(v, n) && n <= 1000) relaySlots = n;
        else errors.push_back(std::string("KKE_SERVER_RELAY_SLOTS='") + v + "': expected 1 to 1000");
    }
    if (const char* v = get("KKE_SERVER_SAVE_DIR")) saveDir = v;
    if (const char* v = get("KKE_SERVER_STORAGE")) storage = v;
    if (const char* v = get("KKE_SERVER_DIRECTORIES")) directories = splitList(v);
    auto countVar = [&](const char* key, uint16_t& out, unsigned lo, unsigned hi) {
        const char* v = get(key);
        if (!v) return;
        uint16_t n = 0;
        if ((parsePort(v, n) || std::string(v) == "0") && n >= lo && n <= hi) out = n;
        else errors.push_back(std::string(key) + "='" + v + "': expected " + std::to_string(lo) + " to " + std::to_string(hi));
    };
    countVar("KKE_SERVER_BACKUPS", backups, 0, 1000);
    countVar("KKE_SERVER_BACKUP_MINUTES", backupMinutes, 1, 10080);
    portVar("KKE_SERVER_DIRECTORY_PORT", directoryPort);
    if (const char* v = get("KKE_SERVER_PUBLIC"); v && !parseBool(v, isPublic)) errors.push_back(std::string("KKE_SERVER_PUBLIC='") + v + "': expected true or false");
    if (const char* v = get("KKE_SERVER_CLIENT_SCORES"); v && !parseBool(v, clientScores))
        errors.push_back(std::string("KKE_SERVER_CLIENT_SCORES='") + v + "': expected true or false");
    if (const char* v = get("KKE_SERVER_FOG_OF_WAR"); v && !parseBool(v, fogOfWar))
        errors.push_back(std::string("KKE_SERVER_FOG_OF_WAR='") + v + "': expected true or false");
    return errors.size() == before;
}

bool ServerConfig::applyArgs(const std::vector<std::string>& args, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) {
                errors.push_back(a + ": needs a value");
                return false;
            }
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--name") { if (value(v)) name = v; }
        else if (a == "--game") { if (value(v)) game = v; }
        else if (a == "--port" || a == "--directory-port" || a == "--relay-port") {
            uint16_t& target = a == "--port" ? port : a == "--directory-port" ? directoryPort : relayPort;
            if (value(v) && !parsePort(v, target)) errors.push_back(a + " " + v + ": expected a port, 1 to 65535");
        } else if (a == "--max-players") {
            uint16_t n = 0;
            if (value(v)) {
                if (parsePort(v, n) && n <= 32) maxPlayers = n;
                else errors.push_back("--max-players " + v + ": expected 1 to 32");
            }
        } else if (a == "--password") { if (value(v)) password = v; }
        else if (a == "--motd") { if (value(v)) motd = v; }
        else if (a == "--roles") { if (value(v)) roles = splitList(v); }
        else if (a == "--scene") { if (value(v)) scene = v; }
        else if (a == "--scripts") { if (value(v)) scripts = v; }
        else if (a == "--relay") { if (value(v)) relay = v; }
        else if (a == "--relay-slots") {
            uint16_t n = 0;
            if (value(v)) {
                if (parsePort(v, n) && n <= 1000) relaySlots = n;
                else errors.push_back("--relay-slots " + v + ": expected 1 to 1000");
            }
        }
        else if (a == "--save-dir") { if (value(v)) saveDir = v; }
        else if (a == "--storage") { if (value(v)) storage = v; }
        else if (a == "--directory") { if (value(v)) directories.push_back(v); }
        else if (a == "--backups" || a == "--backup-minutes") {
            if (value(v)) {
                const bool count = a == "--backups";
                uint16_t n = 0;
                const bool parsed = parsePort(v, n) || (count && v == "0");
                if (parsed && (count ? n <= 1000 : n >= 1 && n <= 10080)) (count ? backups : backupMinutes) = n;
                else errors.push_back(a + " " + v + (count ? ": expected 0 to 1000" : ": expected 1 to 10080 (a week)"));
            }
        }
        else if (a == "--public") isPublic = true;
        else if (a == "--client-scores") clientScores = true;
        else if (a == "--fog-of-war") fogOfWar = true;
        else if (a == "--config") { value(v); } // read before the layers are applied (kke_server's main)
        else errors.push_back(a + ": unknown option (--help lists them)");
    }
    return errors.size() == before;
}

bool ServerConfig::validate(std::vector<std::string>& errors) const {
    const size_t before = errors.size();
    if (name.empty() || name.size() > 24) errors.push_back("name: 1 to 24 characters");
    if (game.empty() || game.size() > 32) errors.push_back("game: 1 to 32 characters");
    if (password.size() > 64) errors.push_back("password: at most 64 characters");
    std::string known;
    for (const std::string& r : knownRoles()) known += (known.empty() ? "" : ", ") + r;
    if (roles.empty()) errors.push_back("roles: at least one (" + known + ")");
    for (const std::string& r : roles)
        if (std::find(knownRoles().begin(), knownRoles().end(), r) == knownRoles().end()) errors.push_back("roles: '" + r + "' isn't one (" + known + ")");
    const bool game_ = hasGameSocket();
    if (hasRole("physics") && scene.empty()) errors.push_back("roles: physics needs a scene (the level whose walls it checks moves against)");
    if (fogOfWar && !hasRole("physics")) errors.push_back("fogOfWar: needs the physics role (the level's walls decide who sees whom)");
    if (hasRole("directory") && game_ && directoryPort == port) errors.push_back("directoryPort: must differ from port (both are UDP sockets)");
    if (hasRole("relay")) {
        // The relay's port and the slots above it.
        const unsigned lo = relayPort, hi = static_cast<unsigned>(relayPort) + relaySlots;
        if (hi > 65535) errors.push_back("relayPort + relaySlots: past port 65535");
        if (game_ && port >= lo && port <= hi) errors.push_back("relayPort: the relay uses UDP " + std::to_string(lo) + "-" + std::to_string(hi) + ", which takes port");
        if (hasRole("directory") && directoryPort >= lo && directoryPort <= hi)
            errors.push_back("relayPort: the relay uses UDP " + std::to_string(lo) + "-" + std::to_string(hi) + ", which takes directoryPort");
    }
    if (!relay.empty()) {
        std::string h;
        uint16_t p = 0;
        if (!game_) errors.push_back("relay: only a server players join gets a join code (add the players role)");
        if (relay.find(':') != std::string::npos && !splitHostPort(relay, h, p)) errors.push_back("relay: '" + relay + "' isn't host or host:port");
    }
    if (hasRole("scripts") && scripts.empty()) errors.push_back("scripts: the scripts role needs a folder (default scripts)");
    if (isPublic && directories.empty()) errors.push_back("public: true, but no directories to register with");
    for (const std::string& d : directories) {
        std::string h;
        uint16_t p = 0;
        if (!splitHostPort(d, h, p)) errors.push_back("directories: '" + d + "' isn't host:port");
    }
    if (saveDir.empty()) errors.push_back("saveDir: a folder is needed (access list, leaderboards)");
    return errors.size() == before;
}

std::string ServerConfig::describe() const {
    std::string r;
    for (const std::string& role : roles) r += (r.empty() ? "" : ", ") + role;
    std::string d;
    for (const std::string& dir : directories) d += (d.empty() ? "" : ", ") + dir;
    std::string out = "name '" + name + "'";
    if (hasGameSocket())
        out += ", game '" + game + "', UDP port " + std::to_string(port) + ", " + std::to_string(maxPlayers) + " players, password " + (password.empty() ? "none" : "***");
    out += "\n  roles: " + r + "\n  save folder: " + saveDir;
    if (!storage.empty()) {
        std::string shown = storage;
        // postgres://user:password@host -> postgres://user:***@host
        if (const size_t scheme = shown.find("://"), at = shown.rfind('@'); scheme != std::string::npos && at != std::string::npos && at > scheme) {
            const size_t colon = shown.find(':', scheme + 3);
            if (colon != std::string::npos && colon < at) shown.replace(colon + 1, at - colon - 1, "***");
        }
        out += "\n  storage: " + shown;
    }
    out += "\n  backups: " + (backups ? std::to_string(backups) + " kept, every " + std::to_string(backupMinutes) + " min" : std::string("off"));
    if (!scene.empty()) out += "\n  scene: " + scene;
    if (hasRole("scripts")) out += "\n  scripts: " + scripts;
    if (fogOfWar) out += "\n  fog of war: players are sent only who they could see or hear";
    if (hasRole("directory")) out += "\n  directory on UDP port " + std::to_string(directoryPort);
    if (hasRole("relay"))
        out += "\n  relay on UDP port " + std::to_string(relayPort) + " (players relayed on " + std::to_string(relayPort + 1) + "-" +
               std::to_string(relayPort + relaySlots) + ")";
    if (!relay.empty()) out += "\n  join code from relay " + relay;
    if (isPublic) out += "\n  public, listed on: " + d;
    if (hasRole("leaderboard")) out += std::string("\n  leaderboard: ") + (clientScores ? "players may send scores" : "scores from the server only");
    return out;
}

std::string ServerConfig::usage() {
    return "kke_server: a KKE game server (docs/SERVER_HOSTING.md)\n"
           "  --config FILE         settings file (default server.json)\n"
           "  --name TEXT           shown in server lists\n"
           "  --game ID             which game's players may join (default kke)\n"
           "  --port N              UDP port (default 27960)\n"
           "  --max-players N       1 to 32 (default 16)\n"
           "  --password TEXT       required to join\n"
           "  --motd TEXT           message to each player who joins\n"
           "  --roles a,b           players, physics, leaderboard, scripts, directory, relay\n"
           "  --scene FILE          level collision for the physics role\n"
           "  --scripts DIR         the game's server scripts (sv_*.lua, sh_*.lua; default scripts)\n"
           "  --save-dir DIR        access list, leaderboards (default save)\n"
           "  --public              register with the directories below\n"
           "  --directory HOST:PORT a directory to register with (repeatable)\n"
           "  --directory-port N    UDP port of this server's directory role (default 27950)\n"
           "  --client-scores       leaderboard: players may send their own scores\n"
           "  --relay HOST[:PORT]   get a join code from this relay: players join without port forwarding\n"
           "  --relay-port N        UDP port of this server's relay role (default 27970)\n"
           "  --relay-slots N       relay role: players relayed at once, one UDP port each above it (default 64)\n"
           "  --fog-of-war          physics: send each player only who they could see or hear\n"
           "  --storage URL         sqlite:FILE (default save/server.db), valkey://HOST:PORT, postgres://...\n"
           "  --backups N           copies of the store kept in SAVE_DIR/backups (default 5, 0 = none)\n"
           "  --backup-minutes N    how often one is made (default 60)\n"
           "Environment: KKE_SERVER_NAME, _GAME, _PORT, _MAX_PLAYERS, _PASSWORD, _MOTD, _ROLES,\n"
           "  _SCENE, _SCRIPTS, _SAVE_DIR, _DIRECTORIES, _DIRECTORY_PORT, _PUBLIC, _CLIENT_SCORES,\n"
           "  _FOG_OF_WAR, _STORAGE, _RELAY, _RELAY_PORT, _RELAY_SLOTS,\n"
           "  _BACKUPS, _BACKUP_MINUTES\n"
           "  (flags win over them).\n";
}

} // namespace kke::server
