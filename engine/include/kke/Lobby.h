#pragma once

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace kke {

// The start menu of a local multiplayer game, as pure logic (no window,
// no SDL): who is playing, on which controller, what each player looks
// like, and player 1's game settings (how many CPU players, how hard each
// one is, and whatever rows the game or another module adds, like
// "Host game"). kke::LobbyModule turns real devices into Press events,
// shows it with RmlUi and hands the result to the game; see docs/LOBBY.md.
//
// Seats: player 1 is always in. Until they press something, player 1 is
// "any device"; the first device they press on becomes theirs (the classic
// "press A or Enter"). After that, any other controller pressing A (the
// south face button), or the keyboard pressing Enter when player 1 is on a
// controller, joins the next free seat, in the menu or during the game.
// Back (B / Esc) leaves a seat (not player 1's).
//
// Each seat moves a cursor through its rows: the game's look fields
// (colour, name, ...) and, for player 1 only, the options and Start.
// Left and right change the row's value, A goes to the next row (or
// presses an action row), Start from player 1 starts the game.
class Lobby {
public:
    static constexpr int kMaxSeats = 4;
    static constexpr int kMaxCpus = 5;
    // In a device list: matches no device (a seat whose controller is
    // unplugged; an empty list would mean every device).
    static constexpr uint32_t kNoDevice = 0xFFFFFFFFu;

    enum class Device : uint8_t { Any, KeyboardMouse, Pad };

    // A thing each player picks for themselves: a colour, a name, a hat.
    struct LookField {
        std::string id, label;
        std::vector<std::string> choices;
        std::vector<glm::vec3> swatches; // optional: a colour per choice (the UI shows it)
    };

    // A row of player 1's settings. With choices it's a value to pick
    // (left / right); without, an action (A presses it).
    struct Option {
        std::string id, label;
        std::vector<std::string> choices;
        int value = 0;
        bool visible = true;
        std::function<void()> onPress;         // an action row
        std::function<void(int value)> onChange;
    };

    struct Seat {
        bool joined = false;
        Device device = Device::Any;
        uint32_t pad = 0;          // Device::Pad: the InputDevices ref
        bool padPresent = true;    // false while its controller is unplugged
        std::vector<int> look;     // one choice per look field
        int row = 0;               // the seat's cursor
    };

    // One frame of one device's menu buttons, as edges (just pressed).
    struct Press {
        Device device = Device::KeyboardMouse; // KeyboardMouse or Pad
        uint32_t pad = 0;
        bool up = false, down = false, left = false, right = false;
        bool confirm = false; // A / Enter / Space
        bool back = false;    // B / Esc / Backspace
        bool start = false;   // Start / Enter on the keyboard is confirm
        bool any() const { return up || down || left || right || confirm || back || start; }
    };

    // What a seat's cursor is on.
    struct Row {
        enum class Kind : uint8_t { Look, Option, Start } kind = Kind::Look;
        int index = 0; // look field or option index
    };

    struct Toast {
        std::string text;
        float ttl = 4.0f;
    };

    Lobby();

    // ---- set up by the game
    void addLookField(LookField field);
    const std::vector<LookField>& lookFields() const { return m_looks; }
    // Adds a row to player 1's settings, before Start. Returns its index.
    int addOption(Option option);
    Option* option(const std::string& id);
    const std::vector<Option>& options() const { return m_options; }
    // A row player 1 types into (an address, a join code): A on it opens
    // an on-screen keyboard; on the keyboard, just type (Enter keeps it,
    // Esc puts back what was there). Saved with the other options.
    // `onDone` runs when they finish typing (kept, not cancelled).
    int addTextOption(std::string id, std::string label, std::string placeholder = {},
                      std::function<void(const std::string&)> onDone = {});
    bool isTextOption(const std::string& id) const { return m_texts.count(id) > 0; }
    std::string text(const std::string& id) const;
    std::string placeholder(const std::string& id) const;
    void setText(const std::string& id, std::string text);

    // ---- typing into a text row (player 1 only; the others carry on)
    // The on-screen keyboard: rows of keys; the last row is the actions.
    static const std::vector<std::vector<std::string>>& keyboardKeys();
    static constexpr const char* kKeyDelete = "Delete";
    static constexpr const char* kKeyClear = "Clear";
    static constexpr const char* kKeyDone = "Done";
    static constexpr size_t kMaxTextLength = 80;
    bool editing() const { return !m_editing.empty(); }
    const std::string& editingId() const { return m_editing; }
    int keyRow() const { return m_keyRow; }
    int keyCol() const { return m_keyCol; }
    void startEditing(const std::string& id);
    // Adds what was typed, keeping only what an address or a code can
    // hold (letters, digits and . : - _ @ [ ]).
    void typeText(const std::string& typed);
    void backspace();
    // keep = false puts back the text from before.
    void finishEditing(bool keep);
    // The CPU players' difficulty names, easiest first (default Easy,
    // Normal, Hard, Expert). Resets every CPU to the second one.
    void setDifficulties(std::vector<std::string> names);
    const std::vector<std::string>& difficulties() const { return m_difficulties; }
    // 0 hides the CPU rows (a game without CPU players).
    void setMaxCpus(int count);
    // How the join button is shown in toasts ("press A to join"): plain
    // "A" here; LobbyModule sets the controller's own glyph (RmlUi markup).
    std::string joinButton = "A";

    // ---- the settings
    int cpuCount() const;
    void setCpuCount(int count);
    int cpuDifficulty(int cpu) const; // index into difficulties()
    void setCpuDifficulty(int cpu, int difficulty);

    // ---- seats
    const Seat& seat(int i) const { return m_seats[static_cast<size_t>(i)]; }
    int joinedCount() const;
    // The seats that are in, in order (player 1 first).
    std::vector<int> joinedSeats() const;
    // The seat a pad belongs to, or -1.
    int seatOfPad(uint32_t pad) const;
    int seatOfKeyboard() const;
    // Joins the next free seat on a device (false when full or taken).
    int join(Device device, uint32_t pad = 0);
    void leave(int seat);
    // Moves a joined seat onto another device (a pause menu's "change
    // controller"): a controller (or flight stick) by its ref, or the
    // keyboard and mouse. Never onto one another seat holds: false then,
    // and nothing changes. Choosing the device it already has is true.
    bool setSeatDevice(int seat, Device device, uint32_t pad = 0);
    // The seat holding a device (-1: free). Player 1 before their first
    // press holds nothing.
    int seatOfDevice(Device device, uint32_t pad = 0) const;
    // "Player 2", or the seat's choice of a look field with id "name".
    std::string seatName(int seat) const;
    // The rows a seat's cursor moves through.
    std::vector<Row> rows(int seat) const;
    void setLook(int seat, int field, int choice);

    // Which device refs each seat listens to. `pads` and `keyboardMice`
    // are every connected device of each kind. A pinned seat gets its own;
    // player 1 before pinning gets everything nobody else has (and, while
    // no one else has claimed a pad, the first pad counts as theirs).
    std::vector<uint32_t> devicesFor(int seat, const std::vector<uint32_t>& pads, const std::vector<uint32_t>& keyboardMice) const;

    // ---- events
    // The menu is open (the lobby screen) or closed (in game: only joins).
    bool isOpen() const { return m_open; }
    void setOpen(bool open);
    void handle(const Press& press, const std::vector<uint32_t>& connectedPads = {});
    void padConnected(uint32_t pad, bool atStartup = false);
    void padDisconnected(uint32_t pad);
    void update(float dt); // ages the toasts
    // True once when player 1 asked to start.
    bool takeStart();
    void toast(const std::string& text, float seconds = 4.0f);
    const std::vector<Toast>& toasts() const { return m_toasts; }
    // Bumped on every change a UI would show.
    uint64_t revision() const { return m_revision; }

    std::function<void(int seat)> onJoin, onLeave;

    // ---- online (docs/LOBBY.md "Online"): the players on the other
    // machines of a network game, shown beside this screen's seats with
    // the looks they picked. Every machine runs the same game, so a look
    // goes over the network as its choice numbers ("look:2.0.5"), as the
    // NetModule player's character; a game may add its own words after
    // a '|' ("look:2.0.5|cpu").
    struct OnlinePlayer {
        uint8_t id = 0;            // NetModule player id (0: the host)
        std::string name;
        std::vector<int> look;     // one choice per look field; empty: unknown
        bool host = false, cpu = false;
    };
    // "look:" and the choices of `look`.
    static std::string lookText(const std::vector<int>& look);
    std::string lookText(int seat) const; // a seat's own
    // The choices in a character ("look:2.0.5|cpu"), one per look field
    // (missing or out of range: 0); empty when it isn't one.
    std::vector<int> lookFromText(const std::string& character) const;
    // The game's words after the '|' ("cpu"), or empty.
    static std::string characterExtra(const std::string& character);
    // A look choice's name and swatch, for any look (a seat's or an online player's).
    std::string lookChoice(const std::vector<int>& look, int field) const;
    void setOnlinePlayers(std::vector<OnlinePlayer> players); // bumps revision() only when it changed
    const std::vector<OnlinePlayer>& onlinePlayers() const { return m_online; }

    // ---- saved between runs: every seat's looks and player 1's settings
    // (no devices: controllers are claimed again each time).
    nlohmann::json save() const;
    void load(const nlohmann::json& j);

private:
    void changed() { ++m_revision; }
    void refreshCpuRows();
    void step(int seat, const Press& press);
    void editStep(const Press& press);

    struct TextField {
        std::string text, placeholder;
        std::function<void(const std::string&)> onDone;
    };
    std::map<std::string, TextField> m_texts; // by option id
    std::map<std::string, std::string> m_savedTexts; // loaded before their row was added
    std::string m_editing;                    // the text row being typed into, or empty
    std::string m_before;                     // its text before
    int m_keyRow = 0, m_keyCol = 0;

    std::vector<Seat> m_seats;
    std::vector<OnlinePlayer> m_online;
    std::vector<LookField> m_looks;
    std::vector<Option> m_options;
    std::vector<std::string> m_difficulties;
    std::vector<Toast> m_toasts;
    int m_maxCpus = kMaxCpus;
    bool m_open = true;
    bool m_start = false;
    uint64_t m_revision = 1;
};

} // namespace kke
