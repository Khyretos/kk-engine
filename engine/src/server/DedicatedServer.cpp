#include "kke/server/DedicatedServer.h"

#include "kke/net/Protocol.h"
#include "kke/net/ScriptSpawns.h"

#if KKE_ENABLE_LUA
#include "kke/server/ServerScripts.h"
#endif

#if KKE_ENABLE_JOLT
#include "kke/AssetCatalog.h"
#include "kke/RigidWorld.h"
#include "kke/SceneFile.h"
#include "kke/SceneLoader.h"
#include "kke/net/WorldMoveCheck.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace kke::server {

namespace {

constexpr double kAutosaveSeconds = 60.0;
constexpr double kScriptTick = 1.0 / 60.0; // the scripts role steps its world like a game (Tick hook, physics)
constexpr int kMaxTicksPerUpdate = 8;      // behind by more (a paused VM): skip ahead rather than race

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

std::vector<uint8_t> text(const std::string& s) {
    return { s.begin(), s.begin() + static_cast<std::ptrdiff_t>(std::min(s.size(), net::kMaxEventBytes)) };
}

} // namespace

DedicatedServer::DedicatedServer(ServerConfig config, net::ITransport& transport, std::string assetsDir)
    : m_config(std::move(config)), m_transport(transport), m_assetsDir(std::move(assetsDir)) {}

DedicatedServer::~DedicatedServer() { stop(); }

std::string DedicatedServer::accessPath() const { return (std::filesystem::path(m_config.saveDir) / "access.json").string(); }
std::string DedicatedServer::leaderboardPath() const { return (std::filesystem::path(m_config.saveDir) / "leaderboards.json").string(); }

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
    if (m_config.hasRole("leaderboard")) m_leaderboards.load(leaderboardPath(), fileProblems);
    for (const std::string& p : fileProblems) warning(p);

    if (m_config.hasRole("physics") || (m_config.hasRole("scripts") && !m_config.scene.empty())) {
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
            if (m_config.hasRole("physics")) m_moveCheck = std::make_unique<net::WorldMoveCheck>(*m_world);
            info("physics: " + m_config.scene + ", " + std::to_string(m_collisionBodies) + " collision bodies, " +
                 std::to_string(loaded.collisionTriangles) + " triangles");
        } catch (const std::exception& e) {
            errors.push_back("scene '" + m_config.scene + "': " + e.what());
        }
#else
        if (m_config.hasRole("physics")) errors.push_back("roles: physics needs a build with Jolt (KKE_ENABLE_JOLT)");
#endif
    }
#if KKE_ENABLE_JOLT
    if (m_config.hasRole("scripts") && !m_world) {
        // Scripts without a level: an empty world (their bodies fall forever unless they build a floor).
        RigidWorld::Settings st;
        st.threads = 0;
        m_world = std::make_unique<RigidWorld>(st);
    }
#endif
#if !KKE_ENABLE_LUA
    if (m_config.hasRole("scripts")) errors.push_back("roles: scripts needs a build with Lua (ENGINE_ENABLE_LUA)");
#endif
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
#if KKE_ENABLE_LUA
            if (m_scripts) {
                std::string name;
                for (const net::RemotePlayer& p : m_net->players(m_now))
                    if (p.id == id) name = p.name;
                if (joined) m_scripts->playerJoined(id, name);
                else m_scripts->playerLeft(id);
            }
#endif
            if (!joined) m_net->voice.muted.erase(id); // the next player with this id starts unmuted
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
#if KKE_ENABLE_LUA
    if (m_config.hasRole("scripts") && m_net) {
        ServerScripts::Services sv;
        sv.world = m_world.get();
        sv.leaderboards = m_config.hasRole("leaderboard") ? &m_leaderboards : nullptr;
        sv.serverName = m_config.name;
        sv.kick = [this](uint8_t id, const std::string& reason) { m_net->kick(id, reason); };
        m_scripts = std::make_unique<ServerScripts>(m_config.scripts, *m_net, std::move(sv));
        m_scripts->log = [this](const std::string& s) { info(s); };
        m_scripts->warn = [this](const std::string& s) { warning(s); };
        std::string error;
        if (!m_scripts->load(&error)) {
            errors.push_back(error);
            m_scripts.reset();
            return false;
        }
        const auto files = m_scripts->scripts();
        std::string list;
        for (const std::string& f : files) list += (list.empty() ? "" : ", ") + f;
        info("scripts: " + std::to_string(files.size()) + " from '" + m_config.scripts + "'" + (list.empty() ? " (none: sv_*.lua, sh_*.lua)" : ": " + list));
    }
#endif
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
    if (m_nextSave == 0) m_nextSave = now + kAutosaveSeconds;
#if KKE_ENABLE_LUA
    if (m_scripts) {
        // Fixed ticks, as a game's: the Tick hook, then the world steps.
        if (m_tickClock < 0) m_tickClock = now;
        if (now - m_tickClock > kMaxTicksPerUpdate * kScriptTick) m_tickClock = now - kMaxTicksPerUpdate * kScriptTick;
        while (m_tickClock + kScriptTick <= now) {
            m_tickClock += kScriptTick;
            m_scripts->tick(static_cast<float>(kScriptTick), m_tick++);
#if KKE_ENABLE_JOLT
            if (m_world) m_world->step(static_cast<float>(kScriptTick));
#endif
        }
        m_scripts->update(now, static_cast<float>(std::clamp(now - m_now, 0.0, 0.25)));
    }
#endif
    m_now = now;
    if (m_net) m_net->update(now);
    if (m_directory) m_directory->update(now);
    if (m_publisher) m_publisher->update(now);
    if (now >= m_nextSave) {
        m_nextSave = now + kAutosaveSeconds;
        std::string error;
        if (m_leaderboards.dirty() && !m_leaderboards.save(leaderboardPath(), &error)) warning("autosave: " + error);
    }
}

void DedicatedServer::onEvent(const net::GameEventMsg& e) {
    const bool board = e.kind == kEventLeaderboardSubmit || e.kind == kEventLeaderboardQuery || e.kind == kEventLeaderboardReply;
#if KKE_ENABLE_LUA
    if (m_scripts && e.kind == script_net::kScriptEvent) {
        // A client's net.send is for the server's scripts, as it is for a host's.
        m_scripts->netMessage(e);
        return;
    }
#endif
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
    if (ok && m_config.hasRole("leaderboard")) ok = m_leaderboards.save(leaderboardPath(), &e);
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
        return std::string("status | players | kick <id|name> [reason] | ban <id|name|address> [reason] | unban <name|address> | bans | admin <name> | "
                           "allow <name> | say <text> | mute <id|name> | unmute <id|name> | top <board> | save | stop") +
               (m_config.hasRole("scripts") ? " | scripts | reload [file] | lua <code>" : "");
#if KKE_ENABLE_LUA
    if (cmd == "scripts" || cmd == "reload" || cmd == "lua") {
        if (!m_scripts) return "this server has no scripts role";
        return m_scripts->command(cmd, arg + (tail.empty() ? "" : " " + tail));
    }
#endif
    if (cmd == "status") {
        std::string s = m_config.name + ": ";
        if (m_net) s += std::to_string(m_net->clientCount()) + "/" + std::to_string(m_config.maxPlayers) + " players on UDP " + std::to_string(m_config.port);
        if (m_net) s += ", " + std::to_string(m_net->badPackets()) + " bad packets, " + std::to_string(m_net->refusedMoves()) + " moves refused";
        if (m_directory) s += (m_net ? "; " : "") + std::string("directory: ") + std::to_string(m_directory->registry().size()) + " servers listed";
        if (m_config.hasRole("leaderboard")) s += "; " + std::to_string(m_leaderboards.boards().size()) + " leaderboards";
#if KKE_ENABLE_LUA
        if (m_scripts) s += "; " + std::to_string(m_scripts->scripts().size()) + " scripts, " + std::to_string(m_scripts->bodyCount()) + " bodies";
#endif
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
#if KKE_ENABLE_LUA
    if (m_scripts) m_scripts->shutdown(); // their Shutdown hook, while the players are still here
    m_scripts.reset();
#endif
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
    m_moveCheck.reset();
    m_world.reset();
    m_store.reset();
}

} // namespace kke::server
