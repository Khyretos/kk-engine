#include "kke/ScriptTables.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <utility>

namespace kke {

namespace {

constexpr const char* kTableMeta = "kke.NetTable";
constexpr const char* kViewMeta = "kke.NetView";

void pushDecoded(lua_State* L, const std::string& bytes) {
    std::string err;
    if (bytes.empty() || !ScriptVM::decodeValue(L, bytes, err)) lua_pushnil(L);
}

// Moves the functions registered in global `hidden` into a metatable
// (registry[meta]) as its __index, and takes the global away again.
void makeMeta(lua_State* L, const char* hidden, const char* meta) {
    lua_newtable(L);
    lua_getglobal(L, hidden);
    lua_setfield(L, -2, "__index");
    lua_setfield(L, LUA_REGISTRYINDEX, meta);
    lua_pushnil(L);
    lua_setglobal(L, hidden);
}

} // namespace

ScriptTables::ScriptTables(ScriptVM& vm, Link link, net::TableLimits limits)
    : m_vm(vm), m_link(std::move(link)), m_server(limits), m_client(limits) {}

ScriptTables::~ScriptTables() {
    for (const auto& [id, v] : m_views) m_vm.unref(v.ref);
}

bool ScriptTables::scalar(lua_State* L, int index, std::string& out) {
    std::string err;
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
    case LUA_TSTRING: return ScriptVM::encodeValue(L, index, out, err, 256);
    case LUA_TNUMBER: {
        if (!lua_isinteger(L, index)) {
            // 3.0 is 3: filters on numbers mustn't depend on how one was made.
            const double d = lua_tonumber(L, index);
            if (std::floor(d) == d && std::fabs(d) < 9007199254740992.0) {
                lua_pushinteger(L, static_cast<lua_Integer>(d));
                const bool ok = ScriptVM::encodeValue(L, -1, out, err, 16);
                lua_pop(L, 1);
                return ok;
            }
        }
        return ScriptVM::encodeValue(L, index, out, err, 16);
    }
    default: return false;
    }
}

bool ScriptTables::keyFromLua(lua_State* L, int index, std::string& out, const char* what) {
    const int t = lua_type(L, index);
    const bool whole = t == LUA_TNUMBER && (lua_isinteger(L, index) || std::floor(lua_tonumber(L, index)) == lua_tonumber(L, index));
    if ((t != LUA_TSTRING && !whole) || !scalar(L, index, out)) {
        luaL_error(L, "%s: a key is a whole number or a string", what);
        return false;
    }
    return true;
}

bool ScriptTables::rowFromLua(lua_State* L, int index, net::TableRow& row, std::string& error) const {
    index = lua_absindex(L, index);
    if (!ScriptVM::encodeValue(L, index, row.bytes, error, m_server.limits().maxRowBytes)) return false;
    if (!lua_istable(L, index)) return true;
    lua_pushnil(L);
    while (lua_next(L, index)) {
        std::string value;
        if (lua_type(L, -2) == LUA_TSTRING && scalar(L, -1, value)) row.fields[lua_tostring(L, -2)] = std::move(value);
        lua_pop(L, 1);
    }
    return true;
}

std::string ScriptTables::tableArg(lua_State* L, const char* what) const {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "name");
    std::string name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);
    if (name.empty()) luaL_error(L, "%s: call it as tbl:%s(...) on what net.table gave", what, what);
    if (!m_link.serves || !m_link.serves())
        luaL_error(L, "%s: only the host or server changes synced tables (players watch them with net.watch)", what);
    return name;
}

uint32_t ScriptTables::viewArg(lua_State* L, const char* what) const {
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "id");
    const lua_Integer id = lua_isinteger(L, -1) ? lua_tointeger(L, -1) : 0;
    lua_pop(L, 1);
    if (id <= 0 || !m_views.count(uint32_t(id))) luaL_error(L, "%s: this watch was stopped (or isn't one)", what);
    return uint32_t(id);
}

void ScriptTables::bind() {
    // Methods of what net.table gives.
    m_vm.registerFunction("__kke_nettable", "set", [this](lua_State* L) {
        const std::string name = tableArg(L, "set");
        std::string key, err;
        keyFromLua(L, 2, key, "set");
        net::TableRow row;
        if (!rowFromLua(L, 3, row, err)) return luaL_error(L, "%s:set: %s", name.c_str(), err.c_str());
        if (!m_server.set(name, key, std::move(row), &err)) return luaL_error(L, "%s:set: %s", name.c_str(), err.c_str());
        return 0;
    });
    m_vm.registerFunction("__kke_nettable", "get", [this](lua_State* L) {
        const std::string name = tableArg(L, "get");
        std::string key;
        keyFromLua(L, 2, key, "get");
        const net::TableRow* row = m_server.get(name, key);
        if (row) pushDecoded(L, row->bytes);
        else lua_pushnil(L);
        return 1;
    });
    m_vm.registerFunction("__kke_nettable", "remove", [this](lua_State* L) {
        const std::string name = tableArg(L, "remove");
        std::string key;
        keyFromLua(L, 2, key, "remove");
        lua_pushboolean(L, m_server.remove(name, key));
        return 1;
    });
    m_vm.registerFunction("__kke_nettable", "rows", [this](lua_State* L) {
        const std::string name = tableArg(L, "rows");
        const auto* rows = m_server.rows(name);
        lua_createtable(L, 0, rows ? int(rows->size()) : 0);
        if (rows)
            for (const auto& [key, row] : *rows) {
                pushDecoded(L, key);
                pushDecoded(L, row.bytes);
                lua_settable(L, -3);
            }
        return 1;
    });
    m_vm.registerFunction("__kke_nettable", "count", [this](lua_State* L) {
        const auto* rows = m_server.rows(tableArg(L, "count"));
        lua_pushinteger(L, rows ? lua_Integer(rows->size()) : 0);
        return 1;
    });
    m_vm.registerFunction("__kke_nettable", "clear", [this](lua_State* L) {
        m_server.clear(tableArg(L, "clear"));
        return 0;
    });
    // Methods of what net.watch gives.
    m_vm.registerFunction("__kke_netview", "rows", [this](lua_State* L) {
        const auto* rows = m_client.rows(viewArg(L, "rows"));
        lua_createtable(L, 0, rows ? int(rows->size()) : 0);
        if (rows)
            for (const auto& [key, row] : *rows) {
                pushDecoded(L, key);
                pushDecoded(L, row);
                lua_settable(L, -3);
            }
        return 1;
    });
    m_vm.registerFunction("__kke_netview", "get", [this](lua_State* L) {
        const uint32_t id = viewArg(L, "get");
        std::string key;
        keyFromLua(L, 2, key, "get");
        const auto* rows = m_client.rows(id);
        const auto it = rows->find(key); // a live view always has its rows
        if (it != rows->end()) pushDecoded(L, it->second);
        else lua_pushnil(L);
        return 1;
    });
    m_vm.registerFunction("__kke_netview", "ready", [this](lua_State* L) {
        lua_pushboolean(L, m_client.ready(viewArg(L, "ready")));
        return 1;
    });
    m_vm.registerFunction("__kke_netview", "count", [this](lua_State* L) {
        const auto* rows = m_client.rows(viewArg(L, "count"));
        lua_pushinteger(L, rows ? lua_Integer(rows->size()) : 0);
        return 1;
    });
    m_vm.registerFunction("__kke_netview", "stop", [this](lua_State* L) {
        const uint32_t id = viewArg(L, "stop");
        m_client.stop(id);
        m_vm.unref(m_views[id].ref);
        m_views.erase(id);
        return 0;
    });
    lua_State* L0 = m_vm.state();
    makeMeta(L0, "__kke_nettable", kTableMeta);
    makeMeta(L0, "__kke_netview", kViewMeta);

    // net.table(name) -> the table, on the machine that keeps them.
    m_vm.registerFunction("net", "table", [this](lua_State* L) {
        size_t len = 0;
        const char* name = luaL_checklstring(L, 1, &len);
        if (len == 0 || len > m_server.limits().maxNameBytes)
            return luaL_error(L, "net.table: a name is 1-%d characters", int(m_server.limits().maxNameBytes));
        if (!m_link.serves || !m_link.serves())
            return luaL_error(L, "net.table: only the host or server keeps synced tables (players watch them with net.watch)");
        m_owners.try_emplace(std::string(name, len), m_vm.currentSource());
        lua_createtable(L, 0, 1);
        lua_pushlstring(L, name, len);
        lua_setfield(L, -2, "name");
        lua_getfield(L, LUA_REGISTRYINDEX, kTableMeta);
        lua_setmetatable(L, -2);
        return 1;
    });
    // net.watch(name [, { field = value, ... }] [, function(event, key, row, old) end]) -> view
    m_vm.registerFunction("net", "watch", [this](lua_State* L) {
        const std::string name = luaL_checkstring(L, 1);
        int fn = 2;
        net::TableFilter filter;
        if (lua_istable(L, 2)) {
            fn = 3;
            lua_pushnil(L);
            while (lua_next(L, 2)) {
                std::string value;
                if (lua_type(L, -2) != LUA_TSTRING || !scalar(L, -1, value))
                    return luaL_error(L, "net.watch: a filter is { field = value } with plain values (numbers, text, true/false)");
                filter.emplace_back(lua_tostring(L, -2), std::move(value));
                lua_pop(L, 1);
            }
        } else if (!lua_isnoneornil(L, 2) && !lua_isfunction(L, 2)) {
            return luaL_error(L, "net.watch: the second argument is a filter table or the callback");
        }
        if (!lua_isnoneornil(L, fn)) luaL_checktype(L, fn, LUA_TFUNCTION);
        const uint32_t id = m_client.watch(name, filter);
        if (!id)
            return luaL_error(L, "net.watch: '%s' isn't a table name, or the filter has more than %d fields", name.c_str(),
                              int(m_server.limits().maxFilterFields));
        m_views[id] = { lua_isnoneornil(L, fn) ? 0 : m_vm.ref(L, fn), m_vm.currentSource() };
        lua_createtable(L, 0, 2);
        lua_pushinteger(L, lua_Integer(id));
        lua_setfield(L, -2, "id");
        lua_pushstring(L, name.c_str());
        lua_setfield(L, -2, "table");
        lua_getfield(L, LUA_REGISTRYINDEX, kViewMeta);
        lua_setmetatable(L, -2);
        return 1;
    });
}

void ScriptTables::received(uint16_t kind, int fromPlayer, const std::vector<uint8_t>& payload) {
    if (kind == net::kTableRows) {
        if (m_source == Source::Remote) m_client.received(payload);
        return;
    }
    if ((kind == net::kTableSubscribe || kind == net::kTableUnsubscribe) && m_link.serves && m_link.serves())
        m_server.received(fromPlayer, kind, payload);
}

void ScriptTables::playerLeft(int player) { m_server.dropPlayer(player); }

void ScriptTables::flush() {
    for (auto& [player, bytes] : m_server.flush()) {
        if (player == net::TableServer::kLocal) m_client.received(bytes);
        else if (m_link.sendToPlayer) m_link.sendToPlayer(player, bytes);
    }
}

void ScriptTables::update() {
    const bool serves = m_link.serves && m_link.serves();
    const Source want = serves ? Source::Local : (m_link.online && m_link.online()) ? Source::Remote : Source::None;
    if (want != m_source) {
        if (m_source == Source::Local) m_server.dropPlayer(net::TableServer::kLocal);
        m_client.sourceLost();
        m_source = want;
        if (want != Source::None) m_client.resubscribe();
    }
    for (auto& [kind, bytes] : m_client.takeSends()) {
        if (m_source == Source::Local) m_server.received(net::TableServer::kLocal, kind, bytes);
        else if (m_source == Source::Remote && m_link.sendToServer) m_link.sendToServer(kind, bytes);
    }
    if (serves) flush();
    for (const net::TableClient::Change& c : m_client.takeChanges()) {
        const auto v = m_views.find(c.watch);
        if (v == m_views.end() || !v->second.ref) continue;
        const View view = v->second; // the callback may stop it
        m_vm.callRef(view.ref, view.source, [&](lua_State* L) {
            using K = net::TableClient::Change::Kind;
            static constexpr const char* kNames[] = { "insert", "update", "delete", "ready", "error" };
            lua_pushstring(L, kNames[static_cast<int>(c.kind)]);
            if (c.kind == K::Ready) return 1;
            if (c.kind == K::Error) {
                lua_pushnil(L);
                lua_pushlstring(L, c.row.data(), c.row.size());
                return 3;
            }
            pushDecoded(L, c.key);
            pushDecoded(L, c.row);
            pushDecoded(L, c.old);
            return 4;
        });
    }
}

void ScriptTables::release(const std::string& source) {
    for (auto it = m_views.begin(); it != m_views.end();) {
        if (it->second.source != source) {
            ++it;
            continue;
        }
        m_client.stop(it->first);
        m_vm.unref(it->second.ref);
        it = m_views.erase(it);
    }
    for (auto it = m_owners.begin(); it != m_owners.end();) {
        if (it->second != source) {
            ++it;
            continue;
        }
        m_server.clear(it->first);
        it = m_owners.erase(it);
    }
}

} // namespace kke
