#include "kke/server/ServerConfig.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
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
    static const std::vector<std::string> roles{ "players", "physics", "leaderboard", "directory" };
    return roles;
}

bool ServerConfig::hasRole(const std::string& role) const { return std::find(roles.begin(), roles.end(), role) != roles.end(); }

bool ServerConfig::hasGameSocket() const { return hasRole("players") || hasRole("physics") || hasRole("leaderboard"); }

bool splitHostPort(const std::string& s, std::string& host, uint16_t& port) {
    const size_t colon = s.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    host = s.substr(0, colon);
    return parsePort(s.substr(colon + 1), port);
}

bool ServerConfig::loadJson(const std::string& text, std::vector<std::string>& errors) {
    const size_t before = errors.size();
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        errors.push_back("server.json: not a JSON object (check commas and quotes)");
        return false;
    }
    auto str = [&](const char* key, std::string& out) {
        if (!j.contains(key)) return;
        if (j[key].is_string()) out = j[key].get<std::string>();
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
    num("maxPlayers", maxPlayers, 1, 32);
    str("password", password);
    str("motd", motd);
    list("roles", roles);
    str("scene", scene);
    str("saveDir", saveDir);
    list("directories", directories);
    num("directoryPort", directoryPort, 1, 65535);
    num("tickRate", tickRate, 10, 240);
    if (j.contains("public")) {
        if (j["public"].is_boolean()) isPublic = j["public"].get<bool>();
        else errors.push_back("server.json \"public\": expected true or false");
    }
    if (j.contains("clientScores")) {
        if (j["clientScores"].is_boolean()) clientScores = j["clientScores"].get<bool>();
        else errors.push_back("server.json \"clientScores\": expected true or false");
    }
    static const std::vector<std::string> keys{ "name", "game", "port", "maxPlayers", "password", "motd", "roles", "scene", "saveDir",
                                                "directories", "directoryPort", "tickRate", "public", "clientScores" };
    for (auto it = j.begin(); it != j.end(); ++it)
        if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
            errors.push_back("server.json \"" + it.key() + "\": not a setting (a typo?)");
    return errors.size() == before;
}

bool ServerConfig::loadFile(const std::string& path, std::vector<std::string>& errors) {
    std::ifstream in(path);
    if (!in) return true; // no file: the defaults
    std::stringstream ss;
    ss << in.rdbuf();
    return loadJson(ss.str(), errors);
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
        if (parsePort(v, n) && n <= 32) maxPlayers = n;
        else errors.push_back(std::string("KKE_SERVER_MAX_PLAYERS='") + v + "': expected 1 to 32");
    }
    // Empty is meaningful for a password ("no password"), so it is read as set.
    if (const char* v = getenv("KKE_SERVER_PASSWORD")) password = v;
    if (const char* v = get("KKE_SERVER_MOTD")) motd = v;
    if (const char* v = get("KKE_SERVER_ROLES")) roles = splitList(v);
    if (const char* v = get("KKE_SERVER_SCENE")) scene = v;
    if (const char* v = get("KKE_SERVER_SAVE_DIR")) saveDir = v;
    if (const char* v = get("KKE_SERVER_DIRECTORIES")) directories = splitList(v);
    portVar("KKE_SERVER_DIRECTORY_PORT", directoryPort);
    if (const char* v = get("KKE_SERVER_PUBLIC"); v && !parseBool(v, isPublic)) errors.push_back(std::string("KKE_SERVER_PUBLIC='") + v + "': expected true or false");
    if (const char* v = get("KKE_SERVER_CLIENT_SCORES"); v && !parseBool(v, clientScores))
        errors.push_back(std::string("KKE_SERVER_CLIENT_SCORES='") + v + "': expected true or false");
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
        else if (a == "--port" || a == "--directory-port") {
            if (value(v) && !parsePort(v, a == "--port" ? port : directoryPort)) errors.push_back(a + " " + v + ": expected a port, 1 to 65535");
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
        else if (a == "--save-dir") { if (value(v)) saveDir = v; }
        else if (a == "--directory") { if (value(v)) directories.push_back(v); }
        else if (a == "--public") isPublic = true;
        else if (a == "--client-scores") clientScores = true;
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
    if (roles.empty()) errors.push_back("roles: at least one (players, physics, leaderboard, directory)");
    for (const std::string& r : roles)
        if (std::find(knownRoles().begin(), knownRoles().end(), r) == knownRoles().end())
            errors.push_back("roles: '" + r + "' isn't one (players, physics, leaderboard, directory)");
    const bool game_ = hasGameSocket();
    if (hasRole("physics") && scene.empty()) errors.push_back("roles: physics needs a scene (the level whose walls it checks moves against)");
    if (hasRole("directory") && game_ && directoryPort == port) errors.push_back("directoryPort: must differ from port (both are UDP sockets)");
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
    if (!scene.empty()) out += "\n  scene: " + scene;
    if (hasRole("directory")) out += "\n  directory on UDP port " + std::to_string(directoryPort);
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
           "  --roles a,b           players, physics, leaderboard, directory\n"
           "  --scene FILE          level collision for the physics role\n"
           "  --save-dir DIR        access list, leaderboards (default save)\n"
           "  --public              register with the directories below\n"
           "  --directory HOST:PORT a directory to register with (repeatable)\n"
           "  --directory-port N    UDP port of this server's directory role (default 27950)\n"
           "  --client-scores       leaderboard: players may send their own scores\n"
           "Environment: KKE_SERVER_NAME, _GAME, _PORT, _MAX_PLAYERS, _PASSWORD, _MOTD, _ROLES,\n"
           "  _SCENE, _SAVE_DIR, _DIRECTORIES, _DIRECTORY_PORT, _PUBLIC, _CLIENT_SCORES\n"
           "  (flags win over them).\n";
}

} // namespace kke::server
