#include "kke/modules/SubtitleModule.h"

#include "kke/Application.h"
#include "kke/EngineSettings.h"
#include "kke/Localization.h"
#include "kke/Log.h"
#include "kke/RmlTextSafety.h"
#include "kke/ScriptVM.h"
#include "kke/modules/ScriptModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cstdio>
#include <typeindex>

namespace kke {

namespace {

// Drawn by itself (no theme file) and never takes the mouse. High contrast
// (class kke-high-contrast, set by UiModule on every document) makes the band solid and outlined.
const char* kRml = R"(
<rml>
<head>
    <title>Subtitles</title>
    <style>
        body { font-family: Noto Sans; pointer-events: none; width: 100%; height: 100%; }
        #subs { position: absolute; left: 10%; right: 10%; bottom: 36dp; text-align: center; }
        .line { display: block; margin: 3dp auto 0 auto; padding: 4dp 12dp; width: auto; max-width: 100%;
                background-color: #000000b4; border-radius: 5dp; color: #ffffff; font-effect: shadow(1dp 1dp #000000d0); }
        .speaker { color: #ffd966; font-weight: bold; }
        body.kke-high-contrast .line { background-color: #000000; border: 2dp #ffffff; }
        body.kke-high-contrast .speaker { color: #ffff00; }
    </style>
</head>
<body><div id="subs"></div></body>
</rml>
)";

LocArgs readArgs(lua_State* L, int index) {
    LocArgs args;
    if (!lua_istable(L, index)) return args;
    lua_pushnil(L);
    while (lua_next(L, index) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            const std::string key = lua_tostring(L, -2);
            size_t len = 0;
            const char* text = luaL_tolstring(L, -1, &len);
            args.emplace_back(key, std::string(text, len));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    return args;
}

} // namespace

std::vector<ModuleDependency> SubtitleModule::dependencies() const {
    return { { std::type_index(typeid(UiModule)), true, "draws the subtitles" },
             { std::type_index(typeid(SettingsModule)), false, "the player's subtitle choices" },
             { std::type_index(typeid(ScriptModule)), false, "the subtitle.* Lua functions" } };
}

void SubtitleModule::init(Application& app) {
    m_app = &app;
    m_ui = app.getModule<UiModule>();
    if (auto* settings = app.getModule<SettingsModule>()) onSettingsChanged(settings->settings()); // it may have applied before we existed
    registerLua();
}

std::string SubtitleModule::translated(const std::string& textOrKey, const LocArgs& args) const {
    if (m_localizer) {
        Localization& language = m_localizer->localization();
        if (language.has(textOrKey)) return language.tr(textOrKey, args);
    }
    return args.empty() ? textOrKey : Localization::format(textOrKey, args); // plain text with {placeholders}
}

uint32_t SubtitleModule::say(const std::string& textOrKey, float seconds, const std::string& speakerOrKey, const LocArgs& args) {
    if (!m_enabled) return 0; // the player turned subtitles off: nothing is queued, so nothing appears later
    const std::string speaker = speakerOrKey.empty() ? std::string() : translated(speakerOrKey, {});
    return m_queue.say(translated(textOrKey, args), speaker, seconds);
}

void SubtitleModule::onSettingsChanged(const EngineSettings& settings) {
    m_enabled = settings.accessibility.subtitles;
    m_scale = settings.accessibility.subtitleScale;
    if (!m_enabled) m_queue.clear();
}

bool SubtitleModule::makeDocument() {
    if (m_doc) return true;
    if (!m_ui || !m_ui->context()) {
        if (!m_noUiLogged) log::get(name())->warn("Subtitles need a UiModule (RmlUi): add app.addModule<kke::UiModule>()");
        m_noUiLogged = true;
        return false;
    }
    m_doc = m_ui->context()->LoadDocumentFromMemory(kRml, "subtitles");
    if (!m_doc) {
        log::get(name())->error("could not build the subtitles document");
        m_noUiLogged = true;
        return false;
    }
    m_box = m_doc->GetElementById("subs");
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return m_box != nullptr;
}

void SubtitleModule::update(const UpdateContext& ctx) {
    if (!m_localizer) {
        const auto found = m_app->findCapability<ILocalizer>();
        if (!found.empty()) m_localizer = found.front();
    }
    m_queue.update(ctx.dt);
    if (m_queue.empty() && !m_doc) return; // nothing to show, nothing built
    if (!makeDocument()) return;
    const bool rtl = m_localizer && m_localizer->localization().rtl();
    std::string rml;
    for (const SubtitleView& line : m_queue.visible()) {
        char alpha[16];
        std::snprintf(alpha, sizeof alpha, "%.2f", static_cast<double>(line.alpha));
        rml += "<div class=\"line\" style=\"opacity: ";
        rml += alpha;
        rml += ";\">";
        if (!line.speaker.empty()) rml += "<span class=\"speaker\">" + escapeRmlText(Localization::visualOrder(line.speaker, rtl)) + ":</span> ";
        std::string text = escapeRmlText(Localization::visualOrder(line.text, rtl));
        for (size_t at = text.find('\n'); at != std::string::npos; at = text.find('\n', at + 5)) text.replace(at, 1, "<br/>");
        rml += text + "</div>";
    }
    char size[48];
    std::snprintf(size, sizeof size, "font-size: %.1fdp;", static_cast<double>(18.0f * m_scale));
    const std::string key = size + rml;
    if (key == m_shown) return;
    m_shown = key;
    m_box->SetAttribute("style", size);
    m_box->SetInnerRML(rml);
}

void SubtitleModule::shutdown() {
    m_doc = nullptr; // the context owns it and closes it with the UI
    m_box = nullptr;
}

void SubtitleModule::registerLua() {
    auto* scripts = m_app->getModule<ScriptModule>();
    if (!scripts) return;
    ScriptVM& vm = scripts->vm();
    // subtitle.say(textOrKey [, seconds [, speaker [, args]]]) -> id (0 when subtitles are off)
    vm.registerFunction("subtitle", "say", [this](lua_State* L) {
        const std::string text = luaL_checkstring(L, 1);
        const float seconds = static_cast<float>(luaL_optnumber(L, 2, 0.0));
        const std::string speaker = lua_isstring(L, 3) ? lua_tostring(L, 3) : "";
        lua_pushinteger(L, say(text, seconds, speaker, readArgs(L, 4)));
        return 1;
    });
    vm.registerFunction("subtitle", "clear", [this](lua_State*) {
        clear();
        return 0;
    });
    vm.registerFunction("subtitle", "active", [this](lua_State* L) {
        lua_pushinteger(L, static_cast<lua_Integer>(m_queue.shown() + m_queue.pending()));
        return 1;
    });
}

} // namespace kke
