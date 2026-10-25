#include "kke/ScriptStore.h"

#include <lauxlib.h>
#include <lua.h>

#include <cmath>
#include <utility>

namespace kke {

ScriptStore::ScriptStore(std::string url, const std::string& game) : m_url(std::move(url)), m_collection(collectionFor(game)), m_game(game) {}

ScriptStore::ScriptStore(storage::Store& shared, const std::string& game)
    : m_collection(collectionFor(game)), m_store(&shared), m_tried(true), m_game(game) {}

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

SaveSlots* ScriptStore::saveSlots() {
    if (!m_saves)
        if (storage::Store* s = store()) m_saves = std::make_unique<SaveSlots>(*s, m_game);
    return m_saves.get();
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
    bindSaves(vm);
}

namespace {

constexpr const char* kMigrations = "kke.saves.migrations";

std::string checkSlot(lua_State* L, int index, const char* fn) {
    size_t len = 0;
    const char* p = nullptr;
    if (lua_type(L, index) == LUA_TNUMBER) p = lua_tolstring(L, index, &len); // saves.read(1) is slot "1"
    else p = luaL_checklstring(L, index, &len);
    std::string slot(p, len);
    if (!SaveSlots::validSlot(slot)) luaL_error(L, "saves.%s: a slot name is 1-64 letters, digits, spaces, _ - or .", fn);
    return slot;
}

void pushInfo(lua_State* L, const SaveInfo& info) {
    lua_createtable(L, 0, 6);
    lua_pushlstring(L, info.slot.data(), info.slot.size());
    lua_setfield(L, -2, "slot");
    lua_pushlstring(L, info.title.data(), info.title.size());
    lua_setfield(L, -2, "title");
    lua_pushinteger(L, lua_Integer(info.savedAtMs / 1000));
    lua_setfield(L, -2, "time");
    lua_pushinteger(L, lua_Integer(info.version));
    lua_setfield(L, -2, "version");
    lua_pushnumber(L, info.playSeconds);
    lua_setfield(L, -2, "playtime");
    lua_pushinteger(L, lua_Integer(info.dataBytes));
    lua_setfield(L, -2, "bytes");
}

} // namespace

void ScriptStore::bindSaves(ScriptVM& vm) {
    auto open = [this](lua_State* L, const char* fn) -> SaveSlots& {
        SaveSlots* s = saveSlots();
        if (!s) luaL_error(L, "saves.%s: can't open the save (%s): %s", fn, m_url.c_str(), m_error.c_str());
        return *s;
    };

    // saves.version([n]) -> the version saves.write stamps (sets it when given).
    vm.registerFunction("saves", "version", [this, open](lua_State* L) {
        SaveSlots& s = open(L, "version");
        if (!lua_isnoneornil(L, 1)) {
            const lua_Integer v = luaL_checkinteger(L, 1);
            if (v < 1 || v > 1000000) return luaL_error(L, "saves.version: a version is a whole number from 1");
            s.setVersion(uint32_t(v));
        }
        lua_pushinteger(L, lua_Integer(s.version()));
        return 1;
    });
    // saves.migrate(from, fn): fn(data) brings a version-`from` save to from + 1
    // (it returns the new data, or changes the table and returns nothing).
    vm.registerFunction("saves", "migrate", [](lua_State* L) {
        const lua_Integer from = luaL_checkinteger(L, 1);
        if (from < 1) return luaL_error(L, "saves.migrate: the version it starts from is a whole number from 1");
        luaL_checktype(L, 2, LUA_TFUNCTION);
        if (lua_getfield(L, LUA_REGISTRYINDEX, kMigrations) != LUA_TTABLE) {
            lua_pop(L, 1);
            lua_newtable(L);
            lua_pushvalue(L, -1);
            lua_setfield(L, LUA_REGISTRYINDEX, kMigrations);
        }
        lua_pushvalue(L, 2);
        lua_rawseti(L, -2, from);
        lua_pop(L, 1);
        return 0;
    });
    // saves.write(slot, data [, {title =, playtime =}]) -> true, or false and why.
    vm.registerFunction("saves", "write", [open](lua_State* L) {
        const std::string slot = checkSlot(L, 1, "write");
        if (lua_isnoneornil(L, 2)) return luaL_error(L, "saves.write('%s'): nothing to save (saves.remove clears a slot)", slot.c_str());
        std::string title;
        double play = 0.0;
        if (lua_istable(L, 3)) {
            if (lua_getfield(L, 3, "title") == LUA_TSTRING) title = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (lua_getfield(L, 3, "playtime") == LUA_TNUMBER) play = lua_tonumber(L, -1);
            lua_pop(L, 1);
        } else if (!lua_isnoneornil(L, 3)) {
            return luaL_error(L, "saves.write('%s'): the third argument is a table { title =, playtime = }", slot.c_str());
        }
        std::string bytes, err;
        if (!ScriptVM::encodeValue(L, 2, bytes, err, kMaxSaveBytes)) return luaL_error(L, "saves.write('%s'): %s", slot.c_str(), err.c_str());
        SaveSlots& s = open(L, "write");
        if (s.write(slot, bytes, title, play, &err)) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushboolean(L, 0);
        lua_pushstring(L, err.c_str());
        return 2;
    });
    // saves.read(slot) -> data, info; or nil and why. An older save is brought
    // up to date by the saves.migrate steps; a newer one is refused.
    vm.registerFunction("saves", "read", [open](lua_State* L) {
        const std::string slot = checkSlot(L, 1, "read");
        SaveSlots& s = open(L, "read");
        auto none = [L](const std::string& why) {
            lua_pushnil(L);
            lua_pushstring(L, why.c_str());
            return 2;
        };
        std::string err;
        std::optional<SaveFile> f = s.read(slot, &err);
        if (!f) return none(err);
        if (f->info.version > s.version())
            return none("'" + slot + "' was saved by a newer version of the game (save version " + std::to_string(f->info.version) +
                        ", this game " + std::to_string(s.version()) + ")");
        lua_settop(L, 1);
        if (!ScriptVM::decodeValue(L, f->data, err)) return none("'" + slot + "' is damaged: " + err);
        for (uint32_t v = f->info.version; v < s.version(); ++v) {
            const int data = lua_gettop(L);
            if (lua_getfield(L, LUA_REGISTRYINDEX, kMigrations) != LUA_TTABLE || lua_rawgeti(L, -1, v) != LUA_TFUNCTION) {
                lua_settop(L, 1);
                return none("'" + slot + "' is a version " + std::to_string(f->info.version) + " save and there's no saves.migrate(" +
                            std::to_string(v) + ", ...) to bring it to version " + std::to_string(s.version()));
            }
            lua_pushvalue(L, data);
            lua_call(L, 1, 1); // a migration's error is the script's error, naming its line
            if (lua_isnil(L, -1)) lua_pop(L, 1);
            else lua_replace(L, data);
            lua_settop(L, data);
        }
        pushInfo(L, f->info);
        return 2;
    });
    // saves.list() -> every slot's info, newest first.
    vm.registerFunction("saves", "list", [open](lua_State* L) {
        SaveSlots& s = open(L, "list");
        const std::vector<SaveInfo> all = s.list();
        lua_createtable(L, int(all.size()), 0);
        for (size_t i = 0; i < all.size(); ++i) {
            pushInfo(L, all[i]);
            lua_rawseti(L, -2, lua_Integer(i + 1));
        }
        return 1;
    });
    vm.registerFunction("saves", "exists", [open](lua_State* L) {
        const std::string slot = checkSlot(L, 1, "exists");
        lua_pushboolean(L, open(L, "exists").exists(slot));
        return 1;
    });
    // saves.remove(slot) -> true, or false and why (an empty slot is fine).
    vm.registerFunction("saves", "remove", [open](lua_State* L) {
        const std::string slot = checkSlot(L, 1, "remove");
        std::string err;
        if (open(L, "remove").remove(slot, &err)) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushboolean(L, 0);
        lua_pushstring(L, err.c_str());
        return 2;
    });
}

} // namespace kke
