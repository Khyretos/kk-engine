// loc.*, subtitles.* and accessibility.*: words, captions and access from
// Lua (docs/LOCALIZATION.md, docs/ACCESSIBILITY.md).
#include "kke/Accessibility.h"
#include "kke/Localization.h"
#include "kke/Subtitles.h"
#include "kke/modules/ScriptModule.h"

#include <lauxlib.h>
#include <lua.h>

#include <filesystem>
#include <string>

namespace kke {

namespace {

// {name = "Kees", points = 12345} -> placeholders. A number is written the way the
// player's language writes numbers (12.345, 12,345 or ١٢٬٣٤٥).
Localization::Args readArgs(lua_State* L, int index) {
    Localization::Args args;
    if (lua_isnoneornil(L, index)) return args;
    luaL_checktype(L, index, LUA_TTABLE);
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const std::string key = lua_tostring(L, -2);
            if (lua_type(L, -1) == LUA_TNUMBER) {
                if (lua_isinteger(L, -1)) args.emplace_back(key, Localization::get().number(static_cast<double>(lua_tointeger(L, -1)), 0));
                else args.emplace_back(key, Localization::get().number(lua_tonumber(L, -1)));
            } else {
                size_t length = 0;
                const char* text = luaL_tolstring(L, -1, &length); // pushes a string
                args.emplace_back(key, std::string(text, length));
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }
    return args;
}

} // namespace

void ScriptModule::bindLocale() {
    ScriptVM& vm = *m_vm;

    // The game's own words: <game folder>/locales/*.json, on top of the engine's.
    {
        std::error_code ec;
        const std::filesystem::path own = std::filesystem::path(gameFolder()) / "locales";
        if (!gameFolder().empty() && std::filesystem::is_directory(own, ec)) {
            std::string errors;
            const int n = Localization::get().loadFolder(own.string(), &errors);
            log("", "the game's own languages: " + std::to_string(n) + " file(s) in " + own.string());
            if (!errors.empty()) log("", errors);
        }
    }

    // loc.tr(key [, args]): the string in the player's language; {name} filled from args.
    vm.registerFunction("loc", "tr", [](lua_State* L) {
        const std::string key = luaL_checkstring(L, 1);
        lua_pushstring(L, Localization::get().tr(key, readArgs(L, 2)).c_str());
        return 1;
    });
    // loc.plural(key, n [, args]): the form for n ("1 coin", "2 coins"); {n} is n.
    vm.registerFunction("loc", "plural", [](lua_State* L) {
        const std::string key = luaL_checkstring(L, 1);
        const double n = luaL_checknumber(L, 2);
        lua_pushstring(L, Localization::get().plural(key, n, readArgs(L, 3)).c_str());
        return 1;
    });
    // loc.select(key, choice [, args]): the form named by choice ("male", "female", ...), else "other".
    vm.registerFunction("loc", "select", [](lua_State* L) {
        const std::string key = luaL_checkstring(L, 1);
        const std::string choice = luaL_checkstring(L, 2);
        lua_pushstring(L, Localization::get().select(key, choice, readArgs(L, 3)).c_str());
        return 1;
    });
    vm.registerFunction("loc", "has", [](lua_State* L) {
        lua_pushboolean(L, Localization::get().has(luaL_checkstring(L, 1)));
        return 1;
    });
    // loc.language(): the code in use ("nl"). loc.set(code): switches; false when there is no such language.
    vm.registerFunction("loc", "language", [](lua_State* L) {
        lua_pushstring(L, Localization::get().language().c_str());
        return 1;
    });
    vm.registerFunction("loc", "set", [](lua_State* L) {
        lua_pushboolean(L, Localization::get().setLanguage(luaL_checkstring(L, 1)));
        return 1;
    });
    // loc.languages(): { {code="nl", name="Nederlands", rtl=false, strings=120}, ... }, for a language menu.
    vm.registerFunction("loc", "languages", [](lua_State* L) {
        const auto list = Localization::get().languages();
        lua_createtable(L, static_cast<int>(list.size()), 0);
        int i = 1;
        for (const Localization::Language& l : list) {
            lua_createtable(L, 0, 4);
            lua_pushstring(L, l.code.c_str());
            lua_setfield(L, -2, "code");
            lua_pushstring(L, l.name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, l.rtl);
            lua_setfield(L, -2, "rtl");
            lua_pushinteger(L, static_cast<lua_Integer>(l.strings));
            lua_setfield(L, -2, "strings");
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });
    vm.registerFunction("loc", "rtl", [](lua_State* L) {
        lua_pushboolean(L, Localization::get().rtl());
        return 1;
    });
    // loc.number(n [, decimals]): n written the player's way.
    vm.registerFunction("loc", "number", [](lua_State* L) {
        const double n = luaL_checknumber(L, 1);
        const int decimals = static_cast<int>(luaL_optinteger(L, 2, -1));
        lua_pushstring(L, Localization::get().number(n, decimals).c_str());
        return 1;
    });
    // loc.display(text): right-to-left text in the order a screen draws it. The ui.* functions
    // already do this: use it only for text that goes anywhere else.
    vm.registerFunction("loc", "display", [](lua_State* L) {
        lua_pushstring(L, Localization::get().display(luaL_checkstring(L, 1)).c_str());
        return 1;
    });
    // loc.load(path): one language file, or every one in a folder (relative to the game's folder first).
    // Returns how many loaded, or nil and why not.
    vm.registerFunction("loc", "load", [this](lua_State* L) {
        namespace fs = std::filesystem;
        std::string path = luaL_checkstring(L, 1);
        std::error_code ec;
        if (fs::path(path).is_relative() && !gameFolder().empty() && fs::exists(fs::path(gameFolder()) / path, ec)) path = (fs::path(gameFolder()) / path).string();
        std::string error;
        int loaded = 0;
        if (fs::is_directory(path, ec)) loaded = Localization::get().loadFolder(path, &error);
        else loaded = Localization::get().loadFile(path, &error) ? 1 : 0;
        if (loaded == 0 && !error.empty()) {
            lua_pushnil(L);
            lua_pushstring(L, error.c_str());
            return 2;
        }
        lua_pushinteger(L, loaded);
        return 1;
    });
    // loc.missing(): the keys asked for that no language had: what is left to write.
    vm.registerFunction("loc", "missing", [](lua_State* L) {
        const auto keys = Localization::get().missingKeys();
        lua_createtable(L, static_cast<int>(keys.size()), 0);
        int i = 1;
        for (const std::string& k : keys) {
            lua_pushstring(L, k.c_str());
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });

    // subtitles.say(text [, {speaker=, duration=, kind="speech"|"sound"|"note"}]) -> id.
    // The text is translated like any other (English text with a translation, or "@key").
    vm.registerFunction("subtitles", "say", [](lua_State* L) {
        const std::string text = luaL_checkstring(L, 1);
        Subtitles::Options o;
        if (!lua_isnoneornil(L, 2)) {
            luaL_checktype(L, 2, LUA_TTABLE);
            lua_getfield(L, 2, "speaker");
            if (lua_isstring(L, -1)) o.speaker = lua_tostring(L, -1);
            lua_pop(L, 1);
            lua_getfield(L, 2, "duration");
            if (lua_isnumber(L, -1)) o.duration = static_cast<float>(lua_tonumber(L, -1));
            lua_pop(L, 1);
            lua_getfield(L, 2, "kind");
            if (lua_isstring(L, -1)) o.kind = lua_tostring(L, -1);
            lua_pop(L, 1);
            if (o.kind != "speech" && o.kind != "sound" && o.kind != "note") return luaL_error(L, "subtitles.say: kind must be \"speech\", \"sound\" or \"note\"");
        }
        lua_pushinteger(L, Subtitles::get().say(text, o));
        return 1;
    });
    vm.registerFunction("subtitles", "remove", [](lua_State* L) {
        Subtitles::get().remove(static_cast<int>(luaL_checkinteger(L, 1)));
        return 0;
    });
    vm.registerFunction("subtitles", "clear", [](lua_State*) {
        Subtitles::get().clear();
        return 0;
    });
    vm.registerFunction("subtitles", "enabled", [](lua_State* L) {
        lua_pushboolean(L, Subtitles::get().enabled());
        return 1;
    });

    // accessibility.*: what the player turned on, for a game to respect in its own effects.
    vm.registerFunction("accessibility", "reduceMotion", [](lua_State* L) {
        lua_pushboolean(L, Accessibility::get().reduceMotion());
        return 1;
    });
    vm.registerFunction("accessibility", "highContrast", [](lua_State* L) {
        lua_pushboolean(L, Accessibility::get().highContrast());
        return 1;
    });
    vm.registerFunction("accessibility", "screenReader", [](lua_State* L) {
        lua_pushboolean(L, Accessibility::get().screenReader());
        return 1;
    });
    vm.registerFunction("accessibility", "colorBlind", [](lua_State* L) {
        lua_pushstring(L, Accessibility::get().options().colorBlind.c_str());
        return 1;
    });
    // accessibility.announce(text [, interrupt]): said aloud when a screen reader is on; true when it was.
    vm.registerFunction("accessibility", "announce", [](lua_State* L) {
        const std::string text = luaL_checkstring(L, 1);
        Localization& loc = Localization::get();
        const std::string said = (!text.empty() && text[0] == '@') ? loc.tr(text.substr(1)) : (loc.has(text) ? loc.tr(text) : text);
        lua_pushboolean(L, Accessibility::get().announce(said, lua_toboolean(L, 2) != 0));
        return 1;
    });
}

} // namespace kke
