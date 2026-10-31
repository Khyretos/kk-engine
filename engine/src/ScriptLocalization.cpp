#include "kke/ScriptLocalization.h"

#include "kke/ScriptVM.h"

#include <lauxlib.h>
#include <lua.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>

namespace kke {

namespace {

// A path inside `folder`, or "" (absolute paths and ".." are refused).
std::string inside(const std::string& folder, const std::string& relative) {
    namespace fs = std::filesystem;
    if (folder.empty() || relative.empty()) return {};
    const fs::path rel(relative);
    if (rel.is_absolute()) return {};
    for (const auto& part : rel)
        if (part == "..") return {};
    return (fs::path(folder) / rel).string();
}

void pushList(lua_State* L, const std::vector<std::string>& items) {
    lua_createtable(L, static_cast<int>(items.size()), 0);
    for (size_t i = 0; i < items.size(); ++i) {
        lua_pushlstring(L, items[i].data(), items[i].size());
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
}

} // namespace

void ScriptLocalization::bind(ScriptVM& vm) {
    // {name = "Ana", count = 3}: numbers are written the way the language writes numbers.
    auto readArgs = [this](lua_State* L, int idx) {
        LocArgs args;
        if (!lua_istable(L, idx)) return args;
        lua_pushnil(L);
        while (lua_next(L, idx)) {
            std::string key;
            if (lua_type(L, -2) == LUA_TSTRING) key = lua_tostring(L, -2);
            else if (lua_type(L, -2) == LUA_TNUMBER) key = std::to_string(static_cast<long long>(lua_tointeger(L, -2)) - 1); // {...}[1] is {0}
            if (!key.empty()) {
                switch (lua_type(L, -1)) {
                case LUA_TSTRING: args[key] = lua_tostring(L, -1); break;
                case LUA_TNUMBER: args[key] = m_loc.number(lua_tonumber(L, -1)); break;
                case LUA_TBOOLEAN: args[key] = lua_toboolean(L, -1) ? "true" : "false"; break;
                default: break;
                }
            }
            lua_pop(L, 1);
        }
        return args;
    };

    // loc.language([code]) -> the language, or with a code: true, or false when there is no table for it
    vm.registerFunction("loc", "language", [this, &vm](lua_State* L) {
        if (lua_isnoneornil(L, 1)) {
            lua_pushstring(L, m_loc.language().c_str());
            return 1;
        }
        const std::string code = luaL_checkstring(L, 1);
        const std::string before = m_loc.language();
        if (!m_loc.setLanguage(code)) {
            lua_pushboolean(L, 0);
            return 1;
        }
        if (onFonts) onFonts(m_loc.fonts());
        if (m_loc.language() != before) {
            const std::string now = m_loc.language();
            vm.callHookWith("LanguageChanged", [now](lua_State* H) {
                lua_newtable(H);
                lua_pushstring(H, now.c_str());
                lua_setfield(H, -2, "language");
                return 1;
            });
        }
        lua_pushboolean(L, 1);
        return 1;
    });
    vm.registerFunction("loc", "languages", [this](lua_State* L) {
        pushList(L, m_loc.languages());
        return 1;
    });
    vm.registerFunction("loc", "fallback", [this](lua_State* L) {
        if (!lua_isnoneornil(L, 1)) {
            m_loc.setFallback(luaL_checkstring(L, 1));
            return 0;
        }
        lua_pushstring(L, m_loc.fallback().c_str());
        return 1;
    });
    // loc.add("de", { menu = { play = "Spielen" } }) -> true, or false and what's wrong
    vm.registerFunction("loc", "add", [this](lua_State* L) {
        const std::string lang = luaL_checkstring(L, 1);
        luaL_checktype(L, 2, LUA_TTABLE);
        std::string error;
        if (!m_loc.addTable(lang, ScriptVM::toJson(L, 2), &error)) {
            lua_pushboolean(L, 0);
            lua_pushstring(L, error.c_str());
            return 2;
        }
        lua_pushboolean(L, 1);
        return 1;
    });
    // loc.load("lang/de.json"), loc.loadFolder("lang") -> true / how many, or false and why
    vm.registerFunction("loc", "load", [this](lua_State* L) {
        const std::string path = inside(m_folder, luaL_checkstring(L, 1));
        std::string error = "the file must be inside the game's folder";
        if (!path.empty() && m_loc.loadFile(path, &error)) {
            lua_pushboolean(L, 1);
            return 1;
        }
        lua_pushboolean(L, 0);
        lua_pushstring(L, error.c_str());
        return 2;
    });
    vm.registerFunction("loc", "loadFolder", [this](lua_State* L) {
        const std::string path = inside(m_folder, luaL_checkstring(L, 1));
        if (path.empty()) {
            lua_pushnil(L);
            lua_pushstring(L, "the folder must be inside the game's folder");
            return 2;
        }
        std::vector<std::string> errors;
        const size_t n = m_loc.loadFolder(path, &errors);
        lua_pushinteger(L, static_cast<lua_Integer>(n));
        if (errors.empty()) return 1;
        pushList(L, errors);
        return 2;
    });
    // loc.t(key [, args]) -> text
    vm.registerFunction("loc", "t", [this, readArgs](lua_State* L) {
        const std::string text = m_loc.text(luaL_checkstring(L, 1), readArgs(L, 2));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    // loc.plural(key, n [, args]) -> text
    vm.registerFunction("loc", "plural", [this, readArgs](lua_State* L) {
        const double n = luaL_checknumber(L, 2);
        if (!std::isfinite(n)) luaL_error(L, "loc.plural: n must be a number");
        const std::string text = m_loc.plural(luaL_checkstring(L, 1), n, readArgs(L, 3));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    vm.registerFunction("loc", "has", [this](lua_State* L) {
        lua_pushboolean(L, m_loc.has(luaL_checkstring(L, 1)));
        return 1;
    });
    vm.registerFunction("loc", "number", [this](lua_State* L) {
        const double v = luaL_checknumber(L, 1);
        const std::string text = m_loc.number(v, lua_isnoneornil(L, 2) ? -1 : static_cast<int>(luaL_checkinteger(L, 2)));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    vm.registerFunction("loc", "rtl", [this](lua_State* L) {
        lua_pushboolean(L, m_loc.rtl());
        return 1;
    });
    // loc.display(text): shaped and reordered for a renderer that draws left to right
    vm.registerFunction("loc", "display", [this](lua_State* L) {
        const std::string text = m_loc.display(luaL_checkstring(L, 1));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    vm.registerFunction("loc", "missing", [this](lua_State* L) {
        pushList(L, m_loc.missing());
        return 1;
    });
    vm.registerFunction("loc", "untranslated", [this](lua_State* L) {
        pushList(L, m_loc.untranslated(luaL_checkstring(L, 1)));
        return 1;
    });
    vm.registerFunction("loc", "fonts", [this](lua_State* L) {
        pushList(L, m_loc.fonts());
        return 1;
    });
}

} // namespace kke
