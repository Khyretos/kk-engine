#include "kke/server/DedicatedServer.h"

#include "kke/net/Protocol.h"

#if KKE_ENABLE_JOLT
#include "kke/AssetCatalog.h"
#include "kke/RigidWorld.h"
#include "kke/SceneFile.h"
#include "kke/SceneLoader.h"
#include "kke/net/WorldMoveCheck.h"
#endif

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>

namespace kke::server {

namespace {

constexpr double kAutosaveSeconds = 60.0;

uint64_t unixTime() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<std::string> words(const std::string& line, size_t max, std::string& rest) {
    std::istringstream in(line);
    std::vector<std::string> out;
    std::string w;
    while (out.size() < max && in >> w) out.push_back(w);
    std::getline(in, rest);
    rest.erase(0, rest.find_first_not_of(" \t"));
    return out;
}

// "20260926-211500" (UTC): backups sort by name in time order.
std::string stamp() {
    const std::time_t t = std::time(nullptr);
    char buf[32] = {};
    if (const std::tm* u = std::gmtime(&t)) std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", u);
    return buf;
}

std::string toJson(const DedicatedServer::PlayerRecord& r) {
    nlohmann::json j{ { "name", r.name }, { "firstSeen", r.firstSeen }, { "lastSeen", r.lastSeen }, { "visits", r.visits },
                      { "playSeconds", static_cast<uint64_t>(r.playSeconds) } };
    if (r.hasPosition) j["position"] = { r.position.x, r.position.y, r.position.z };
    return j.dump();
}

std::optional<DedicatedServer::PlayerRecord> playerFromJson(const std::string& text) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    DedicatedServer::PlayerRecord r;
    r.name = j.value("name", std::string());
    r.firstSeen = j.value("firstSeen", uint64_t(0));
    r.lastSeen = j.value("lastSeen", uint64_t(0));
    r.visits = j.value("visits", uint32_t(0));
    r.playSeconds = static_cast<double>(j.value("playSeconds", uint64_t(0)));
    if (const auto p = j.find("position"); p != j.end() && p->is_array() && p->size() == 3 &&
                                          std::all_of(p->begin(), p->end(), [](const nlohmann::json& v) { return v.is_number(); })) {
        r.position = glm::vec3((*p)[0].get<float>(), (*p)[1].get<float>(), (*p)[2].get<float>());
        r.hasPosition = true;
    }
    return r;
}

std::vector<uint8_t> text(const std::string& s) {
    return { s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), net::kMaxEventBytes)) };
}

} // namespace

DedicatedServer::DedicatedServer(ServerConfig config, net::ITransport& transport, std::string assetsDir)
    : m_config(std::move(config)), m_transport(transport), m_assetsDir(std::move(assetsDir)) {}

DedicatedServer::~DedicatedServer() { stop(); }

std::string DedicatedServer::accessPath() const { return (std::filesystem::path(m_config.saveDir) / "access.json").string(); }
std::string DedicatedServer::leaderboardPath() const { return (std::filesystem::path(m_config.saveDir) / "leaderboards.json").string(); }
std::string DedicatedServer::backupDir() const { return (std::filesystem::path(m_config.saveDir) / "backups").string(); }

bool DedicatedServer::start(std::vector<std::string>& errors) {
    const size_t before = errors.size();
    if (!m_config.validate(errors)) return false;

    std::error_code ec;
    std::filesystem::create_directories(m_config.saveDir, ec);
    if (ec) errors.push_back("saveDir '" + m_config.saveDir + "': can't make it: " + ec.message());

    {
        const std::string url = m_config.storage.empty() ? "sqlite:" + (std::filesystem::path(m_config.saveDir) / "server.db").string() : m_config.storage;
        std::string error;
        m_store = storage::openStore(url, &error);
        if (!m_store) errors.push_back("storage: " + error);
        else {
            m_store->onNotice = [this](const std::string& m) { warning(m); };
            info(std::string("storage: ") + m_store->backendName());
        }
    }
    std::vector<std::string> fileProblems;
    m_access.load(accessPath(), fileProblems);
    if (m_config.hasRole("leaderboard") && m_store) {
        bool inStore = false;
        m_leaderboards.load(*m_store, fileProblems, &inStore);
        // Leaderboards used to be a file; the first start with a store takes it in.
        if (!inStore && std::filesystem::exists(leaderboardPath())) {
            std::string error;
            if (m_leaderboards.load(leaderboardPath(), fileProblems) && m_leaderboards.save(*m_store, &error)) {
                std::error_code rec;
                std::filesystem::rename(leaderboardPath(), leaderboardPath() + ".imported", rec);
                info("leaderboards: imported " + leaderboardPath() + " into the store" + (rec ? "" : " (the file is now leaderboards.json.imported)"));
            } else if (!error.empty()) {
                fileProblems.push_back(error);
            }
        }
    }
    for (const std::string& p : fileProblems) warning(p);
    m_backups = m_store && m_config.backups > 0 && m_store->canBackup();
    if (m_store && m_config.backups > 0 && !m_store->canBackup())
        info(std::string("backups: a ") + m_store->backendName() + " store is backed up with the database's own tools, not by the server");

    if (m_config.hasRole("physics")) {
#if KKE_ENABLE_JOLT
        try {
            const SceneFile scene = SceneFile::load(m_config.scene);
            const AssetCatalog catalog = AssetCatalog::scan(m_assetsDir);
            RigidWorld::Settings st;
            st.threads = 0; // move checks are rays: no workers needed
            m_world = std::make_unique<RigidWorld>(st);
            const LoadedScene loaded = loadSceneCollision(scene, catalog, *m_world);
            m_collisionBodies = loaded.bodies.size();
            if (!loaded.missing.empty())
                warning("physics: " + std::to_string(loaded.missing.size()) + " models of the scene aren't in the asset folder ('" + m_assetsDir +
                        "'; set KKE_ASSETS_DIR); their walls won't stop anyone");
            m_moveCheck = std::make_unique<net::WorldMoveCheck>(*m_world);
            info("physics: " + m_config.scene + ", " + std::to_string(m_collisionBodies) + " collision bodies, " +
                 std::to_string(loaded.collisionTriangles) + " triangles");
        } catch (const std::exception& e) {
            errors.push_back("scene '" + m_config.scene + "': " + e.what());
        }
#else
        errors.push_back("roles: physics needs a build with Jolt (KKE_ENABLE_JOLT)");
#endif
    }
    if (errors.size() != before) return false;

    if (m_config.hasGameSocket()) {
        net::NetConfig nc;
        nc.gameId = m_config.game;
        nc.maxPlayers = m_config.maxPlayers;
        nc.password = m_config.password;
        nc.dedicated = true;
        m_net = std::make_unique<net::NetServer>(m_transport, nc);
        std::string error;
        if (!m_net->start(m_config.port, m_config.name, {}, &error)) {
            errors.push_back("UDP port " + std::to_string(m_config.port) + ": " + error);
            m_net.reset();
            return false;
        }
        m_net->admit = [this](const std::string& name, const std::string& address) { return m_access.admit(name, address); };
        m_net->onEvent = [this](const net::GameEventMsg& e) { onEvent(e); };
        m_net->onPlayer = [this](uint8_t id, bool joined) {
            if (m_moveCheck && !joined) m_moveCheck->forget(id);
            if (!joined) m_net->voice.muted.erase(id); // the next player with this id starts unmuted
            if (joined) seePlayer(id);
            else savePlayer(id, true);
            info("player " + std::to_string(id) + (joined ? " joined from " + m_net->address(id) : " left") + " (" + std::to_string(m_net->clientCount()) +
                 "/" + std::to_string(m_config.maxPlayers) + ")");
            if (joined && !m_config.motd.empty()) m_net->sendEventTo(id, net::kEventServerMessage, text(m_config.motd));
            if (m_publisher) m_publisher->setEntry(directoryEntry());
        };
#if KKE_ENABLE_JOLT
        if (m_moveCheck)
            m_net->checkMove = [this](uint8_t id, const net::NetPlayerState& from, const net::NetPlayerState& to, double dt) {
                return m_moveCheck->check(id, from, to, dt) == net::WorldMoveCheck::Verdict::Ok;
            };
#endif
    }
    if (m_config.hasRole("directory")) {
        m_directory = std::make_unique<DirectoryService>();
        m_directory->log = [this](const std::string& s) { info(s); };
        std::string error;
        if (!m_directory->start(m_config.directoryPort, &error)) {
            errors.push_back("directory: " + error);
            return false;
        }
        info("directory: listening on UDP port " + std::to_string(m_config.directoryPort));
    }
    if (m_config.isPublic && m_net) {
        m_publisher = std::make_unique<DirectoryPublisher>();
        m_publisher->setEntry(directoryEntry());
        std::string error;
        if (!m_publisher->start(m_config.directories, &error)) {
            errors.push_back("public: " + error);
            return false;
        }
    }
    m_started = true;
    m_stopRequested = false;
    return true;
}

DirectoryEntry DedicatedServer::directoryEntry() const {
    DirectoryEntry e;
    e.name = m_config.name;
    e.game = m_config.game;
    e.port = m_config.port;
    e.players = static_cast<uint16_t>(m_net ? m_net->clientCount() : 0);
    e.maxPlayers = m_config.maxPlayers;
    e.password = !m_config.password.empty();
    e.roles = m_config.roles;
    e.protocol = net::kProtocolVersion;
    return e;
}

void DedicatedServer::update(double now) {
    if (!m_started) return;
    m_now = now;
    if (m_nextSave == 0) m_nextSave = now + kAutosaveSeconds;
    if (m_nextBackup == 0) m_nextBackup = now + m_config.backupMinutes * 60.0;
    if (m_net) m_net->update(now);
    if (m_directory) m_directory->update(now);
    if (m_publisher) m_publisher->update(now);
    if (now >= m_nextTrack) {
        m_nextTrack = now + 1.0;
        trackPositions();
    }
    if (now >= m_nextSave) {
        m_nextSave = now + kAutosaveSeconds;
        std::string error;
        if (m_store && m_leaderboards.dirty() && !m_leaderboards.save(*m_store, &error)) warning("autosave: " + error);
        for (const auto& [id, p] : m_present) savePlayer(id, false);
    }
    if (m_backups && now >= m_nextBackup) {
        m_nextBackup = now + m_config.backupMinutes * 60.0;
        std::string error;
        const std::string path = backup(&error);
        if (path.empty()) warning("backup: " + error);
        else info("backup: " + path);
    }
}

void DedicatedServer::trackPositions() {
    if (!m_net) return;
    for (const net::RemotePlayer& p : m_net->players(m_now))
        if (auto it = m_present.find(p.id); it != m_present.end() && p.hasState) {
            it->second.position = p.state.position;
            it->second.hasPosition = true;
        }
}

std::optional<DedicatedServer::PlayerRecord> DedicatedServer::playerRecord(const std::string& name) {
    if (!m_store) return std::nullopt;
    const std::string key = foldName(name);
    if (!storage::Store::validKey(key)) return std::nullopt;
    const std::optional<std::string> v = m_store->get("players", key);
    return v ? playerFromJson(*v) : std::nullopt;
}

void DedicatedServer::seePlayer(uint8_t id) {
    std::string name;
    for (const net::RemotePlayer& p : m_net->players(m_now))
        if (p.id == id) name = p.name;
    m_present[id] = Present{ name, m_now };
    if (!m_store || !storage::Store::validKey(foldName(name))) return;
    PlayerRecord r = playerRecord(name).value_or(PlayerRecord{});
    const uint64_t t = unixTime();
    if (r.visits == 0) r.firstSeen = t;
    r.name = name;
    r.lastSeen = t;
    ++r.visits;
    if (!m_store->put("players", foldName(name), toJson(r))) warning("players: " + m_store->lastError());
}

void DedicatedServer::savePlayer(uint8_t id, bool leaving) {
    auto it = m_present.find(id);
    if (it == m_present.end()) return;
    Present& p = it->second;
    if (m_store && storage::Store::validKey(foldName(p.name))) {
        PlayerRecord r = playerRecord(p.name).value_or(PlayerRecord{});
        r.name = p.name;
        r.lastSeen = unixTime();
        if (r.firstSeen == 0) r.firstSeen = r.lastSeen;
        r.playSeconds += std::max(0.0, m_now - p.since);
        if (p.hasPosition) {
            r.position = p.position;
            r.hasPosition = true;
        }
        if (!m_store->put("players", foldName(p.name), toJson(r))) warning("players: " + m_store->lastError());
    }
    p.since = m_now;
    if (leaving) m_present.erase(it);
}

std::string DedicatedServer::backup(std::string* error) {
    auto fail = [&](const std::string& e) {
        if (error) *error = e;
        return std::string();
    };
    if (!m_store) return fail("no store");
    if (!m_store->canBackup()) return fail(std::string("a ") + m_store->backendName() + " store is backed up with the database's own tools");
    const std::string path = (std::filesystem::path(backupDir()) / ("server-" + stamp() + ".db")).string();
    if (!m_store->backup(path)) return fail(m_store->lastError());
    pruneBackups();
    return path;
}

void DedicatedServer::pruneBackups() {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(backupDir(), ec)) {
        const std::string n = e.path().filename().string();
        if (e.is_regular_file() && n.rfind("server-", 0) == 0 && n.size() > 3 && n.compare(n.size() - 3, 3, ".db") == 0) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end()); // oldest first: the names are times
    const size_t keep = std::max<size_t>(1, m_config.backups);
    for (size_t i = 0; i + keep < files.size(); ++i) std::filesystem::remove(files[i], ec);
}

void DedicatedServer::onEvent(const net::GameEventMsg& e) {
    const bool board = e.kind == kEventLeaderboardSubmit || e.kind == kEventLeaderboardQuery || e.kind == kEventLeaderboardReply;
    if (!board) {
        // No game code on a plain server: a player's event goes to the others, as a host would pass it on.
        m_net->relayEvent(e);
        return;
    }
    if (!m_config.hasRole("leaderboard")) return;
    if (e.kind == kEventLeaderboardQuery) {
        const auto q = decodeLeaderboardQuery(e.payload);
        LeaderboardReply r;
        if (!q) r.error = "damaged query";
        else {
            r.board = q->board;
            r.entries = m_leaderboards.top(q->board, q->count);
        }
        m_net->sendEventTo(e.fromPlayer, kEventLeaderboardReply, encode(r));
    } else if (e.kind == kEventLeaderboardSubmit) {
        const auto s = decodeLeaderboardSubmit(e.payload);
        LeaderboardReply r;
        if (!s) r.error = "damaged score";
        else {
            r.board = s->board;
            if (!m_config.clientScores) r.error = "this server takes scores from the game, not from players";
            else if (!Leaderboard::validBoardName(s->board)) r.error = "no such board";
            else {
                std::string name;
                for (const net::RemotePlayer& p : m_net->players(m_now))
                    if (p.id == e.fromPlayer) name = p.name;
                m_leaderboards.submit(s->board, name, s->score, unixTime());
                r.entries = m_leaderboards.top(s->board, kMaxLeaderboardReply);
            }
        }
        m_net->sendEventTo(e.fromPlayer, kEventLeaderboardReply, encode(r));
    }
    // A reply sent by a client: not a server's business; dropped.
}

int DedicatedServer::findPlayer(const std::string& idOrName) const {
    if (!m_net) return -1;
    const std::string folded = foldName(idOrName);
    for (const net::RemotePlayer& p : m_net->players(m_now))
        if (std::to_string(p.id) == idOrName || foldName(p.name) == folded) return p.id;
    return -1;
}

bool DedicatedServer::save(std::string* error) {
    std::string e;
    bool ok = m_access.save(accessPath(), &e);
    if (ok && m_config.hasRole("leaderboard") && m_store) ok = m_leaderboards.save(*m_store, &e);
    if (ok)
        for (const auto& [id, p] : m_present) savePlayer(id, false);
    if (!ok && error) *error = e;
    return ok;
}

std::string DedicatedServer::command(const std::string& line) {
    std::string rest;
    const std::vector<std::string> w = words(line, 2, rest);
    if (w.empty()) return {};
    const std::string& cmd = w[0];
    const std::string arg = w.size() > 1 ? w[1] : std::string();
    const std::string tail = rest; // text after the second word
    auto saveAccess = [&](const std::string& done) {
        std::string error;
        return m_access.save(accessPath(), &error) ? done : done + " (but not saved: " + error + ")";
    };

    if (cmd == "help")
        return "status | players | kick <id|name> [reason] | ban <id|name|address> [reason] | unban <name|address> | bans | admin <name> | "
               "allow <name> | say <text> | mute <id|name> | unmute <id|name> | top <board> | seen <name> | save | backup | stop";
    if (cmd == "status") {
        std::string s = m_config.name + ": ";
        if (m_net) s += std::to_string(m_net->clientCount()) + "/" + std::to_string(m_config.maxPlayers) + " players on UDP " + std::to_string(m_config.port);
        if (m_net) s += ", " + std::to_string(m_net->badPackets()) + " bad packets, " + std::to_string(m_net->refusedMoves()) + " moves refused";
        if (m_directory) s += (m_net ? "; " : "") + std::string("directory: ") + std::to_string(m_directory->registry().size()) + " servers listed";
        if (m_config.hasRole("leaderboard")) s += "; " + std::to_string(m_leaderboards.boards().size()) + " leaderboards";
        if (m_store) s += std::string("; storage: ") + m_store->backendName();
        return s;
    }
    if (cmd == "players") {
        if (!m_net) return "no players on a directory-only server";
        std::string s;
        for (const net::RemotePlayer& p : m_net->players(m_now)) {
            const net::PeerStats st = m_net->stats(p.id);
            s += (s.empty() ? "" : "\n") + std::to_string(p.id) + "  " + p.name + "  " + m_net->address(p.id) + "  " + std::to_string(static_cast<int>(st.rttMs)) + " ms";
        }
        return s.empty() ? "nobody is here" : s;
    }
    if (cmd == "kick" || cmd == "ban") {
        if (arg.empty()) return "usage: " + cmd + " <id|name" + (cmd == "ban" ? "|address" : "") + "> [reason]";
        const int id = findPlayer(arg);
        if (cmd == "kick") {
            if (id < 0) return "no player '" + arg + "' (players lists them)";
            m_net->kick(static_cast<uint8_t>(id), tail.empty() ? "kicked by the server" : "kicked: " + tail);
            return "kicked " + arg;
        }
        std::string name, address;
        if (id >= 0) {
            for (const net::RemotePlayer& p : m_net->players(m_now))
                if (p.id == id) name = p.name;
            address = m_net->address(static_cast<uint8_t>(id));
            if (address.rfind("loopback:", 0) == 0) address.clear(); // tests: not a real address
        } else if (arg.find_first_not_of("0123456789.:abcdefABCDEF") == std::string::npos && arg.find('.') != std::string::npos) {
            address = arg; // an IP of someone not here
        } else {
            name = arg; // a name of someone not here
        }
        m_access.ban(name, address, tail);
        if (id >= 0) m_net->kick(static_cast<uint8_t>(id), tail.empty() ? "you are banned from this server" : "you are banned from this server: " + tail);
        return saveAccess("banned " + (name.empty() ? address : name + (address.empty() ? "" : " (" + address + ")")));
    }
    if (cmd == "unban") {
        if (arg.empty()) return "usage: unban <name|address>";
        const size_t n = m_access.unban(arg);
        return n ? saveAccess("unbanned " + arg) : "'" + arg + "' isn't banned";
    }
    if (cmd == "bans") {
        std::string s;
        for (const ServerAccess::Ban& b : m_access.bans())
            s += (s.empty() ? "" : "\n") + (b.name.empty() ? "-" : b.name) + "  " + (b.address.empty() ? "-" : b.address) + "  " + b.reason;
        return s.empty() ? "no bans" : s;
    }
    if (cmd == "admin" || cmd == "allow") {
        if (arg.empty()) return "usage: " + cmd + " <name>";
        if (cmd == "admin") m_access.addAdmin(arg);
        else m_access.allow(arg);
        return saveAccess(cmd == "admin" ? arg + " is an admin" : arg + " may join (only listed players may, now)");
    }
    if (cmd == "mute" || cmd == "unmute") {
        if (arg.empty()) return "usage: " + cmd + " <id|name>";
        const int id = findPlayer(arg);
        if (id < 0) return "no player '" + arg + "' (players lists them)";
        if (cmd == "mute") m_net->voice.muted.insert(static_cast<uint8_t>(id));
        else m_net->voice.muted.erase(static_cast<uint8_t>(id));
        return arg + (cmd == "mute" ? "'s voice isn't passed on (until they leave, or unmute)" : " can be heard again");
    }
    if (cmd == "say") {
        const std::string msg = arg + (tail.empty() ? "" : " " + tail);
        if (msg.empty()) return "usage: say <text>";
        if (!m_net) return "no players on a directory-only server";
        m_net->sendEvent(net::kEventServerMessage, text(msg));
        return "said: " + msg;
    }
    if (cmd == "top") {
        if (!m_config.hasRole("leaderboard")) return "this server has no leaderboard role";
        if (arg.empty()) {
            std::string s;
            for (const std::string& b : m_leaderboards.boards()) s += (s.empty() ? "" : ", ") + b;
            return s.empty() ? "no leaderboards yet" : "boards: " + s;
        }
        std::string s;
        size_t rank = 1;
        for (const Leaderboard::Entry& e : m_leaderboards.top(arg, 10)) s += (s.empty() ? "" : "\n") + std::to_string(rank++) + ". " + e.name + "  " + std::to_string(e.score);
        return s.empty() ? "'" + arg + "' has no scores" : s;
    }
    if (cmd == "seen") {
        if (arg.empty()) return "usage: seen <name>";
        const std::string name = arg + (tail.empty() ? "" : " " + tail);
        const std::optional<PlayerRecord> r = playerRecord(name);
        if (!r) return "'" + name + "' hasn't been here" + (m_store && !m_store->lastError().empty() ? " (" + m_store->lastError() + ")" : "");
        const uint64_t now = unixTime();
        auto ago = [&](uint64_t t) {
            const uint64_t d = now > t ? now - t : 0;
            if (d < 120) return std::to_string(d) + " s ago";
            if (d < 7200) return std::to_string(d / 60) + " min ago";
            if (d < 172800) return std::to_string(d / 3600) + " h ago";
            return std::to_string(d / 86400) + " days ago";
        };
        std::string s = r->name + ": " + std::to_string(r->visits) + (r->visits == 1 ? " visit" : " visits") + ", played " +
                        std::to_string(static_cast<uint64_t>(r->playSeconds) / 60) + " min, first " + ago(r->firstSeen) + ", last " + ago(r->lastSeen);
        for (const auto& [id, p] : m_present)
            if (foldName(p.name) == foldName(name)) s += " (here now, player " + std::to_string(id) + ")";
        if (r->hasPosition) {
            char pos[96];
            std::snprintf(pos, sizeof(pos), "; left at %.1f, %.1f, %.1f", static_cast<double>(r->position.x), static_cast<double>(r->position.y),
                          static_cast<double>(r->position.z));
            s += pos;
        }
        return s;
    }
    if (cmd == "backup") {
        std::string error;
        const std::string path = backup(&error);
        return path.empty() ? "no backup: " + error : "backed up to " + path;
    }
    if (cmd == "save") {
        std::string error;
        return save(&error) ? "saved to " + m_config.saveDir : "not saved: " + error;
    }
    if (cmd == "stop" || cmd == "quit" || cmd == "exit") {
        m_stopRequested = true;
        return "stopping";
    }
    return "unknown command '" + cmd + "' (help lists them)";
}

void DedicatedServer::stop(const std::string& reason) {
    if (!m_started) return;
    m_started = false;
    trackPositions();
    while (!m_present.empty()) savePlayer(m_present.begin()->first, true); // before the kicks: they may not report leaving
    if (m_net) {
        for (const net::RemotePlayer& p : m_net->players(m_now)) m_net->kick(p.id, reason);
        // Flush the goodbyes before the socket closes.
        std::vector<net::NetEvent> ignored;
        m_transport.poll(ignored);
        m_net->stop();
        m_net.reset();
    }
    if (m_publisher) m_publisher->stop();
    m_publisher.reset();
    if (m_directory) m_directory->stop();
    m_directory.reset();
    std::string error;
    if (!save(&error)) warning("on stop: " + error);
    if (m_backups) {
        const std::string path = backup(&error);
        if (path.empty()) warning("backup on stop: " + error);
        else info("backup: " + path);
    }
    m_moveCheck.reset();
    m_world.reset();
    m_store.reset();
}

} // namespace kke::server
