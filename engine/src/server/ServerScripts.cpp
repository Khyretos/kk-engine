#include "kke/server/ServerScripts.h"

#include "kke/net/ScriptSpawns.h"
#include "kke/server/Leaderboard.h"

#if KKE_ENABLE_JOLT
#include "kke/RigidWorld.h"
#endif

#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>

namespace kke::server {

namespace {

// Players as the in-game host has them (NetModule): a kinematic capsule
// that pushes what the scripts spawned.
constexpr float kCapsuleRadius = 0.3f, kCapsuleHalfHeight = 0.6f, kCapsuleCentre = 0.9f;
constexpr float kSnapDistance = 2.0f;

bool serverRealm(const std::string& path) {
    const std::string file = std::filesystem::path(path).filename().string();
    return file.rfind("sv_", 0) == 0 || file.rfind("sh_", 0) == 0;
}

uint64_t unixTime() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

std::vector<uint8_t> scriptMessage(const char* name, size_t len, const std::string& bytes) {
    std::vector<uint8_t> payload(name, name + len);
    payload.push_back(0);
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    return payload;
}

} // namespace

ServerScripts::ServerScripts(std::string folder, net::NetServer& net, Services services)
    : m_folder(std::move(folder)), m_net(net), m_services(std::move(services)), m_vm(std::make_unique<ScriptVM>()) {
    m_vm->printSink = [this](const std::string& source, const std::string& text) {
        const std::string file = std::filesystem::path(source).filename().string();
        if (log) log("script " + (file.empty() ? source : file) + ": " + text);
    };
    m_vm->onUnload = [this](const std::string& source) { release(source); };
    bind();
}

ServerScripts::~ServerScripts() { shutdown(); }

uint16_t ServerScripts::nextNetId() {
    // Spawn ids live from 0x8000 up; skip ones still in use after a wrap.
    for (int tries = 0; tries < 0x8000; ++tries) {
        const uint16_t id = m_nextNet;
        m_nextNet = m_nextNet == 0xFFFF ? 0x8000 : static_cast<uint16_t>(m_nextNet + 1);
        if (std::none_of(m_bodies.begin(), m_bodies.end(), [id](const Body& b) { return b.netId == id; })) return id;
    }
    return 0;
}

bool ServerScripts::load(std::string* error) {
    std::error_code ec;
    if (!std::filesystem::is_directory(m_folder, ec)) {
        if (error) *error = "scripts: no folder '" + m_folder + "'";
        return false;
    }
    std::vector<std::string> found;
    for (const auto& e : std::filesystem::directory_iterator(m_folder, ec))
        if (e.is_regular_file() && e.path().extension() == ".lua" && serverRealm(e.path().generic_string())) found.push_back(e.path().generic_string());
    if (ec) {
        if (error) *error = "scripts: can't read '" + m_folder + "': " + ec.message();
        return false;
    }
    std::sort(found.begin(), found.end());
    for (const std::string& path : found) {
        m_vm->runFile(path);
        m_files.push_back(path);
    }
    report();
    return true;
}

std::vector<std::string> ServerScripts::scripts() const {
    std::vector<std::string> out;
    for (const std::string& f : m_files) out.push_back(std::filesystem::path(f).filename().string());
    return out;
}

void ServerScripts::report() {
    const auto& errors = m_vm->errors();
    for (size_t i = m_errorsSeen; i < errors.size(); ++i) {
        const std::string file = std::filesystem::path(errors[i].source).filename().string();
        const std::string line = "script " + (file.empty() ? errors[i].source : file) + ": " + errors[i].message;
        if (warn) warn(line);
        else if (log) log(line);
    }
    m_errorsSeen = errors.size();
    // Keep the list from growing without end on a script that fails every tick.
    if (errors.size() > 1000) {
        m_vm->clearErrors();
        m_errorsSeen = 0;
    }
}

void ServerScripts::tick(float dt, uint64_t tickIndex) {
    syncCapsules(dt);
    m_vm->callHook("Tick", dt, tickIndex);
    report();
}

void ServerScripts::update(double now, float dt) {
    m_time = now;
    m_dt = dt;
    if (!m_inited) {
        m_inited = true;
        m_vm->callHook("Init");
    }
#if KKE_ENABLE_JOLT
    if (RigidWorld* w = m_services.world) {
        // Always taken, so contacts don't pile up while no script listens.
        const std::vector<RigidWorld::Contact> contacts = w->takeContacts();
        if (m_vm->hookCount("Contact") > 0) {
            int n = 0;
            for (const RigidWorld::Contact& c : contacts) {
                if (n++ >= maxContactsPerTick) break;
                m_vm->callHookWith("Contact", [&](lua_State* L) {
                    lua_createtable(L, 0, 6);
                    lua_pushinteger(L, lua_Integer(c.a));
                    lua_setfield(L, -2, "a");
                    lua_pushinteger(L, lua_Integer(c.b));
                    lua_setfield(L, -2, "b");
                    lua_pushnumber(L, c.speed);
                    lua_setfield(L, -2, "speed");
                    lua_pushinteger(L, lua_Integer(c.materialA));
                    lua_setfield(L, -2, "materialA");
                    lua_pushinteger(L, lua_Integer(c.materialB));
                    lua_setfield(L, -2, "materialB");
                    ScriptVM::pushVec3(L, c.point);
                    lua_setfield(L, -2, "pos");
                    return 1;
                });
            }
        }
    }
#endif
    m_vm->updateTimers(now);
    auto inbox = std::move(m_inbox);
    m_inbox.clear();
    for (const Message& m : inbox) {
        m_vm->callHookWith("NetMessage", [&](lua_State* L) {
            lua_pushlstring(L, m.name.data(), m.name.size());
            std::string err;
            if (!ScriptVM::decodeValue(L, m.data, err)) {
                if (warn) warn("script message '" + m.name + "' from player " + std::to_string(m.from) + " is damaged (" + err + "): data is nil");
                lua_pushnil(L);
            }
            lua_pushinteger(L, m.from);
            return 3;
        });
    }
    m_vm->callHook("Think", dt);
#if KKE_ENABLE_JOLT
    if (RigidWorld* w = m_services.world) {
        std::vector<net::NetBodyState> states;
        for (const Body& b : m_bodies) {
            if (!b.netId || !b.dynamic) continue;
            net::NetBodyState s;
            s.id = b.netId;
            s.position = w->position(b.id);
            s.rotation = w->rotation(b.id);
            s.velocity = w->velocity(b.id);
            s.sleeping = !w->isActive(b.id);
            states.push_back(s);
        }
        m_net.setBodies(states);
    }
#endif
    report();
}

void ServerScripts::shutdown() {
    if (!m_vm) return;
    m_vm->callHook("Shutdown");
    report();
    for (const std::string& f : m_files) m_vm->unload(f);
    m_vm->unload("console"); // what `lua` lines made
    m_files.clear();
#if KKE_ENABLE_JOLT
    if (RigidWorld* w = m_services.world)
        for (const auto& [id, body] : m_capsules) w->remove(body);
#endif
    m_capsules.clear();
    m_vm.reset();
}

void ServerScripts::playerJoined(uint8_t id, const std::string& name) {
    m_names[id] = name;
    m_vm->callHook("PlayerJoin", int(id), name);
    report();
}

void ServerScripts::playerLeft(uint8_t id) {
    const std::string name = m_names.count(id) ? m_names[id] : std::string();
    m_names.erase(id);
#if KKE_ENABLE_JOLT
    if (auto it = m_capsules.find(id); it != m_capsules.end()) {
        if (m_services.world) m_services.world->remove(it->second);
        m_capsules.erase(it);
    }
#endif
    m_vm->callHook("PlayerLeave", int(id), name);
    report();
}

void ServerScripts::netMessage(const net::GameEventMsg& e) {
    if (e.kind != script_net::kScriptEvent) return;
    // name \0 value-bytes (ScriptVM::encodeValue); anything else is dropped.
    const auto zero = std::find(e.payload.begin(), e.payload.end(), uint8_t(0));
    if (zero == e.payload.end() || zero == e.payload.begin() || zero - e.payload.begin() > 64) {
        if (warn) warn("dropped a script message from player " + std::to_string(e.fromPlayer) + ": no name");
        return;
    }
    if (m_inbox.size() >= maxMessagesWaiting) {
        if (warn) warn("dropped a script message from player " + std::to_string(e.fromPlayer) + ": " + std::to_string(maxMessagesWaiting) + " already waiting");
        return;
    }
    m_inbox.push_back({ std::string(e.payload.begin(), zero), std::string(zero + 1, e.payload.end()), e.fromPlayer });
}

void ServerScripts::syncCapsules(float dt) {
#if KKE_ENABLE_JOLT
    RigidWorld* w = m_services.world;
    if (!w) return;
    const std::vector<net::RemotePlayer> players = m_net.players(m_time);
    for (auto it = m_capsules.begin(); it != m_capsules.end();) {
        const bool present = std::any_of(players.begin(), players.end(), [&](const net::RemotePlayer& p) { return p.id == it->first && p.hasState; });
        if (present) { ++it; continue; }
        w->remove(it->second);
        it = m_capsules.erase(it);
    }
    const glm::quat upright(1.0f, 0.0f, 0.0f, 0.0f);
    for (const net::RemotePlayer& p : players) {
        if (!p.hasState) continue;
        const glm::vec3 centre = p.state.position + glm::vec3(0.0f, kCapsuleCentre, 0.0f);
        auto it = m_capsules.find(p.id);
        if (it == m_capsules.end()) {
            RigidWorld::BodyDesc d;
            d.shape = RigidWorld::Shape::Capsule;
            d.radius = kCapsuleRadius;
            d.halfHeight = kCapsuleHalfHeight;
            d.motion = RigidWorld::Motion::Kinematic;
            d.position = centre;
            const RigidWorld::BodyId b = w->add(d);
            if (b != RigidWorld::kNoBody) m_capsules[p.id] = b;
            continue;
        }
        if (glm::length(w->position(it->second) - centre) > kSnapDistance) w->setTransform(it->second, centre, upright);
        else w->moveKinematic(it->second, centre, upright, dt);
    }
#else
    (void)dt;
#endif
}

void ServerScripts::release(const std::string& source) {
    for (const Body& b : m_bodies) {
        if (b.source != source) continue;
        if (b.netId) m_net.despawn(b.netId);
#if KKE_ENABLE_JOLT
        if (m_services.world) m_services.world->remove(b.id);
#endif
    }
    std::erase_if(m_bodies, [&](const Body& b) { return b.source == source; });
}

std::string ServerScripts::command(const std::string& cmd, const std::string& arg) {
    if (cmd == "scripts") {
        std::string s;
        const auto& stopped = m_vm->stoppedScripts();
        for (const std::string& f : m_files) {
            size_t bodies = 0;
            for (const Body& b : m_bodies) bodies += b.source == f;
            const bool stop = std::find(stopped.begin(), stopped.end(), f) != stopped.end();
            s += (s.empty() ? "" : "\n") + std::filesystem::path(f).filename().string() + (stop ? "  STOPPED" : "") + "  " + std::to_string(bodies) +
                 " bodies  " + std::to_string(static_cast<int>(m_vm->cpuSeconds(f) * 1000.0)) + " ms of Lua so far";
        }
        return s.empty() ? "no server scripts (sv_*.lua, sh_*.lua) in '" + m_folder + "'" : s;
    }
    if (cmd == "reload") {
        // New files too: a script added while the server runs.
        std::error_code ec;
        std::vector<std::string> found;
        for (const auto& e : std::filesystem::directory_iterator(m_folder, ec))
            if (e.is_regular_file() && e.path().extension() == ".lua" && serverRealm(e.path().generic_string())) found.push_back(e.path().generic_string());
        std::sort(found.begin(), found.end());
        size_t n = 0;
        for (const std::string& path : found) {
            if (!arg.empty() && std::filesystem::path(path).filename().string() != arg) continue;
            if (std::find(m_files.begin(), m_files.end(), path) == m_files.end()) {
                m_files.push_back(path);
                m_vm->runFile(path);
            } else {
                m_vm->reloadFile(path);
            }
            ++n;
        }
        // Deleted since: unloaded.
        for (auto it = m_files.begin(); it != m_files.end();) {
            if (arg.empty() && std::find(found.begin(), found.end(), *it) == found.end()) {
                m_vm->unload(*it);
                it = m_files.erase(it);
            } else {
                ++it;
            }
        }
        const size_t errorsBefore = m_errorsSeen;
        report();
        if (!arg.empty() && n == 0) return "no server script '" + arg + "' (scripts lists them)";
        return "reloaded " + std::to_string(n) + " script(s)" + (m_vm->errors().size() > errorsBefore ? " (with errors, above)" : "");
    }
    if (cmd == "lua") {
        if (arg.empty()) return "usage: lua <code>";
        const bool ok = m_vm->runString(arg, "console");
        report();
        return ok ? "ok" : "error (above)";
    }
    return {};
}

// ---------------------------------------------------------------- bindings

void ServerScripts::bind() {
    ScriptVM& vm = *m_vm;
    vm.registerFunction("kke", "log", [this](lua_State* L) {
        std::string out;
        for (int i = 1; i <= lua_gettop(L); ++i) {
            if (i > 1) out += ' ';
            size_t len = 0;
            const char* s = luaL_tolstring(L, i, &len);
            out.append(s, len);
            lua_pop(L, 1);
        }
        if (log) log("script " + std::filesystem::path(m_vm->currentSource()).filename().string() + ": " + out);
        return 0;
    });
    vm.registerFunction("kke", "time", [this](lua_State* L) { lua_pushnumber(L, m_time); return 1; });
    vm.registerFunction("kke", "dt", [this](lua_State* L) { lua_pushnumber(L, m_dt); return 1; });

    // net.* as in a game (docs/SCRIPTING.md), from the server's side.
    vm.registerFunction("net", "role", [](lua_State* L) { lua_pushstring(L, "server"); return 1; });
    vm.registerFunction("net", "isServer", [](lua_State* L) { lua_pushboolean(L, 1); return 1; });
    vm.registerFunction("net", "connected", [this](lua_State* L) { lua_pushboolean(L, m_net.running()); return 1; });
    vm.registerFunction("net", "playerId", [](lua_State* L) { lua_pushinteger(L, 0); return 1; }); // the server, as the host is
    // net.players() -> { { id = 1, name = "Kees", pos = Vec }, ... }
    vm.registerFunction("net", "players", [this](lua_State* L) {
        const std::vector<net::RemotePlayer> players = m_net.players(m_time);
        lua_createtable(L, int(players.size()), 0);
        for (size_t i = 0; i < players.size(); ++i) {
            lua_createtable(L, 0, 3);
            lua_pushinteger(L, players[i].id);
            lua_setfield(L, -2, "id");
            lua_pushstring(L, players[i].name.c_str());
            lua_setfield(L, -2, "name");
            if (players[i].hasState) {
                ScriptVM::pushVec3(L, players[i].state.position);
                lua_setfield(L, -2, "pos");
            }
            lua_rawseti(L, -2, lua_Integer(i + 1));
        }
        return 1;
    });
    // net.send(name, data [, player]): to every player, or to one.
    vm.registerFunction("net", "send", [this](lua_State* L) {
        size_t len = 0;
        const char* name = luaL_checklstring(L, 1, &len);
        if (len == 0 || len > 64 || std::memchr(name, 0, len)) return luaL_error(L, "net.send: the name must be 1-64 characters");
        std::string bytes, err;
        if (!ScriptVM::encodeValue(L, 2, bytes, err, 1024)) return luaL_error(L, "net.send: %s", err.c_str());
        const std::vector<uint8_t> payload = scriptMessage(name, len, bytes);
        if (lua_isnoneornil(L, 3)) {
            m_net.sendEvent(script_net::kScriptEvent, payload);
        } else {
            const lua_Integer to = luaL_checkinteger(L, 3);
            if (to < 1 || to > 255 || !m_names.count(uint8_t(to))) {
                lua_pushboolean(L, 0);
                return 1;
            }
            m_net.sendEventTo(uint8_t(to), script_net::kScriptEvent, payload);
        }
        lua_pushboolean(L, 1);
        return 1;
    });

    // server.*: what only a server script does.
    vm.registerFunction("server", "name", [this](lua_State* L) { lua_pushstring(L, m_services.serverName.c_str()); return 1; });
    vm.registerFunction("server", "say", [this](lua_State* L) {
        size_t len = 0;
        const char* text = luaL_checklstring(L, 1, &len);
        len = std::min(len, net::kMaxEventBytes);
        m_net.sendEvent(net::kEventServerMessage, std::vector<uint8_t>(text, text + len));
        return 0;
    });
    vm.registerFunction("server", "kick", [this](lua_State* L) {
        const lua_Integer id = luaL_checkinteger(L, 1);
        const std::string reason = luaL_optstring(L, 2, "kicked by the server");
        if (id < 1 || id > 255 || !m_names.count(uint8_t(id)) || !m_services.kick) {
            lua_pushboolean(L, 0);
            return 1;
        }
        m_services.kick(uint8_t(id), reason);
        lua_pushboolean(L, 1);
        return 1;
    });
    if (m_services.leaderboards) {
        // server.score(board, player, score): player is an id here or a name.
        vm.registerFunction("server", "score", [this](lua_State* L) {
            const std::string board = luaL_checkstring(L, 1);
            std::string player;
            if (lua_isinteger(L, 2)) {
                const lua_Integer id = lua_tointeger(L, 2);
                if (id < 1 || id > 255 || !m_names.count(uint8_t(id))) return luaL_error(L, "server.score: no player %d here", int(id));
                player = m_names[uint8_t(id)];
            } else {
                player = luaL_checkstring(L, 2);
            }
            const lua_Number score = luaL_checknumber(L, 3);
            if (!Leaderboard::validBoardName(board)) return luaL_error(L, "server.score: '%s' isn't a board name", board.c_str());
            if (player.empty() || score != score || score < -2147483648.0 || score > 2147483647.0)
                return luaL_error(L, "server.score: needs a player and a whole number score");
            lua_pushboolean(L, m_services.leaderboards->submit(board, player, int32_t(score), unixTime()));
            return 1;
        });
        // server.top(board [, n]) -> { { name = "Kees", score = 12 }, ... }
        vm.registerFunction("server", "top", [this](lua_State* L) {
            const std::string board = luaL_checkstring(L, 1);
            const auto n = size_t(std::clamp<lua_Integer>(luaL_optinteger(L, 2, 10), 1, 100));
            const auto entries = m_services.leaderboards->top(board, n);
            lua_createtable(L, int(entries.size()), 0);
            for (size_t i = 0; i < entries.size(); ++i) {
                lua_createtable(L, 0, 2);
                lua_pushstring(L, entries[i].name.c_str());
                lua_setfield(L, -2, "name");
                lua_pushinteger(L, entries[i].score);
                lua_setfield(L, -2, "score");
                lua_rawseti(L, -2, lua_Integer(i + 1));
            }
            return 1;
        });
    }
    bindPhysics();
    if (m_services.store) {
        m_store = std::make_unique<ScriptStore>(*m_services.store, m_services.game);
        m_store->bind(vm);
    }
}

void ServerScripts::bindPhysics() {
#if KKE_ENABLE_JOLT
    RigidWorld* w = m_services.world;
    if (!w) return;
    ScriptVM& vm = *m_vm;
    // physics.box{...} / physics.sphere{...}: the same options as in a game.
    auto spawn = [this, w](lua_State* L, bool sphere) {
        const std::string src = m_vm->currentSource();
        const size_t mine = static_cast<size_t>(std::count_if(m_bodies.begin(), m_bodies.end(), [&](const Body& b) { return b.source == src; }));
        if (mine >= maxBodiesPerScript) return luaL_error(L, "physics: this script already has %d bodies (maxBodiesPerScript)", int(maxBodiesPerScript));
        luaL_checktype(L, 1, LUA_TTABLE);
        RigidWorld::BodyDesc d;
        d.shape = sphere ? RigidWorld::Shape::Sphere : RigidWorld::Shape::Box;
        d.position = ScriptVM::fieldVec3(L, 1, "pos", glm::vec3(0, 2, 0));
        d.velocity = ScriptVM::fieldVec3(L, 1, "velocity", glm::vec3(0.0f));
        d.radius = std::max(0.02f, ScriptVM::fieldNumber(L, 1, "radius", 0.25f));
        d.halfExtents = glm::max(ScriptVM::fieldVec3(L, 1, "size", glm::vec3(0.5f)) * 0.5f, glm::vec3(0.01f));
        d.density = std::max(1.0f, ScriptVM::fieldNumber(L, 1, "density", 500.0f));
        d.friction = ScriptVM::fieldNumber(L, 1, "friction", 0.6f);
        d.restitution = ScriptVM::fieldNumber(L, 1, "bounce", 0.1f);
        d.material = uint32_t(std::max(0.0f, ScriptVM::fieldNumber(L, 1, "material", 0.0f)));
        const bool isStatic = ScriptVM::fieldBool(L, 1, "static", false);
        if (isStatic) d.motion = RigidWorld::Motion::Static;
        const glm::vec3 color = ScriptVM::fieldVec3(L, 1, "color", glm::vec3(0.8f));
        const RigidWorld::BodyId id = w->add(d);
        if (id == RigidWorld::kNoBody) return luaL_error(L, "physics: body limit reached");
        Body b{ id, src, 0, !isStatic };
        // Every client builds its copy (ScriptReplication.cpp on their side).
        b.netId = nextNetId();
        if (b.netId) {
            net::SpawnMsg m;
            m.id = b.netId;
            m.kind = script_net::kSpawnBody;
            m.desc = script_net::encode(script_net::BodySpawn{ sphere, isStatic, d.position, d.velocity, d.halfExtents, d.radius, d.density, d.friction,
                                                               d.restitution, d.material, color });
            m_net.spawn(m, true);
        }
        m_bodies.push_back(b);
        lua_pushinteger(L, lua_Integer(id));
        return 1;
    };
    auto owned = [this](lua_State* L, int idx) -> uint32_t {
        const auto id = uint32_t(luaL_checkinteger(L, idx));
        for (const Body& b : m_bodies)
            if (b.id == id) return id;
        luaL_error(L, "physics: %d is not a body spawned by a script", int(id));
        return 0;
    };
    vm.registerFunction("physics", "box", [spawn](lua_State* L) { return spawn(L, false); });
    vm.registerFunction("physics", "sphere", [spawn](lua_State* L) { return spawn(L, true); });
    vm.registerFunction("physics", "remove", [this, w, owned](lua_State* L) {
        const uint32_t id = owned(L, 1);
        for (const Body& b : m_bodies)
            if (b.id == id && b.netId) m_net.despawn(b.netId);
        w->remove(id);
        std::erase_if(m_bodies, [id](const Body& b) { return b.id == id; });
        return 0;
    });
    vm.registerFunction("physics", "position", [w, owned](lua_State* L) { ScriptVM::pushVec3(L, w->position(owned(L, 1))); return 1; });
    vm.registerFunction("physics", "velocity", [w, owned](lua_State* L) { ScriptVM::pushVec3(L, w->velocity(owned(L, 1))); return 1; });
    vm.registerFunction("physics", "setVelocity", [w, owned](lua_State* L) { w->setVelocity(owned(L, 1), ScriptVM::toVec3(L, 2)); return 0; });
    vm.registerFunction("physics", "impulse", [w, owned](lua_State* L) {
        const uint32_t id = owned(L, 1);
        w->addImpulse(id, ScriptVM::toVec3(L, 2), lua_istable(L, 3) ? ScriptVM::toVec3(L, 3) : w->position(id));
        return 0;
    });
    vm.registerFunction("physics", "raycast", [w](lua_State* L) {
        const RigidWorld::RayHit h = w->raycast(ScriptVM::toVec3(L, 1), ScriptVM::toVec3(L, 2), float(luaL_optnumber(L, 3, 100.0)));
        if (!h.hit) {
            lua_pushnil(L);
            return 1;
        }
        lua_createtable(L, 0, 5);
        ScriptVM::pushVec3(L, h.point);
        lua_setfield(L, -2, "pos");
        ScriptVM::pushVec3(L, h.normal);
        lua_setfield(L, -2, "normal");
        lua_pushnumber(L, h.distance);
        lua_setfield(L, -2, "distance");
        lua_pushinteger(L, lua_Integer(h.body));
        lua_setfield(L, -2, "body");
        lua_pushinteger(L, lua_Integer(h.material));
        lua_setfield(L, -2, "material");
        return 1;
    });
    vm.registerFunction("physics", "count", [this](lua_State* L) { lua_pushinteger(L, lua_Integer(m_bodies.size())); return 1; });
#endif
}

} // namespace kke::server
