#pragma once

#include "kke/EngineSettings.h"
#include "kke/InputMap.h"
#include "kke/Module.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace kke {

class InputModule;
class LobbyModule;
class DemoPanelModule;
class SettingsModule;

// The menus every game shares, so a player finds the same things in the
// same places in every KKE game (Kees, 2026-09-28):
//
//   Title     the game's name over the game: Play (or the game's modes),
//             Settings, Controls, Quit. A game with a LobbyModule goes on
//             to its lobby after Play: that is where players pick the
//             game type and every controller is claimed before a match.
//   Pause     Start or Select on a controller, Esc on the keyboard:
//             Resume, the game's own rows, Settings, Controls, Main menu,
//             Quit. Local play freezes the game for everyone; online the
//             game keeps running (Application::setPaused only offline).
//   Settings  display (full screen, VSync, frame cap, UI size), sound
//             volumes, look sensitivity, invert look, and the game's own
//             rows. Saved in settings.json next to the games, shared by
//             all of them.
//   Controls  simple remapping: every button action of the game, for the
//             keyboard and mouse or for a controller; press the row, then
//             the new key or button. Saved in the game's input file. The
//             advanced editor (any number of bindings, triggers, chords)
//             is the rmlui_demo's Input screen (setAdvancedControls).
//
// It looks like kke_demo's pause menu (a column of rows on the left over
// a dimmed game) and works the same with a controller (d-pad or left
// stick, A, B), the keyboard (arrows, Enter, Esc) and the mouse.
//
//   // main.cpp, after InputModule and UiModule:
//   app.addModule<kke::GameShellModule>("Racing");
//   // the game's init(), all optional:
//   auto* shell = app.getModule<kke::GameShellModule>();
//   shell->addMode({ "race", "Race", "Three laps against the CPU" });
//   shell->onPlay = [this](const std::string& mode) { start(mode); };
//   shell->onMainMenu = [this] { backToStart(); };
//   shell->addPauseItem("Restart", [this] { restart(); });
//   shell->settings("Gameplay").toggle("Damage numbers", &m_numbers);
//
// With a DemoPanelModule in the game the panel's Select button and Esc go
// to this menu instead; its rows are one item away ("Demo settings").
//
// Developer switches: KKE_MAIN_MENU=0 starts in the game (headless runs,
// benchmarks: a benchmark run never shows it), KKE_MAIN_MENU=pause opens
// the pause menu 1.5 s in, KKE_MAIN_MENU=settings / controls open those
// pages (screenshots). See docs/GAME_SHELL.md.
class GameShellModule : public Module {
    struct Row;

public:
    enum class Page { None, Title, Modes, Pause, Settings, Controls };

    // A list of rows the game adds to a page (settings(), pauseRows()).
    class Rows {
    public:
        Rows& heading(std::string title);
        Rows& text(std::function<std::string()> live);
        Rows& button(std::string label, std::function<void()> onPress);
        Rows& toggle(std::string label, bool* value, std::function<void()> onChange = {});
        Rows& choice(std::string label, int* value, std::vector<std::string> options, std::function<void()> onChange = {});
        Rows& slider(std::string label, float* value, float min, float max, std::string format = "%.2f",
                     std::function<void()> onChange = {}, float step = 0.0f);
        Rows& slider(std::string label, int* value, int min, int max, std::function<void()> onChange = {});
        // The row added last shows only while `visible` says so.
        Rows& showIf(std::function<bool()> visible);

    private:
        friend class GameShellModule;
        explicit Rows(GameShellModule& owner) : m_owner(owner) {}
        Rows& add(Row row);
        GameShellModule& m_owner;
        std::vector<size_t> m_rows; // indices into the owner's game rows
    };

    struct Mode {
        std::string id, label, description;
    };

    explicit GameShellModule(std::string title, std::string subtitle = {});
    ~GameShellModule() override;
    const char* name() const override { return "GameShell"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void frameStart(const UpdateContext& ctx) override;
    void frameEnd() override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    // --- What the game tells it (before or after init) ---------------
    // Play lists these (with their descriptions) before starting; with
    // none, Play starts straight away (onPlay gets "").
    void addMode(Mode mode) { m_modes.push_back(std::move(mode)); }
    // Called when the player starts a game from the title (the mode's id).
    std::function<void(const std::string& mode)> onPlay;
    // Called by the pause menu's "Main menu" before the title shows: put
    // the game back to its start. Unset: the game stays as it is and Play
    // carries on from there. A game with a LobbyModule usually opens its
    // lobby here (and the title is skipped: the lobby is the menu).
    std::function<void()> onMainMenu;
    // True while the game is online: pause no longer freezes the game.
    // Unset: asks NetModule (connected or hosting).
    std::function<bool()> online;
    // True while the game may not be paused right now (a cutscene, a
    // loading screen, its own menu has the screen).
    std::function<bool()> blockPause;
    // True while Start is the game's own button ("Start: race again" on a
    // results screen): then only Select and Esc open the pause menu.
    std::function<bool()> startIsTheGames;
    // A row in the pause menu between Resume and Settings.
    Rows& pauseRows() { return m_pauseRows; }
    void addPauseItem(std::string label, std::function<void()> onPress, std::function<bool()> visible = {});
    // A section of the game's own settings, after the engine's.
    Rows& settings(const std::string& title);
    // The "Advanced" row of the Controls page (the rmlui_demo opens its
    // Input screen). Without it the row isn't there.
    void setAdvancedControls(std::function<void()> open) { m_advanced = std::move(open); }
    // Start on the title (default). Off: the game starts at once (a game
    // that has its own start screen, or a tool).
    void setStartOnTitle(bool enabled) { m_startOnTitle = enabled; }
    // The small line over the title ("KREATIVE KOMPAS ENGINE").
    void setBrand(std::string brand) { m_brand = std::move(brand); m_dirty = true; }

    // --- State ---------------------------------------------------------
    Page page() const { return m_page; }
    bool menuOpen() const { return m_page != Page::None; }
    bool onTitle() const { return m_page == Page::Title || m_page == Page::Modes; }
    // Opens the pause menu (as Start would).
    void openPause();
    // Back to the game from any page.
    void closeMenu();
    void showTitle();
    bool isOnline() const;

    // The settings in use (shared with SettingsModule when the game has
    // one) and saving them.
    EngineSettings& engineSettings();
    void applySettings();
    bool saveSettings();

    // --- Pure helpers, public for the tests ---------------------------
    // What a controls row edits: keyboard and mouse, or a controller.
    enum class DeviceClass { KeyboardMouse, Controller };
    static bool sourceIs(DeviceClass cls, const InputSource& source);
    // Replaces the action's bindings of that device class with one
    // binding of `source` (keeping the first old binding's trigger and
    // modifiers off). Other devices' bindings stay. False when the action
    // doesn't exist or the source is of the other class.
    static bool rebind(InputMap& map, const std::string& action, DeviceClass cls, const InputSource& source);
    // The actions a player can remap simply: Button actions outside the
    // menus (ui.*, panel.*, shell.*) and outside debug categories.
    static std::vector<std::string> remappable(const InputMap& map);
    // Frame cap choices and the index of a cap (0 = off).
    static const std::vector<int>& frameCaps();
    static int frameCapIndex(float fps);

private:
    struct Row {
        enum class Kind { Heading, Text, Note, SliderF, SliderI, Choice, Toggle, Button, Binding } kind = Kind::Button;
        std::string label;
        std::function<std::string()> live;
        std::function<float*()> f;
        std::function<int*()> i;
        std::function<bool*()> b;
        float min = 0.0f, max = 1.0f, step = 0.0f;
        std::string format;
        std::vector<std::string> options;
        std::function<void()> onChange;
        std::function<bool()> visible;
        std::string action; // Binding rows
        Rml::Element* el = nullptr;
        Rml::Element* value = nullptr;
        Rml::Element* fill = nullptr;
        std::string shown;
        float shownFill = -1.0f;
        bool focusable() const { return kind >= Kind::SliderF; }
    };
    struct Section {
        std::string title;
        Rows rows;
    };
    class Listener;
    friend class Rows;

    void setPage(Page page);
    void buildPage();
    void refresh();
    void input(float dt);
    void move(int direction);
    void change(int direction);
    void press();
    void back();
    void onClick(Rml::Element* target, float mouseX, bool down);
    void setSliderFromMouse(size_t row, float mouseX);
    bool rowVisible(const Row& r) const;
    void addRow(Row row);
    void addGameRows(const std::vector<Row>& source, const std::vector<size_t>& indices);
    void startCapture(const std::string& action);
    void finishCapture(const InputSource* source);
    void captureEvent(const SDL_Event& e);
    void setGameInput(bool enabled);
    void definePauseAction();
    void setFrozen(bool frozen);
    void loadSettings();
    std::string bindingText(const std::string& action) const;
    std::string prompts(const std::string& text) const;
    DeviceClass deviceClass() const;

    Application* m_app = nullptr;
    InputModule* m_input = nullptr;
    LobbyModule* m_lobby = nullptr;
    DemoPanelModule* m_panel = nullptr;
    SettingsModule* m_settingsModule = nullptr;
    std::string m_title, m_subtitle, m_brand = "KREATIVE KOMPAS ENGINE";
    std::vector<Mode> m_modes;
    std::vector<Row> m_gameRows; // what Rows add; copied into a page when it's built
    Rows m_pauseRows{ *this };
    std::vector<std::unique_ptr<Section>> m_sections;
    std::function<void()> m_advanced;
    bool m_startOnTitle = true;

    EngineSettings m_settings;
    std::string m_settingsPath = "settings.json";
    int m_frameCap = 0;          // index into frameCaps()
    int m_controlsDevice = 0;    // 0 keyboard and mouse, 1 controller

    Page m_page = Page::None;
    std::vector<Page> m_stack;   // pages to go back to
    std::vector<Row> m_rows;     // the shown page's rows
    size_t m_focus = 0;
    bool m_dirty = true;
    bool m_frozen = false;       // we paused the Application
    bool m_gameInputOff = false;
    int m_players = 0;           // InputModule players last seen
    bool m_panelHidden = false;
    bool m_lobbySuspended = false;
    bool m_lobbyRowsAdded = false;
    bool m_settingsChanged = false;
    bool m_bindingsChanged = false;
    float m_titleAt = -1.0f;     // seconds until the title freezes the game behind it
    float m_openAt = -1.0f;      // KKE_MAIN_MENU=pause/settings/controls
    Page m_openPage = Page::None;
    std::string m_capture;       // the action waiting for a key or button, or empty
    float m_captureCooldown = 0.0f;

    Rml::ElementDocument* m_doc = nullptr;
    Rml::Element* m_rowsEl = nullptr;
    std::unique_ptr<Listener> m_listener;
    long long m_dragRow = -1;
    float m_held[2] = {}, m_repeatAt[2] = {}, m_vHeld[2] = {}, m_vRepeatAt[2] = {};
    std::string m_hintShown;
};

} // namespace kke
