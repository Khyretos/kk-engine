#pragma once

#include "kke/ButtonPrompts.h"
#include "kke/Lobby.h"
#include "kke/Module.h"

#include <RmlUi/Core/DataModelHandle.h>

#include <map>
#include <string>
#include <vector>

namespace Rml { class ElementDocument; }

namespace kke {

class InputModule;

// The start menu and "press A to join" for local multiplayer games: a
// kke::Lobby fed from every connected controller and the keyboard, shown
// with RmlUi (four player cards along the bottom, player 1's with the
// game's settings and Start), plus toasts that also show during the game
// ("Controller connected: press A to join"). The game draws whatever it
// likes behind it (Climb Race lines the climbers up) and reads the result.
// See docs/LOBBY.md.
//
//   auto& lobby = app.addModule<kke::LobbyModule>("my_game_lobby.json");
//   // in the game's init():
//   lobby.lobby().addLookField({ "colour", "Colour", { "Sky", "Ember" }, { ... } });
//   lobby.load();            // last time's looks and settings
//   // every frame while the menu is up:
//   if (lobby.lobby().takeStart()) { lobby.close(); lobby.applyInput(); startGame(); }
//
// Other modules add rows to player 1's settings with lobby().addOption()
// (the network thread's "Host game" / "Join game" go there).
//
// Headless / demo switch (developer builds): KKE_LOBBY_JOIN=<n> joins the
// first n connected controllers as players 2.. at startup (with
// KKE_VIRTUAL_INPUT=pad,pad for screenshots of a full lobby).
class LobbyModule : public Module {
public:
    explicit LobbyModule(std::string path = "lobby.json");
    ~LobbyModule() override;
    const char* name() const override { return "Lobby"; }
    std::vector<ModuleDependency> dependencies() const override;
    void init(Application& app) override;
    void update(const UpdateContext& ctx) override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    Lobby& lobby() { return m_lobby; }
    // The big line over the menu, and the one under it ({a}, {start}, ...:
    // button prompts, see ButtonPrompts::format).
    void setTitle(std::string title, std::string subtitle = {});

    // The menu screen: open, or closed for the game (toasts still show,
    // and controllers can still join).
    void open();
    void close();
    bool isOpen() const { return m_lobby.isOpen(); }

    // One InputModule player per joined seat, in seat order, each on its
    // own devices; kept up to date as controllers come and go. Call it
    // when the game starts (players who join later are added the next
    // time, so a race in progress keeps its split screen).
    void applyInput();
    // The InputModule player of a seat since the last applyInput (-1: none).
    int playerOf(int seat) const;
    int players() const { return static_cast<int>(m_applied.size()); }

    bool load();
    bool save() const;

private:
    struct Held {
        unsigned bits = 0;
        float repeat = 0.0f;
    };
    void readDevices(float dt);
    Lobby::Press pressFrom(unsigned now, Held& held, unsigned tapped, float dt) const;
    void assignDevices();
    void buildUi();
    void refreshUi();
    // Button prompts (Xelu glyphs, kke/ButtonPrompts.h) as RmlUi markup.
    PromptStyle seatStyle(int seat) const;
    std::string glyph(PromptStyle style, const std::string& name) const;
    std::string promptText(PromptStyle style, const std::string& text) const;

    std::string m_path;
    Application* m_app = nullptr;
    InputModule* m_input = nullptr;
    Lobby m_lobby;
    std::vector<int> m_applied;              // seat of each InputModule player
    std::vector<uint32_t> m_pads, m_keyboardMice;
    std::map<uint32_t, Held> m_held;         // per pad; 0 = the keyboard
    std::map<uint32_t, unsigned> m_tapped;   // pressed (and maybe let go) since the last frame
    bool m_firstFrame = true;
    uint64_t m_shownRevision = 0;
    uint32_t m_shownPromptSerial = 0;
    PromptStyle m_joinStyle = PromptStyle::Xbox; // the controller last plugged in or pressed
    std::string m_title, m_subtitle;

    struct RowView {
        std::string label, value, swatch = "#00000000"; // swatch: always a colour (RmlUi styles hidden rows too)
        bool hasSwatch = false;
        bool focused = false, action = false, start = false, arrows = false;
    };
    struct SeatView {
        bool joined = false, you = false, unplugged = false, waiting = false;
        std::string name, device, accent, prompt;
        std::vector<RowView> rows;
    };
    struct View {
        bool open = true;
        std::string title, subtitle, hint;
        std::vector<SeatView> seats;
        std::vector<std::string> toasts;
    };
    View m_view;
    Rml::DataModelHandle m_model;
    Rml::ElementDocument* m_doc = nullptr;
};

} // namespace kke
