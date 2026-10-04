#pragma once

#include "kke/UiProfile.h"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kke {

class InputMap;
struct InputSource;

// On-screen controls for phones, tablets and touch screens: a stick to
// move, a drag anywhere else to look, round buttons for the game's
// actions and a pause button, so every game plays with fingers alone.
// Players move, resize and rebind them in an edit mode (the Controls
// page of the game shell, "Touch controls"); the layout is saved with
// the bindings in the game's input file.
//
// The sticks are a controller's sticks: they feed the left and right
// stick of "any controller" into player 1's map, so every action a pad
// moves (walking, steering, menus) follows the thumb with the game's own
// deadzones and inverts. Buttons hold an *action* (a Button action, or an
// Axis1D one such as "throttle" held at 1), and the look drag turns an
// axis action ("look.rate") as fast as the finger moves.
//
// Pure logic, in pixels of the frame; InputModule feeds it fingers and
// applies it, UiModule draws it (kke/TouchOverlay). Tested in
// tests/test_touch_controls.cpp. See docs/TOUCH.md.
struct TouchControl {
    enum class Kind : uint8_t { Stick, Button, Look, Pause };
    Kind kind = Kind::Button;
    int stick = 0;            // Stick: 0 = a controller's left stick, 1 = its right stick
    std::string action;       // Button: the action it holds; Look: the axis action it turns
    std::string label;        // Button: the words on it (empty: the action's label)
    glm::vec2 anchor{ 1.0f }; // the corner or edge it keeps to: 0, 0.5 or 1 of the safe area each way
    glm::vec2 offset{ 0.0f }; // its centre from the anchor, in short sides of the screen
    float size = 0.14f;       // diameter, in short sides of the screen
    bool latch = false;       // Button: a tap turns it on, the next tap off (crouch, sprint)
    bool operator==(const TouchControl&) const = default;
};

struct TouchLayoutOptions {
    // How the player looks around: a drag anywhere free (third and first
    // person), a right stick (games where a tap on the world matters,
    // like pointing at a spot), or not at all.
    enum class Look : uint8_t { Area, Stick, None };
    Look look = Look::Area;
    int maxButtons = 6;
    // These actions, in this order, instead of the guess.
    std::vector<std::string> buttons;
};

class TouchControls {
public:
    // ---- layout
    // A layout guessed from the actions: the left stick when something is
    // bound to a pad's left stick, the look drag (or the right stick) when
    // an axis action is on its right stick, and a button for each action a
    // controller player presses, A first (buttonActions(), most used pad
    // buttons first, up to maxButtons). Plus a pause button.
    static std::vector<TouchControl> autoLayout(const InputMap& map, const TouchLayoutOptions& options = {});
    // Actions a button may hold, in the map's order: Button actions and
    // Axis1D actions on a trigger, outside the menus and developer keys.
    static std::vector<std::string> buttonActions(const InputMap& map);
    // The axis action a look drag turns: "look.rate", else one on the
    // right stick (not the orbit camera's, which has its own drag). Empty: none.
    static std::string lookAction(const InputMap& map);

    // The game's layout; the shown one until the player edits theirs.
    void setDefaults(std::vector<TouchControl> controls);
    const std::vector<TouchControl>& defaults() const { return m_defaults; }
    const std::vector<TouchControl>& layout() const { return m_layout; }
    void setLayout(std::vector<TouchControl> controls);
    void resetToDefaults();
    bool hasDefaults() const { return m_haveDefaults; }

    float opacity = 0.6f;  // of the drawn controls, 0.15..1
    // A drag across the screen's short side moves the look axis as a full
    // stick held for one second (about half a turn in the demos) at 1.
    float lookSpeed = 1.0f;

    // ---- the screen, in pixels: the whole frame and its safe area
    void setScreen(const glm::vec2& size, const ScreenRect& safe);
    glm::vec2 centre(const TouchControl& c) const;
    float radius(const TouchControl& c) const;
    float shortSide() const;
    // The control under a point (sticks reach a little further than they
    // are drawn, so a thumb need not be exact), or -1. Never the look drag.
    int controlAt(const glm::vec2& px) const;
    // Whether a finger landing here would be the controls' (a control, or
    // the look drag when there is one). Editing: only controls.
    bool wouldTake(const glm::vec2& px) const;

    // ---- fingers, in pixels. Down returns true when the touch is the
    // controls' (then its moves and lift are too).
    bool fingerDown(uint64_t id, const glm::vec2& px);
    bool fingerMove(uint64_t id, const glm::vec2& px);
    bool fingerUp(uint64_t id);
    bool owns(uint64_t id) const;
    int fingers() const { return static_cast<int>(m_fingers.size()); }
    void releaseAll();

    // ---- what the fingers do
    // A stick's value as a controller's: x right, y down, length up to 1.
    glm::vec2 stick(int stick) const;
    // A gamepad-axis source's value from the sticks (any device, full
    // range: the caller takes halves); 0 for everything else.
    float sourceValue(const InputSource& source) const;
    bool held(int index) const;
    // Buttons, axes and the look drag into `map` (call before its update;
    // dt in seconds since the last apply). Editing applies nothing.
    void apply(InputMap& map, float dt);
    // True once after the pause button was tapped.
    bool takePause();

    // ---- editing: drag a control to move it; the rest goes through these
    void setEditing(bool editing);
    bool editing() const { return m_editing; }
    int selected() const { return m_selected; }
    void select(int index);
    void resize(int index, float factor);
    void setAction(int index, const std::string& action);
    void setLatch(int index, bool latch);
    // A new button in the middle of the screen, selected. Returns its index.
    int addButton(const std::string& action);
    void remove(int index);

    // Saved: the layout (only when it differs from the defaults, so a
    // game's new defaults reach players who never edited), opacity and
    // look speed. load() drops buttons whose action the map doesn't have.
    nlohmann::json save() const;
    bool load(const nlohmann::json& j, const InputMap& map);

    static const char* kindName(TouchControl::Kind kind);

private:
    struct Finger {
        uint64_t id = 0;
        int control = -1;  // -1: the look drag
        glm::vec2 pos{ 0.0f }, grab{ 0.0f };
    };
    Finger* finger(uint64_t id);
    void moveTo(TouchControl& c, const glm::vec2& centrePx) const;
    void edge(const std::string& action, bool down);

    std::vector<TouchControl> m_defaults, m_layout;
    bool m_haveDefaults = false;
    std::vector<uint8_t> m_latched; // per control
    glm::vec2 m_size{ 0.0f };
    ScreenRect m_safe;
    std::vector<Finger> m_fingers;
    std::vector<std::pair<std::string, bool>> m_edges; // button presses since the last apply, in order
    std::vector<std::string> m_heldAxes;               // Axis1D actions a button set last apply
    glm::vec2 m_lookMoved{ 0.0f };
    bool m_lookWasOn = false;
    std::string m_lookAppliedTo;
    bool m_pause = false;
    bool m_editing = false;
    int m_selected = -1;
};

} // namespace kke
