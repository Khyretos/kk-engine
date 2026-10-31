#include "kke/modules/LocalizationModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/EngineSettings.h"
#include "kke/Log.h"
#include "kke/ScriptVM.h"
#include "kke/modules/ScriptModule.h"
#include "kke/modules/SettingsModule.h"

#include <SDL3/SDL.h>

#include <filesystem>
#include <typeindex>

#include <lauxlib.h>
#include <lua.h>

namespace kke {

std::vector<ModuleDependency> LocalizationModule::dependencies() const {
    return { { std::type_index(typeid(SettingsModule)), false, "the player's language choice" },
             { std::type_index(typeid(ScriptModule)), false, "the i18n.* Lua functions and the game's own lang/ folder" } };
}

void LocalizationModule::init(Application& app) {
    m_app = &app;
    const char* base = SDL_GetBasePath();
    std::vector<std::string> searched;
    if (const std::string engine = findAssetFolder("assets/lang", { "KKE_LANG_DIR" }, base ? base : "", &searched); !engine.empty()) {
        if (m_loc.loadFolder(engine) > 0) m_folders.push_back(engine);
    }
    if (auto* scripts = app.getModule<ScriptModule>()) {
        const std::filesystem::path game = std::filesystem::path(scripts->gameFolder()) / "lang";
        std::error_code ec;
        if (std::filesystem::is_directory(game, ec) && m_loc.loadFolder(game) > 0) m_folders.push_back(game.string());
    }
    std::string setting = "auto";
    if (auto* settings = app.getModule<SettingsModule>()) setting = settings->settings().gameplay.language;
    chooseLanguage(setting);
    log::get(name())->info("{} language(s) from {} folder(s); speaking '{}'", m_loc.languages().size(), m_folders.size(), m_loc.language());
    m_hookPending = false; // the first choice is not a change a script needs to hear about
    registerLua();
}

bool LocalizationModule::chooseLanguage(const std::string& setting) {
    m_setting = setting;
    if (setting.empty() || setting == "auto") {
        const std::string system = m_loc.systemLanguage();
        const bool ok = m_loc.setLanguage(system.empty() ? m_loc.defaultLanguage() : system);
        m_hookPending = true;
        return ok;
    }
    const bool ok = m_loc.setLanguage(setting);
    if (ok) m_hookPending = true;
    return ok;
}

void LocalizationModule::onSettingsChanged(const EngineSettings& settings) {
    if (settings.gameplay.language != m_setting) chooseLanguage(settings.gameplay.language);
}

void LocalizationModule::update(const UpdateContext&) {
    if (!m_hookPending) return;
    m_hookPending = false;
    if (auto* scripts = m_app->getModule<ScriptModule>()) scripts->vm().callHook("LanguageChanged", m_loc.language());
}

namespace {

LocArgs readArgs(lua_State* L, int index) {
    LocArgs args;
    if (!lua_istable(L, index)) return args;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) { // key at -2, value at -1
        if (lua_type(L, -2) == LUA_TSTRING) {
            const std::string key = lua_tostring(L, -2);
            size_t len = 0;
            const char* text = luaL_tolstring(L, -1, &len); // pushes a string copy
            args.emplace_back(key, std::string(text, len));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return args;
}

} // namespace

void LocalizationModule::registerLua() {
    auto* scripts = m_app->getModule<ScriptModule>();
    if (!scripts) return;
    ScriptVM& vm = scripts->vm();
    // i18n.tr("menu.start", {name = "Kees"}) -> the text in the player's language.
    vm.registerFunction("i18n", "tr", [this](lua_State* L) {
        const std::string text = m_loc.tr(luaL_checkstring(L, 1), readArgs(L, 2));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    // i18n.trn("coins", 3) -> "3 coins": the plural form for a count.
    vm.registerFunction("i18n", "trn", [this](lua_State* L) {
        const std::string text = m_loc.trn(luaL_checkstring(L, 1), luaL_checknumber(L, 2), readArgs(L, 3));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    vm.registerFunction("i18n", "has", [this](lua_State* L) {
        lua_pushboolean(L, m_loc.has(luaL_checkstring(L, 1)));
        return 1;
    });
    vm.registerFunction("i18n", "language", [this](lua_State* L) {
        lua_pushstring(L, m_loc.language().c_str());
        return 1;
    });
    // i18n.setLanguage("nl") -> true, or false when there is nothing for it.
    vm.registerFunction("i18n", "setLanguage", [this](lua_State* L) {
        lua_pushboolean(L, chooseLanguage(luaL_checkstring(L, 1)));
        return 1;
    });
    // i18n.languages() -> { {code="en", name="English", rtl=false}, ... }
    vm.registerFunction("i18n", "languages", [this](lua_State* L) {
        const auto langs = m_loc.languages();
        lua_createtable(L, static_cast<int>(langs.size()), 0);
        int i = 1;
        for (const LanguageInfo& l : langs) {
            lua_createtable(L, 0, 3);
            lua_pushstring(L, l.code.c_str());
            lua_setfield(L, -2, "code");
            lua_pushstring(L, l.name.c_str());
            lua_setfield(L, -2, "name");
            lua_pushboolean(L, l.rtl);
            lua_setfield(L, -2, "rtl");
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });
    vm.registerFunction("i18n", "rtl", [this](lua_State* L) {
        lua_pushboolean(L, m_loc.rtl());
        return 1;
    });
    // i18n.number(1234.5) -> "1,234.5" or "1.234,5": the way the language writes it.
    vm.registerFunction("i18n", "number", [this](lua_State* L) {
        const std::string text = m_loc.formatNumber(luaL_checknumber(L, 1), static_cast<int>(luaL_optinteger(L, 2, -1)));
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    // i18n.visual(text) -> the text shaped and ordered for display, when the
    // language reads right to left (documents do this by themselves).
    vm.registerFunction("i18n", "visual", [this](lua_State* L) {
        const std::string in = luaL_checkstring(L, 1);
        const std::string text = Localization::visualOrder(in, m_loc.rtl());
        lua_pushlstring(L, text.data(), text.size());
        return 1;
    });
    // i18n.missing() -> the keys asked for that no language had (for a tool or a log).
    vm.registerFunction("i18n", "missing", [this](lua_State* L) {
        const auto& keys = m_loc.missing();
        lua_createtable(L, static_cast<int>(keys.size()), 0);
        int i = 1;
        for (const std::string& k : keys) {
            lua_pushstring(L, k.c_str());
            lua_rawseti(L, -2, i++);
        }
        return 1;
    });
}

} // namespace kke
