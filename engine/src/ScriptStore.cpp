#include "kke/ScriptStore.h"

#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <utility>

namespace kke {

ScriptStore::ScriptStore(std::string url, const std::string& game) : m_url(std::move(url)), m_collection(collectionFor(game)) {}

ScriptStore::ScriptStore(storage::Store& shared, const std::string& game) : m_collection(collectionFor(game)), m_store(&shared), m_tried(true) {}

ScriptStore::~ScriptStore() = default;

std::string ScriptStore::collectionFor(const std::string& game) {
    std::string c = "lua.";
    for (char ch : game) {
        const char l = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
        const bool ok = (l >= 'a' && l <= 'z') || (l >= '0' && l <= '9') || l == '_' || l == '-' || l == '.';
        if (c.size() < storage::Store::kMaxCollection) c += ok ? l : '_';
    }
    if (c.size() == 4) c += "game";
    return c;
}

storage::Store* ScriptStore::store() {
    if (!m_tried) {
        m_tried = true;
        m_owned = storage::openStore(m_url, &m_error);
        m_store = m_owned.get();
    }
    return m_store;
}

namespace {

// The key argument: text of 1-256 bytes, or a Lua error naming the call.
std::string checkKey(lua_State* L, int index, const char* fn) {
    size_t len = 0;
    const char* k = luaL_checklstring(L, index, &len);
    if (len == 0 || len > storage::Store::kMaxKey) luaL_error(L, "store.%s: the name must be 1-%d characters", fn, int(storage::Store::kMaxKey));
    return std::string(k, len);
}

} // namespace

void ScriptStore::bind(ScriptVM& vm) {
    // A store that can't be opened is a Lua error on use (the script's
    // author sees it in the Scripts panel), not a crash or a silent no-op.
    auto open = [this](lua_State* L, const char* fn) -> storage::Store& {
        storage::Store* s = store();
        if (!s) luaL_error(L, "store.%s: can't open the save (%s): %s", fn, m_url.c_str(), m_error.c_str());
        return *s;
    };
    auto result = [](lua_State* L, bool ok, storage::Store& s) {
        lua_pushboolean(L, ok);
        if (ok) return 1;
        lua_pushstring(L, s.lastError().c_str());
        return 2;
    };

    vm.registerFunction("store", "save", [this, open, result](lua_State* L) {
        const std::string key = checkKey(L, 1, "save");
        storage::Store& s = open(L, "save");
        if (lua_isnoneornil(L, 2)) return result(L, s.erase(m_collection, key), s);
        std::string bytes, err;
        if (!ScriptVM::encodeValue(L, 2, bytes, err, kMaxValueBytes)) return luaL_error(L, "store.save('%s'): %s", key.c_str(), err.c_str());
        return result(L, s.put(m_collection, key, bytes), s);
    });
    vm.registerFunction("store", "load", [this, open](lua_State* L) {
        const std::string key = checkKey(L, 1, "load");
        storage::Store& s = open(L, "load");
        lua_settop(L, 2); // the default (nil when not given) at 2
        const std::optional<std::string> bytes = s.get(m_collection, key);
        std::string err;
        if (!bytes || !ScriptVM::decodeValue(L, *bytes, err)) lua_pushvalue(L, 2);
        return 1;
    });
    vm.registerFunction("store", "remove", [this, open, result](lua_State* L) {
        const std::string key = checkKey(L, 1, "remove");
        storage::Store& s = open(L, "remove");
        return result(L, s.erase(m_collection, key), s);
    });
    vm.registerFunction("store", "keys", [this, open](lua_State* L) {
        size_t len = 0;
        const char* p = luaL_optlstring(L, 1, "", &len);
        storage::Store& s = open(L, "keys");
        const std::vector<storage::Store::Item> items = s.list(m_collection, std::string(p, len), kMaxKeys);
        lua_createtable(L, int(items.size()), 0);
        for (size_t i = 0; i < items.size(); ++i) {
            lua_pushlstring(L, items[i].key.data(), items[i].key.size());
            lua_rawseti(L, -2, lua_Integer(i + 1));
        }
        return 1;
    });
    // store.add(key, n = 1): all in one transaction, so two adds never lose
    // one. Whole numbers stay whole (Lua's own + decides).
    vm.registerFunction("store", "add", [this, open](lua_State* L) {
        const std::string key = checkKey(L, 1, "add");
        if (lua_isnoneornil(L, 2)) lua_pushinteger(L, 1);
        else {
            luaL_checknumber(L, 2);
            lua_pushvalue(L, 2);
        }
        const int delta = lua_gettop(L);
        if (lua_type(L, delta) != LUA_TNUMBER) return luaL_error(L, "store.add('%s'): the amount must be a number", key.c_str());
        storage::Store& s = open(L, "add");
        std::string problem;
        const bool ok = s.transaction([&] {
            if (const std::optional<std::string> bytes = s.get(m_collection, key)) {
                std::string err;
                if (!ScriptVM::decodeValue(L, *bytes, err)) {
                    problem = "what's saved there can't be read";
                    return false;
                }
                if (lua_type(L, -1) != LUA_TNUMBER) {
                    lua_pop(L, 1);
                    problem = "what's saved there isn't a number";
                    return false;
                }
            } else if (!s.lastError().empty()) {
                return false;
            } else {
                lua_pushinteger(L, 0);
            }
            lua_pushvalue(L, delta);
            lua_arith(L, LUA_OPADD); // leaves the new count on the stack
            if (lua_isnumber(L, -1) && !std::isfinite(lua_tonumber(L, -1))) {
                lua_pop(L, 1);
                problem = "the count got too big";
                return false;
            }
            std::string out, err;
            if (!ScriptVM::encodeValue(L, -1, out, err, kMaxValueBytes)) {
                lua_pop(L, 1);
                problem = err;
                return false;
            }
            if (!s.put(m_collection, key, out)) {
                lua_pop(L, 1);
                return false;
            }
            return true;
        });
        if (!ok) return luaL_error(L, "store.add('%s'): %s", key.c_str(), problem.empty() ? s.lastError().c_str() : problem.c_str());
        return 1; // the new count, left by the transaction
    });
}

} // namespace kke
