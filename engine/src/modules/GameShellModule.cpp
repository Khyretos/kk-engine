#include "kke/modules/GameShellModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/ResourceGovernor.h"
#include "kke/RmlTextSafety.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/SettingsModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/EventListener.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <typeindex>

namespace kke {

namespace {

// How long the game runs behind the title before it is frozen there: long
// enough for it to put its world and characters in place.
constexpr float kTitleSettle = 0.75f;
// After a key or button is caught for a binding, the menu ignores the pad
// this long, so the same press doesn't also press the row again.
constexpr float kCaptureCooldown = 0.3f;

// kke_demo's pause menu (games/showcase/ui/showcase_pause.rml), which Kees
// picked as the look for every game's menus: a column of rows on the left
// over the dimmed game. The document draws itself (no theme file) so it is
// the same in every game.
const char* kShellRml = R"(
<rml>
<head>
    <title>Menu</title>
    <style>
        body { font-family: Noto Sans; color: #e8ecf4; width: 100%; height: 100%;
               decorator: radial-gradient(circle farthest-corner at 30% 50%, #05070e55 20%, #05070ee6 100%); }
        div { display: block; }
        #menu { position: absolute; left: 8%; top: 9%; width: 440dp; max-width: 86%; max-height: 78%; overflow-y: auto; }
        #brand { font-size: 13dp; letter-spacing: 6dp; color: #ffcf5c; }
        #title { font-size: 44dp; font-weight: bold; line-height: 50dp; margin: 4dp 0 2dp 0; }
        #subtitle { font-size: 15dp; color: #aab3cc; margin-bottom: 16dp; }
        .item { width: 380dp; max-width: 96%; padding: 9dp 20dp; margin: 4dp 0; font-size: 17dp; color: #d6dcef; cursor: pointer;
                border-left: 3dp #00000000; border-radius: 0 6dp 6dp 0;
                decorator: linear-gradient(90deg, #1b2440cc, #1b244000);
                transition: padding-left border-color color 0.15s cubic-out; }
        .item:hover, .item.focused { padding-left: 32dp; color: #ffffff; border-left-color: #ffcf5c;
                                     decorator: linear-gradient(90deg, #3a4a86ee, #3a4a8600); }
        .item .value { float: right; color: #ffffff; font-size: 15dp; margin-top: 1dp; }
        .item .value img { font-size: 14dp; }
        .item .arrow { color: #ffcf5c; }
        .heading { font-size: 12dp; letter-spacing: 3dp; color: #ffcf5c; margin: 14dp 0 2dp 0; }
        .text { font-size: 14dp; color: #aab3cc; margin: 4dp 0; width: 380dp; max-width: 96%; }
        .hidden { display: none; }
        .bar { height: 5dp; margin: 6dp 0 1dp 0; border-radius: 3dp; background-color: #0b0f1c; border: 1dp #3a4670; }
        .fill { height: 100%; border-radius: 3dp; background-color: #ffcf5c; }
        .box { display: inline-block; width: 12dp; height: 12dp; border-radius: 3dp; border: 2dp #4a5888; background-color: #0b0f1c; }
        .box.on { background-color: #ffcf5c; border-color: #fff1c7; }
        #hint { position: absolute; left: 8%; bottom: 3%; font-size: 14dp; color: #aab3cc; }
        #hint img { font-size: 14dp; }
        #capture { position: absolute; left: 0; top: 0; width: 100%; height: 100%; display: none; background-color: #05070ecc; }
        #capture div { position: absolute; left: 8%; top: 44%; font-size: 24dp; color: #ffffff; }
        #capture span { font-size: 15dp; color: #aab3cc; }
        scrollbarvertical { width: 8dp; }
        scrollbarvertical slidertrack { background-color: #00000000; }
        scrollbarvertical sliderbar { background-color: #3a4670; border-radius: 4dp; min-height: 24dp; }
        scrollbarvertical sliderarrowdec, scrollbarvertical sliderarrowinc { height: 0; }
    </style>
</head>
<body>
    <div id="menu">
        <div id="brand"></div>
        <div id="title"></div>
        <div id="subtitle"></div>
        <div id="rows"></div>
    </div>
    <div id="hint"></div>
    <div id="capture"><div id="capturetext"></div></div>
</body>
</rml>
)";

bool envIs(const char* value) {
    const char* v = dev::env("KKE_MAIN_MENU");
    return v && std::strcmp(v, value) == 0;
}

} // namespace

// ---------------------------------------------------------------- Rows

GameShellModule::Rows& GameShellModule::Rows::add(Row row) {
    m_owner.m_gameRows.push_back(std::move(row));
    m_rows.push_back(m_owner.m_gameRows.size() - 1);
    m_owner.m_dirty = true;
    return *this;
}

GameShellModule::Rows& GameShellModule::Rows::heading(std::string title) {
    Row r;
    r.kind = Row::Kind::Heading;
    r.label = std::move(title);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::text(std::function<std::string()> live) {
    Row r;
    r.kind = Row::Kind::Text;
    r.live = std::move(live);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::button(std::string label, std::function<void()> onPress) {
    Row r;
    r.kind = Row::Kind::Button;
    r.label = std::move(label);
    r.onChange = std::move(onPress);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::toggle(std::string label, bool* value, std::function<void()> onChange) {
    Row r;
    r.kind = Row::Kind::Toggle;
    r.label = std::move(label);
    r.b = [value] { return value; };
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::choice(std::string label, int* value, std::vector<std::string> options,
                                                     std::function<void()> onChange) {
    Row r;
    r.kind = Row::Kind::Choice;
    r.label = std::move(label);
    r.i = [value] { return value; };
    r.options = std::move(options);
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::slider(std::string label, float* value, float min, float max, std::string format,
                                                     std::function<void()> onChange, float step) {
    Row r;
    r.kind = Row::Kind::SliderF;
    r.label = std::move(label);
    r.f = [value] { return value; };
    r.min = min;
    r.max = max;
    r.step = step > 0.0f ? step : (max - min) / 50.0f;
    r.format = std::move(format);
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::slider(std::string label, int* value, int min, int max, std::function<void()> onChange) {
    Row r;
    r.kind = Row::Kind::SliderI;
    r.label = std::move(label);
    r.i = [value] { return value; };
    r.min = static_cast<float>(min);
    r.max = static_cast<float>(max);
    r.step = 1.0f;
    r.onChange = std::move(onChange);
    return add(std::move(r));
}

GameShellModule::Rows& GameShellModule::Rows::showIf(std::function<bool()> visible) {
    if (!m_rows.empty()) m_owner.m_gameRows[m_rows.back()].visible = std::move(visible);
    return *this;
}

// ---------------------------------------------------------------- clicks

class GameShellModule::Listener : public Rml::EventListener {
public:
    explicit Listener(GameShellModule& s) : m_shell(s) {}
    void ProcessEvent(Rml::Event& event) override {
        m_shell.onClick(event.GetTargetElement(), event.GetParameter<float>("mouse_x", 0.0f), event.GetType() == "mousedown");
    }

private:
    GameShellModule& m_shell;
};

// ---------------------------------------------------------------- helpers

bool GameShellModule::sourceIs(DeviceClass cls, const InputSource& s) {
    switch (s.kind) {
    case SourceKind::Key:
    case SourceKind::MouseButton:
    case SourceKind::MouseMotion:
    case SourceKind::MouseWheel:
        return cls == DeviceClass::KeyboardMouse;
    case SourceKind::GamepadButton:
    case SourceKind::GamepadAxis:
        return cls == DeviceClass::Controller;
    default:
        return false;
    }
}

bool GameShellModule::rebind(InputMap& map, const std::string& action, DeviceClass cls, const InputSource& source) {
    if (!map.action(action) || !sourceIs(cls, source)) return false;
    Trigger trigger = Trigger::Press;
    std::vector<size_t> old = map.bindingsFor(action);
    std::sort(old.rbegin(), old.rend()); // remove from the back: indices stay valid
    for (size_t index : old) {
        const Binding& b = map.bindings()[index];
        if (!sourceIs(cls, b.source)) continue;
        trigger = b.trigger; // the last one seen is the first in order
        map.removeBinding(index);
    }
    Binding b = InputModule::bind(action, source, trigger);
    if (source.kind == SourceKind::GamepadAxis) b.threshold = 0.4f; // a trigger pulled a little way
    map.addBinding(b);
    return true;
}

std::vector<std::string> GameShellModule::remappable(const InputMap& map) {
    std::vector<std::string> out;
    for (const ActionDef& a : map.actions()) {
        if (a.type != ActionType::Button) continue;
        if (a.context == "ui" || a.context == "panel" || a.context == "shell" || a.context == "debug") continue;
        if (a.id.rfind("ui.", 0) == 0 || a.id.rfind("panel.", 0) == 0 || a.id.rfind("shell.", 0) == 0) continue;
        if (a.category == "Debug" || a.category == "Developer") continue;
        out.push_back(a.id);
    }
    return out;
}

const std::vector<int>& GameShellModule::frameCaps() {
    static const std::vector<int> caps{ 0, 30, 60, 75, 90, 120, 144, 165, 240 };
    return caps;
}

int GameShellModule::frameCapIndex(float fps) {
    const std::vector<int>& caps = frameCaps();
    int best = 0;
    for (size_t i = 0; i < caps.size(); ++i)
        if (std::abs(static_cast<float>(caps[i]) - fps) < std::abs(static_cast<float>(caps[static_cast<size_t>(best)]) - fps))
            best = static_cast<int>(i);
    return best;
}

// ---------------------------------------------------------------- module

GameShellModule::GameShellModule(std::string title, std::string subtitle) : m_title(std::move(title)), m_subtitle(std::move(subtitle)) {}
GameShellModule::~GameShellModule() = default;

std::vector<ModuleDependency> GameShellModule::dependencies() const {
    return { { std::type_index(typeid(InputModule)), true, "the menus are driven by actions and show button prompts" },
             { std::type_index(typeid(UiModule)), true, "draws the menus" },
             { std::type_index(typeid(SettingsModule)), false, "shares the settings when the game has them" },
             { std::type_index(typeid(LobbyModule)), false, "Play goes on to the lobby" },
             { std::type_index(typeid(DemoPanelModule)), false, "the pause menu opens the demo's panel" } };
}

void GameShellModule::addPauseItem(std::string label, std::function<void()> onPress, std::function<bool()> visible) {
    m_pauseRows.button(std::move(label), std::move(onPress));
    if (visible) m_pauseRows.showIf(std::move(visible));
}

GameShellModule::Rows& GameShellModule::settings(const std::string& title) {
    for (auto& s : m_sections)
        if (s->title == title) return s->rows;
    m_sections.push_back(std::make_unique<Section>(Section{ title, Rows(*this) }));
    return m_sections.back()->rows;
}

EngineSettings& GameShellModule::engineSettings() {
    return m_settingsModule ? m_settingsModule->settings() : m_settings;
}

void GameShellModule::loadSettings() {
    if (m_settingsModule) return; // SettingsModule loaded and applied them in its own init
    std::string error;
    m_settings = loadSettingsFile(m_settingsPath, &error, m_app->targetDefaultSettings());
    if (!error.empty()) log::get(name())->info("{}", error);
    applySettings();
}

void GameShellModule::applySettings() {
    if (!m_app) return;
    if (m_settingsModule) {
        m_settingsModule->apply();
        return;
    }
    // Only what a player expects a game's settings to change: the game
    // keeps its own camera, light and physics choices.
    m_settings.sanitize();
    const auto& g = m_settings.graphics;
    if (m_app->window().isFullscreen() != g.fullscreen) m_app->window().setFullscreen(g.fullscreen);
    m_app->renderer().setVSync(g.vsync);
    m_app->setResourceBudget(computeBudget(m_settings, usableCpuCount())); // the frame cap too
    for (ISettingsListener* listener : m_app->findCapability<ISettingsListener>()) listener->onSettingsChanged(m_settings);
}

bool GameShellModule::saveSettings() {
    if (m_settingsModule) return m_settingsModule->save();
    m_settings.sanitize();
    if (!saveSettingsFile(m_settings, m_settingsPath)) {
        log::get(name())->error("could not write settings to '{}'", m_settingsPath);
        return false;
    }
    log::get(name())->info("saved settings to '{}'", m_settingsPath);
    return true;
}

bool GameShellModule::isOnline() const {
    if (online) return online();
    if (!m_app) return false;
    NetModule* net = m_app->getModule<NetModule>();
    return net && (net->connected() || net->role() == NetModule::Role::Host);
}

void GameShellModule::definePauseAction() {
    // Only for button prompts ("{shell.pause} menu" in a game's hints): the
    // menu itself listens to Start, Select and Esc directly.
    for (int p = 0; m_input && p < m_input->players(); ++p) {
        InputMap& m = m_input->map(p);
        if (!m.action("shell.pause")) m.defineAction({ "shell.pause", "Pause menu", "Menus", "shell" });
        if (m.bindingsFor("shell.pause").empty()) {
            m.addBinding(InputModule::bind("shell.pause", InputModule::key(SDL_SCANCODE_ESCAPE)));
            m.addBinding(InputModule::bind("shell.pause", InputModule::pad(SDL_GAMEPAD_BUTTON_START)));
        }
    }
}

void GameShellModule::init(Application& app) {
    m_app = &app;
    m_input = app.getModule<InputModule>();
    definePauseAction();
    m_lobby = app.getModule<LobbyModule>();
    m_panel = app.getModule<DemoPanelModule>();
    m_settingsModule = app.getModule<SettingsModule>();
    // Esc is the pause key here, never "quit at once".
    app.window().setQuitOnEscape(false);
    loadSettings();

    auto* ui = app.getModule<UiModule>();
    if (ui && ui->context()) {
        m_doc = ui->context()->LoadDocumentFromMemory(kShellRml, "game_shell");
        if (!m_doc) log::get(name())->error("could not build the menu document");
    }
    if (m_doc) {
        m_listener = std::make_unique<Listener>(*this);
        m_doc->AddEventListener("click", m_listener.get());
        m_doc->AddEventListener("mousedown", m_listener.get());
        m_rowsEl = m_doc->GetElementById("rows");
        // Hidden, it must not catch the mouse either: RmlUi still hovers a
        // hidden body, and the game would think the UI has the mouse (a
        // click on the view would never reach the game).
        m_doc->SetProperty("visibility", "hidden");
        m_doc->SetProperty("pointer-events", "none");
        m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        // Above the game's own documents (the HUD, the lobby).
        m_doc->PullToFront();
    }

    // A benchmark or a headless check plays the game, not the menu.
    const bool skip = !m_startOnTitle || app.benchmark() || envIs("0");
    if (envIs("pause")) {
        m_openAt = 1.5f;
        m_openPage = Page::Pause;
    } else if (envIs("settings") || envIs("controls")) {
        m_openAt = 1.5f;
        m_openPage = envIs("settings") ? Page::Settings : Page::Controls;
    }
    if (!skip && m_openPage == Page::None) setPage(Page::Title);
}

void GameShellModule::shutdown() {
    if (m_doc) {
        if (m_listener) {
            m_doc->RemoveEventListener("click", m_listener.get());
            m_doc->RemoveEventListener("mousedown", m_listener.get());
        }
        m_doc->Close();
        m_doc = nullptr;
    }
    m_listener.reset();
    if (m_bindingsChanged && m_input) m_input->save();
}

void GameShellModule::setGameInput(bool enabled) {
    if (!m_input || m_gameInputOff == !enabled) return;
    m_gameInputOff = !enabled;
    for (int p = 0; p < m_input->players(); ++p) {
        m_input->map(p).setContextEnabled("game", enabled);
        if (!enabled) m_input->map(p).resetStates(); // nothing stays held while the menu is up
    }
}

void GameShellModule::setFrozen(bool frozen) {
    if (frozen == m_frozen || !m_app) return;
    if (frozen && m_app->isPaused()) return; // someone else paused it (the developer panel): theirs to undo
    m_frozen = frozen;
    m_app->setPaused(frozen);
}

void GameShellModule::setPage(Page page) {
    const Page was = m_page;
    m_page = page;
    m_dirty = true;
    m_capture.clear();
    if (page == Page::None) m_stack.clear();
    const bool open = page != Page::None;
    setGameInput(!open);
    if (m_panel) {
        if (open && !m_panelHidden) {
            m_panelHidden = true;
            m_panel->setVisible(false);
        } else if (!open && m_panelHidden) {
            m_panelHidden = false;
            m_panel->setVisible(true);
        }
    }
    if (m_lobby && m_lobbySuspended != open) {
        m_lobbySuspended = open;
        m_lobby->setSuspended(open);
    }
    // The mouse is free in a menu (and stays free after it: every game
    // takes it back with a click, as after Esc).
    if (open && was == Page::None) {
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
        if (m_doc) m_doc->PullToFront(); // over documents the game opened since
    }
    // Freezing: the pause menu offline; the title once the game behind it
    // has settled (frameStart).
    const Page root = m_stack.empty() ? page : m_stack.front();
    if (!open) {
        setFrozen(false);
        m_titleAt = -1.0f;
    } else if (root == Page::Pause) {
        setFrozen(!isOnline());
    } else if (root == Page::Title && was == Page::None) {
        m_titleAt = kTitleSettle;
    }
    m_focus = 0;
    if (m_doc) {
        m_doc->SetProperty("visibility", open ? "visible" : "hidden");
        m_doc->SetProperty("pointer-events", open ? "auto" : "none");
    }
}

void GameShellModule::openPause() {
    if (m_page != Page::None) return;
    setPage(Page::Pause);
}

void GameShellModule::startTouchEdit() {
    if (!m_input) return;
    m_touchEditing = true;
    if (m_doc) { // the game (still frozen) shows behind the controls, which take the mouse
        m_doc->SetProperty("visibility", "hidden");
        m_doc->SetProperty("pointer-events", "none");
    }
    m_input->editTouch(true);
}

void GameShellModule::closeMenu() {
    if (m_settingsChanged) {
        saveSettings();
        m_settingsChanged = false;
    }
    if (m_bindingsChanged && m_input) {
        m_input->save();
        m_bindingsChanged = false;
    }
    setPage(Page::None);
}

void GameShellModule::showTitle() {
    m_stack.clear();
    if (m_page != Page::None) setPage(Page::None);
    setPage(Page::Title);
}

GameShellModule::DeviceClass GameShellModule::deviceClass() const {
    return m_controlsDevice == 0 ? DeviceClass::KeyboardMouse : DeviceClass::Controller;
}

void GameShellModule::addRow(Row row) {
    m_rows.push_back(std::move(row));
}

void GameShellModule::addGameRows(const std::vector<Row>& source, const std::vector<size_t>& indices) {
    for (size_t i : indices) addRow(source[i]);
}

void GameShellModule::buildPage() {
    m_dirty = false;
    m_rows.clear();
    auto button = [&](std::string label, std::function<void()> fn) {
        Row r;
        r.kind = Row::Kind::Button;
        r.label = std::move(label);
        r.onChange = std::move(fn);
        addRow(std::move(r));
    };
    auto heading = [&](std::string label) {
        Row r;
        r.kind = Row::Kind::Heading;
        r.label = std::move(label);
        addRow(std::move(r));
    };
    auto push = [this](Page p) {
        m_stack.push_back(m_page);
        m_page = p;
        m_dirty = true;
        m_focus = 0;
    };
    auto play = [this](const std::string& mode) {
        closeMenu();
        if (onPlay) onPlay(mode);
    };
    std::string title = m_title, subtitle = m_subtitle;
    EngineSettings& s = engineSettings();
    switch (m_page) {
    case Page::None:
        break;
    case Page::Title:
        if (m_modes.empty()) button("Play", [play] { play(""); });
        else button("Play", [push] { push(Page::Modes); });
        for (const HostedGame& g : m_friends) {
            button(g.label, [this, g] {
                closeMenu(); // to the game (a lobby game: its lobby, where the host's picks show)
                NetModule* net = m_app->getModule<NetModule>();
                std::string error;
                if (net && !net->join(g.address, g.port, &error)) log::get(name())->warn("Can't join {}: {}", g.label, error);
            });
        }
        button("Settings", [push] { push(Page::Settings); });
        button("Controls", [push] { push(Page::Controls); });
        button("Quit", [this] { m_app->window().requestClose(); });
        break;
    case Page::Modes:
        subtitle = "Choose a game";
        for (const Mode& m : m_modes) {
            Row r;
            r.kind = Row::Kind::Button;
            r.label = m.label;
            r.format = m.description; // shown under the menu while focused
            const std::string id = m.id;
            r.onChange = [play, id] { play(id); };
            addRow(std::move(r));
        }
        button("Back", [this] { back(); });
        break;
    case Page::Pause: {
        title = "Paused";
        subtitle.clear();
        Row note;
        note.kind = Row::Kind::Text;
        note.live = [this] { return isOnline() ? std::string("Online: the game keeps running while this menu is open.") : std::string(); };
        addRow(std::move(note));
        button("Resume", [this] { closeMenu(); });
        addGameRows(m_gameRows, m_pauseRows.m_rows);
        if (m_panel) {
            // The demo's own panel, live: the game runs while it's used.
            button("Demo settings", [this] {
                closeMenu();
                m_panel->setState(DemoPanelModule::State::Active);
            });
        }
        button("Settings", [push] { push(Page::Settings); });
        button("Controls", [push] { push(Page::Controls); });
        button("Main menu", [this] {
            closeMenu();
            if (onMainMenu) onMainMenu();
            showTitle();
        });
        button("Quit", [this] { m_app->window().requestClose(); });
        break;
    }
    case Page::Settings: {
        title = "Settings";
        subtitle.clear();
        auto changed = [this] {
            m_settingsChanged = true;
            applySettings();
        };
        heading("DISPLAY");
        {
            Row r;
            r.kind = Row::Kind::Toggle;
            r.label = "Full screen";
            r.b = [&s] { return &s.graphics.fullscreen; };
            r.onChange = changed;
            addRow(std::move(r));
        }
        {
            Row r;
            r.kind = Row::Kind::Toggle;
            r.label = "VSync";
            r.b = [&s] { return &s.graphics.vsync; };
            r.onChange = changed;
            addRow(std::move(r));
        }
        {
            m_frameCap = frameCapIndex(s.graphics.frameRateLimit);
            Row r;
            r.kind = Row::Kind::Choice;
            r.label = "Frame cap";
            for (int cap : frameCaps()) r.options.push_back(cap == 0 ? "Off" : std::to_string(cap) + " fps");
            r.i = [this] { return &m_frameCap; };
            r.onChange = [this, &s, changed] {
                s.graphics.frameRateLimit = static_cast<float>(frameCaps()[static_cast<size_t>(m_frameCap)]);
                changed();
            };
            addRow(std::move(r));
        }
        {
            Row r;
            r.kind = Row::Kind::SliderF;
            r.label = "Menu and HUD size";
            r.f = [&s] { return &s.graphics.uiScale; };
            r.min = 0.75f;
            r.max = 1.75f;
            r.step = 0.05f;
            r.format = "%.2fx";
            r.onChange = changed;
            addRow(std::move(r));
        }
        heading("SOUND");
        auto volume = [&](const char* label, int* v) {
            Row r;
            r.kind = Row::Kind::SliderI;
            r.label = label;
            r.i = [v] { return v; };
            r.min = 0.0f;
            r.max = 100.0f;
            r.step = 5.0f;
            r.format = "%d%%";
            r.onChange = changed;
            addRow(std::move(r));
        };
        volume("Volume", &s.audio.master);
        volume("Music", &s.audio.music);
        volume("Effects", &s.audio.effects);
        {
            Row r;
            r.kind = Row::Kind::Toggle;
            r.label = "Quiet when in the background";
            r.b = [&s] { return &s.audio.muteWhenUnfocused; };
            r.onChange = changed;
            addRow(std::move(r));
        }
        heading("LOOKING AROUND");
        {
            Row r;
            r.kind = Row::Kind::SliderF;
            r.label = "Look speed";
            r.f = [&s] { return &s.controls.mouseSensitivity; };
            r.min = 0.1f;
            r.max = 3.0f;
            r.step = 0.1f;
            r.format = "%.1f";
            r.onChange = changed;
            addRow(std::move(r));
        }
        {
            Row r;
            r.kind = Row::Kind::Toggle;
            r.label = "Invert up and down";
            r.b = [&s] { return &s.controls.invertY; };
            r.onChange = changed;
            addRow(std::move(r));
        }
        for (const auto& sec : m_sections) {
            std::string upper = sec->title;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            heading(upper);
            addGameRows(m_gameRows, sec->rows.m_rows);
        }
        heading("");
        button("Defaults", [this, &s, changed] {
            const std::map<std::string, std::string> custom = s.custom;
            s = m_app->targetDefaultSettings();
            s.custom = custom;
            changed();
        });
        button("Back", [this] { back(); });
        break;
    }
    case Page::Controls: {
        title = "Controls";
        subtitle.clear();
        if (m_input && m_input->promptStyle() != PromptStyle::Keyboard && m_controlsDevice == 0 && !m_bindingsChanged) m_controlsDevice = 1;
        {
            Row r;
            r.kind = Row::Kind::Choice;
            r.label = "Buttons of";
            r.options = { "Keyboard and mouse", "Controller" };
            r.i = [this] { return &m_controlsDevice; };
            r.onChange = [this] { m_dirty = true; };
            addRow(std::move(r));
        }
        if (m_input) {
            const InputMap& map = m_input->map(0);
            std::string category;
            for (const std::string& id : remappable(map)) {
                const ActionDef* a = map.action(id);
                if (a->category != category) {
                    category = a->category;
                    std::string upper = category;
                    for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    heading(upper);
                }
                Row r;
                r.kind = Row::Kind::Binding;
                r.label = a->label.empty() ? a->id : a->label;
                r.action = a->id;
                addRow(std::move(r));
            }
        }
        heading("");
        // On-screen sticks and buttons: move, resize and rebind them on the game itself.
        if (m_input) button("Touch controls", [this] { startTouchEdit(); });
        button("Reset to defaults", [this] {
            for (int p = 0; m_input && p < m_input->players(); ++p) m_input->map(p).restoreDefaults();
            definePauseAction();
            m_bindingsChanged = true;
        });
        if (m_advanced) button("Advanced", [this] {
            closeMenu();
            m_advanced();
        });
        button("Back", [this] { back(); });
        break;
    }
    }
    if (!m_doc || !m_rowsEl) return;
    // Land on the first row that does something.
    if (m_focus >= m_rows.size() || !m_rows[m_focus].focusable())
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].focusable()) {
                m_focus = i;
                break;
            }
    std::string rml;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const Row& r = m_rows[i];
        const std::string id = std::to_string(i);
        const std::string attrs = "id=\"r" + id + "\" data-row=\"" + id + "\"";
        switch (r.kind) {
        case Row::Kind::Heading:
            rml += "<div class=\"heading\" " + attrs + ">" + escapeRmlText(r.label) + "</div>";
            break;
        case Row::Kind::Text:
        case Row::Kind::Note:
            rml += "<div class=\"text\" " + attrs + "><span id=\"v" + id + "\"></span></div>";
            break;
        case Row::Kind::Button:
            rml += "<div class=\"item\" " + attrs + ">" + escapeRmlText(r.label) + "</div>";
            break;
        case Row::Kind::Toggle:
            rml += "<div class=\"item\" " + attrs + ">" + escapeRmlText(r.label) + "<span class=\"value\"><span class=\"box\" id=\"v" + id +
                   "\"></span></span></div>";
            break;
        case Row::Kind::Choice:
            rml += "<div class=\"item\" " + attrs + ">" + escapeRmlText(r.label) +
                   "<span class=\"value\"><span class=\"arrow\" data-dir=\"-1\">&lt; </span><span id=\"v" + id +
                   "\"></span><span class=\"arrow\" data-dir=\"1\"> &gt;</span></span></div>";
            break;
        case Row::Kind::SliderF:
        case Row::Kind::SliderI:
            rml += "<div class=\"item\" " + attrs + ">" + escapeRmlText(r.label) + "<span class=\"value\" id=\"v" + id + "\"></span><div class=\"bar\" id=\"b" +
                   id + "\"><div class=\"fill\" id=\"f" + id + "\"></div></div></div>";
            break;
        case Row::Kind::Binding:
            rml += "<div class=\"item\" " + attrs + ">" + escapeRmlText(r.label) + "<span class=\"value\" id=\"v" + id + "\"></span></div>";
            break;
        }
    }
    m_rowsEl->SetInnerRML(rml);
    for (size_t i = 0; i < m_rows.size(); ++i) {
        const std::string id = std::to_string(i);
        m_rows[i].el = m_doc->GetElementById("r" + id);
        m_rows[i].value = m_doc->GetElementById("v" + id);
        m_rows[i].fill = m_doc->GetElementById("f" + id);
        m_rows[i].shown.clear();
        m_rows[i].shownFill = -1.0f;
    }
    if (Rml::Element* e = m_doc->GetElementById("brand")) e->SetInnerRML(escapeRmlText(m_brand));
    if (Rml::Element* e = m_doc->GetElementById("title")) e->SetInnerRML(escapeRmlText(title));
    if (Rml::Element* e = m_doc->GetElementById("subtitle")) {
        e->SetInnerRML(prompts(subtitle));
        e->SetClass("hidden", subtitle.empty());
    }
    m_hintShown.clear();
}

bool GameShellModule::rowVisible(const Row& r) const {
    if (r.visible && !r.visible()) return false;
    if (r.kind == Row::Kind::Text && r.live && r.live().empty()) return false;
    return true;
}

std::string GameShellModule::prompts(const std::string& text) const {
    return m_input ? m_input->promptText(text) : escapeRmlText(text);
}

std::string GameShellModule::bindingText(const std::string& action) const {
    if (!m_input) return {};
    PromptStyle style = PromptStyle::Keyboard;
    if (deviceClass() == DeviceClass::Controller) {
        style = m_input->promptStyle(0);
        if (!isPadStyle(style)) style = PromptStyle::Xbox;
    }
    const ButtonPrompts& p = m_input->prompts();
    const std::vector<ButtonPrompts::Glyph> glyphs = p.actionGlyphs(style, m_input->map(0), action);
    return glyphs.empty() ? std::string("not set") : p.rml(glyphs);
}

void GameShellModule::refresh() {
    if (!m_doc) return;
    // Rows that come and go.
    bool focusLost = false;
    for (size_t i = 0; i < m_rows.size(); ++i) {
        Row& r = m_rows[i];
        if (!r.el) continue;
        const bool vis = rowVisible(r);
        r.el->SetClass("hidden", !vis);
        r.el->SetClass("focused", i == m_focus && vis);
        if (!vis) {
            focusLost |= i == m_focus;
            continue;
        }
        std::string shown;
        float fill = -1.0f;
        switch (r.kind) {
        case Row::Kind::Text:
        case Row::Kind::Note:
            shown = prompts(r.live ? r.live() : std::string());
            break;
        case Row::Kind::Toggle:
            shown = (r.b && r.b() && *r.b()) ? "on" : "off";
            break;
        case Row::Kind::Choice: {
            const int v = r.i && r.i() ? *r.i() : -1;
            shown = v >= 0 && v < static_cast<int>(r.options.size()) ? escapeRmlText(r.options[static_cast<size_t>(v)]) : "-";
            break;
        }
        case Row::Kind::SliderF: {
            const float v = r.f && r.f() ? *r.f() : r.min;
            shown = escapeRmlText(DemoPanelModule::formatValue(r.format, v));
            fill = r.max > r.min ? (v - r.min) / (r.max - r.min) : 0.0f;
            break;
        }
        case Row::Kind::SliderI: {
            const int v = r.i && r.i() ? *r.i() : 0;
            shown = escapeRmlText(r.format.empty() ? std::to_string(v) : DemoPanelModule::formatValue(r.format, static_cast<float>(v)));
            fill = r.max > r.min ? (static_cast<float>(v) - r.min) / (r.max - r.min) : 0.0f;
            break;
        }
        case Row::Kind::Binding:
            shown = bindingText(r.action);
            break;
        default:
            continue;
        }
        if (r.value && shown != r.shown) {
            r.shown = shown;
            if (r.kind == Row::Kind::Toggle) r.value->SetClass("on", shown == "on");
            else r.value->SetInnerRML(shown);
        }
        if (r.fill && fill >= 0.0f && std::abs(fill - r.shownFill) > 1e-4f) {
            r.shownFill = fill;
            char pct[32];
            std::snprintf(pct, sizeof(pct), "%.2f%%", static_cast<double>(std::clamp(fill, 0.0f, 1.0f) * 100.0f));
            r.fill->SetProperty("width", pct);
        }
    }
    if (focusLost) move(1);

    // The line at the bottom: how to use the menu with what the player holds,
    // or what the focused game mode is.
    const bool keyboard = !m_input || m_input->promptStyle() == PromptStyle::Keyboard;
    std::string hint;
    if (m_focus < m_rows.size() && !m_rows[m_focus].format.empty() && m_page == Page::Modes) hint = escapeRmlText(m_rows[m_focus].format) + "<br/>";
    const bool canGoBack = m_page != Page::Title;
    if (keyboard)
        hint += prompts(std::string("{key:Up}{key:Down} choose  {key:Left}{key:Right} change  {key:Enter} select") + (canGoBack ? "  {key:Escape} back" : ""));
    else
        hint += prompts(std::string("{ui.up}{ui.down} choose  {ui.left}{ui.right} change  {ui.accept} select") + (canGoBack ? "  {ui.back} back" : ""));
    if (hint != m_hintShown) {
        m_hintShown = hint;
        if (Rml::Element* e = m_doc->GetElementById("hint")) e->SetInnerRML(hint);
    }
    if (Rml::Element* c = m_doc->GetElementById("capture")) {
        const bool capturing = !m_capture.empty();
        c->SetProperty("display", capturing ? "block" : "none");
        if (capturing)
            if (Rml::Element* t = m_doc->GetElementById("capturetext")) {
                const ActionDef* a = m_input ? m_input->map(0).action(m_capture) : nullptr;
                const std::string what = a ? (a->label.empty() ? a->id : a->label) : m_capture;
                const std::string device = deviceClass() == DeviceClass::KeyboardMouse ? "a key or a mouse button" : "a button on the controller";
                const std::string cancel = deviceClass() == DeviceClass::KeyboardMouse ? "Esc" : "Start";
                t->SetInnerRML(escapeRmlText("Press " + device + " for " + what) + "<br/><span>" + escapeRmlText(cancel + " keeps it as it was") + "</span>");
            }
    }
}

void GameShellModule::move(int direction) {
    std::vector<size_t> focusable;
    for (size_t i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].focusable() && rowVisible(m_rows[i])) focusable.push_back(i);
    if (focusable.empty()) return;
    auto it = std::find(focusable.begin(), focusable.end(), m_focus);
    int at = it == focusable.end() ? 0 : static_cast<int>(it - focusable.begin()) + direction;
    if (it == focusable.end() && direction < 0) at = static_cast<int>(focusable.size()) - 1;
    at = DemoPanelModule::cycle(at, static_cast<int>(focusable.size()), 0);
    m_focus = focusable[static_cast<size_t>(at)];
    if (m_focus < m_rows.size() && m_rows[m_focus].el) m_rows[m_focus].el->ScrollIntoView(false);
}

void GameShellModule::change(int direction) {
    if (m_focus >= m_rows.size()) return;
    Row& r = m_rows[m_focus];
    switch (r.kind) {
    case Row::Kind::SliderF:
        if (float* v = r.f ? r.f() : nullptr) {
            const float nv = DemoPanelModule::stepValue(*v, r.min, r.max, r.step, direction);
            if (nv != *v) {
                *v = nv;
                if (r.onChange) r.onChange();
            }
        }
        break;
    case Row::Kind::SliderI:
        if (int* v = r.i ? r.i() : nullptr) {
            const int step = std::max(1, static_cast<int>(r.step));
            const int nv = std::clamp(*v + direction * step, static_cast<int>(r.min), static_cast<int>(r.max));
            if (nv != *v) {
                *v = nv;
                if (r.onChange) r.onChange();
            }
        }
        break;
    case Row::Kind::Choice:
        if (int* v = r.i ? r.i() : nullptr) {
            *v = DemoPanelModule::cycle(*v, static_cast<int>(r.options.size()), direction);
            if (r.onChange) r.onChange();
        }
        break;
    case Row::Kind::Toggle:
        press();
        break;
    default:
        break;
    }
}

void GameShellModule::press() {
    if (m_focus >= m_rows.size()) return;
    Row& r = m_rows[m_focus];
    switch (r.kind) {
    case Row::Kind::Toggle:
        if (bool* v = r.b ? r.b() : nullptr) {
            *v = !*v;
            if (r.onChange) r.onChange();
        }
        break;
    case Row::Kind::Choice:
        change(1);
        break;
    case Row::Kind::Button: {
        // The callback may rebuild the rows (a new page): copy it first.
        const std::function<void()> fn = r.onChange;
        if (fn) fn();
        break;
    }
    case Row::Kind::Binding:
        startCapture(r.action);
        break;
    default:
        break;
    }
}

void GameShellModule::back() {
    if (!m_capture.empty()) {
        finishCapture(nullptr);
        return;
    }
    if (m_page == Page::Settings && m_settingsChanged) {
        saveSettings();
        m_settingsChanged = false;
    }
    if (m_page == Page::Controls && m_bindingsChanged && m_input) {
        m_input->save();
        m_bindingsChanged = false;
    }
    if (!m_stack.empty()) {
        const Page from = m_page;
        m_page = m_stack.back();
        m_stack.pop_back();
        m_dirty = true;
        m_focus = 0;
        buildPage();
        // Back on the row that led to the page we left.
        const char* label = from == Page::Settings ? "Settings" : from == Page::Controls ? "Controls" : from == Page::Modes ? "Play" : "";
        for (size_t i = 0; i < m_rows.size(); ++i)
            if (m_rows[i].label == label) m_focus = i;
        return;
    }
    if (m_page == Page::Pause) closeMenu();
}

void GameShellModule::startCapture(const std::string& action) {
    m_capture = action;
    m_captureCooldown = kCaptureCooldown;
}

void GameShellModule::finishCapture(const InputSource* source) {
    if (source && m_input) {
        bool any = false;
        for (int p = 0; p < m_input->players(); ++p) any |= rebind(m_input->map(p), m_capture, deviceClass(), *source);
        m_bindingsChanged |= any;
    }
    m_capture.clear();
    m_captureCooldown = kCaptureCooldown;
    for (int p = 0; m_input && p < m_input->players(); ++p) m_input->map(p).resetStates();
}

void GameShellModule::captureEvent(const SDL_Event& e) {
    const bool keys = deviceClass() == DeviceClass::KeyboardMouse;
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        if (e.key.key == SDLK_ESCAPE) {
            finishCapture(nullptr);
        } else if (keys) {
            const InputSource s = InputModule::key(e.key.scancode);
            finishCapture(&s);
        }
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && keys) {
        const InputSource s = InputModule::mouse(e.button.button);
        finishCapture(&s);
    } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && !keys) {
        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) {
            finishCapture(nullptr);
        } else {
            const InputSource s = InputModule::pad(static_cast<SDL_GamepadButton>(e.gbutton.button));
            finishCapture(&s);
        }
    } else if (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && !keys &&
               (e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) && e.gaxis.value > 20000) {
        const InputSource s = InputModule::padAxis(static_cast<SDL_GamepadAxis>(e.gaxis.axis), 1);
        finishCapture(&s);
    }
}

void GameShellModule::setSliderFromMouse(size_t index, float mouseX) {
    if (index >= m_rows.size() || !m_doc) return;
    Row& r = m_rows[index];
    Rml::Element* bar = m_doc->GetElementById("b" + std::to_string(index));
    if (!bar) return;
    const float left = bar->GetAbsoluteLeft() + bar->GetClientLeft();
    const float width = bar->GetClientWidth();
    if (width <= 0.0f) return;
    const float t = std::clamp((mouseX - left) / width, 0.0f, 1.0f);
    if (r.kind == Row::Kind::SliderF) {
        if (float* v = r.f ? r.f() : nullptr) {
            const float nv = DemoPanelModule::stepValue(r.min + t * (r.max - r.min), r.min, r.max, r.step, 0);
            if (nv != *v) {
                *v = nv;
                if (r.onChange) r.onChange();
            }
        }
    } else if (int* v = r.i ? r.i() : nullptr) {
        const int step = std::max(1, static_cast<int>(r.step));
        const int nv = static_cast<int>(std::lround((r.min + t * (r.max - r.min)) / static_cast<float>(step))) * step;
        if (nv != *v) {
            *v = std::clamp(nv, static_cast<int>(r.min), static_cast<int>(r.max));
            if (r.onChange) r.onChange();
        }
    }
}

void GameShellModule::onClick(Rml::Element* target, float mouseX, bool down) {
    if (!target || m_page == Page::None || !m_capture.empty()) return;
    int dir = 0;
    Rml::Element* rowEl = target;
    for (; rowEl && !rowEl->HasAttribute("data-row"); rowEl = rowEl->GetParentNode())
        if (dir == 0 && rowEl->HasAttribute("data-dir")) dir = rowEl->GetAttribute<int>("data-dir", 1);
    if (!rowEl) return;
    const size_t index = static_cast<size_t>(rowEl->GetAttribute<int>("data-row", -1));
    if (index >= m_rows.size() || !m_rows[index].focusable()) return;
    m_focus = index;
    const Row::Kind kind = m_rows[index].kind;
    if (kind == Row::Kind::SliderF || kind == Row::Kind::SliderI) {
        if (down) {
            m_dragRow = static_cast<long long>(index);
            setSliderFromMouse(index, mouseX);
        }
        return;
    }
    if (down) return;
    if (kind == Row::Kind::Choice) change(dir == 0 ? 1 : dir);
    else press();
}

void GameShellModule::onEvent(const SDL_Event& e) {
    if (!m_app) return;
    if (!m_capture.empty()) {
        captureEvent(e);
        return;
    }
    if (m_touchEditing) {
        // The touch layout's own bar has the screen; Esc or B is Done.
        if ((e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) ||
            (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST))
            m_input->editTouch(false);
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && m_dragRow >= 0) {
        const UiModule* ui = m_app->getModule<UiModule>();
        if (ui) setSliderFromMouse(static_cast<size_t>(m_dragRow), ui->toContext(glm::vec2(e.motion.x, e.motion.y)).x);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        m_dragRow = -1;
    }

    // Opening the pause menu: Start or Select on any controller, Esc on the
    // keyboard. In the lobby Start starts the game and Esc steps back, so
    // there only Select opens it (and the lobby's own Menu row).
    if (m_page == Page::None) {
        if (blockPause && blockPause()) return;
        const bool inLobby = m_lobby && m_lobby->isOpen();
        bool open = false;
        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)
            open = (e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK && !(selectIsTheGames && selectIsTheGames())) ||
                   (e.gbutton.button == SDL_GAMEPAD_BUTTON_START && !inLobby && !(startIsTheGames && startIsTheGames()));
        else if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
            open = e.key.key == SDLK_ESCAPE && !inLobby;
        if (open) openPause();
        return;
    }

    // In a menu: Start / Select on the pause page go back to the game.
    if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && m_stack.empty() && m_page == Page::Pause &&
        (e.gbutton.button == SDL_GAMEPAD_BUTTON_START || e.gbutton.button == SDL_GAMEPAD_BUTTON_BACK)) {
        closeMenu();
        return;
    }
    if (e.type != SDL_EVENT_KEY_DOWN) return;
    // The keyboard reaches the menu directly (ui.* are pad-only by default).
    switch (e.key.key) {
    case SDLK_UP: move(-1); break;
    case SDLK_DOWN: move(1); break;
    case SDLK_LEFT: change(-1); break;
    case SDLK_RIGHT: change(1); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
    case SDLK_SPACE:
        if (!e.key.repeat) press();
        break;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
        if (!e.key.repeat) back();
        break;
    default: break;
    }
}

void GameShellModule::input(float dt) {
    if (!m_input || !m_capture.empty()) return;
    if (m_captureCooldown > 0.0f) {
        m_captureCooldown -= dt;
        return;
    }
    // Any local player's controller drives the menu.
    auto any = [this](const char* action, bool held) {
        for (int p = 0; p < m_input->players(); ++p)
            if (held ? m_input->map(p).held(action) : m_input->map(p).pressed(action)) return true;
        return false;
    };
    if (any("ui.back", false)) {
        back();
        return;
    }
    if (any("ui.accept", false)) {
        press();
        return;
    }
    auto repeat = [&](const char* action, float& held, float& at, float first, float every) {
        if (any(action, false)) {
            held = 0.0f;
            at = first;
            return true;
        }
        if (!any(action, true)) return false;
        held += dt;
        if (held < at) return false;
        at += every;
        return true;
    };
    if (repeat("ui.up", m_vHeld[0], m_vRepeatAt[0], 0.4f, 0.12f)) move(-1);
    if (repeat("ui.down", m_vHeld[1], m_vRepeatAt[1], 0.4f, 0.12f)) move(1);
    if (repeat("ui.left", m_held[0], m_repeatAt[0], 0.35f, 0.06f)) change(-1);
    if (repeat("ui.right", m_held[1], m_repeatAt[1], 0.35f, 0.06f)) change(1);
}

void GameShellModule::frameEnd() {
    // The menu covers the game: a click anywhere is the menu's, never the
    // game's (games grab the mouse on a click unless the UI has it). After
    // UiModule's renderUi, which sets the flag from what the mouse is over.
    if (m_page != Page::None && m_app) m_app->setUiCapturesMouse(true);
}

// The title lists friends hosting this game (LAN or VPN, NetModule's
// search), so joining one is a single press from the main menu.
void GameShellModule::findFriends(float dt) {
    NetModule* net = m_app ? m_app->getModule<NetModule>() : nullptr;
    std::vector<HostedGame> found;
    if (net && m_page == Page::Title && net->role() == NetModule::Role::Offline) {
        if ((m_searchIn -= dt) <= 0.0f) {
            net->searchLan();
            m_searchIn = 3.0f;
        }
        for (const NetModule::LanGame& g : net->lanGames())
            if (g.ours) found.push_back({ "Join " + g.hostName + (g.players.empty() ? "" : " (" + g.players + ")"), g.address, g.port });
    } else {
        m_searchIn = 0.0f; // search at once when the title shows again
    }
    if (found == m_friends) return;
    m_friends = std::move(found);
    if (m_page == Page::Title) m_dirty = true;
}

void GameShellModule::frameStart(const UpdateContext& ctx) {
    // frameStart runs while the game is paused too, so the menus work then.
    if (m_lobby && !m_lobbyRowsAdded) {
        // The lobby is a menu of its own: a row there reaches this one.
        m_lobbyRowsAdded = true;
        m_lobby->lobby().addOption({ "shell.menu", "Settings and quit", {}, 0, true, [this] { openPause(); }, {} });
    }
    if (m_openAt > 0.0f && (m_openAt -= ctx.dt) <= 0.0f) {
        openPause();
        if (m_openPage == Page::Settings || m_openPage == Page::Controls) {
            m_stack.push_back(Page::Pause);
            m_page = m_openPage;
            m_dirty = true;
        }
    }
    if (m_titleAt > 0.0f && (m_titleAt -= ctx.dt) <= 0.0f && onTitle()) setFrozen(true);
    findFriends(ctx.dt);
    if (m_touchEditing) {
        if (m_input && m_input->touch().editing()) return;
        m_touchEditing = false; // Done: back to the Controls page
        if (m_doc) {
            m_doc->SetProperty("visibility", "visible");
            m_doc->SetProperty("pointer-events", "auto");
        }
        m_dirty = true;
    }
    // The touch controls' pause button opens the menu as Start does.
    if (m_input && m_page == Page::None && !(blockPause && blockPause()) && !(m_lobby && m_lobby->isOpen()) && m_input->touch().takePause())
        openPause();
    if (m_input && m_input->players() != m_players) {
        m_players = m_input->players(); // the lobby added players: their maps get the prompt action too
        definePauseAction();
    }
    // A game's own pause buttons, for controllers that aren't gamepads (a
    // flight stick, a wheel): bound to "shell.open" (the events above only
    // see gamepads and the keyboard).
    if (m_page == Page::None && m_input && m_capture.empty())
        for (int p = 0; p < m_input->players(); ++p)
            if (m_input->map(p).action("shell.open") && m_input->map(p).pressed("shell.open")) {
                openPause();
                break;
            }
    if (m_page == Page::None) return;
    if (m_panel) m_panel->setVisible(false); // a game may show it again meanwhile (online, the game runs)
    // A pause menu opened offline that went online meanwhile (a friend
    // joined): the game runs again.
    if ((m_stack.empty() ? m_page : m_stack.front()) == Page::Pause) setFrozen(!isOnline());
    if (m_dirty) buildPage();
    input(ctx.dt);
    if (m_dirty) buildPage();
    refresh();
}

} // namespace kke
