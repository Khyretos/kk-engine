#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace kke {

// Action-based input, modelled on Escape from Tarkov's binding screen:
// games ask for *actions* ("jump", "lean.left", "move"), never for keys,
// and every action can be bound to any number of *sources* (a key, a mouse
// button, the wheel, mouse motion, a gamepad button, half of a stick axis,
// a hat direction, a HOTAS button, a gyro axis, ...) with:
//   - a trigger: Press, Release, Hold, Tap, DoubleTap, Continuous, Toggle,
//     so one key can do different things when tapped, held or
//     double-tapped (Tarkov's "Q tap = lean, Q hold = hold lean");
//   - modifiers: other sources that must be held (Ctrl+E, LB+A, a HOSAS
//     shift button), and a binding with more modifiers wins over one with
//     fewer on the same source (Ctrl+E suppresses E);
//   - analog shaping: deadzone, response curve, invert, scale, and the
//     threshold at which an analog source counts as pressed.
// Anything maps to anything: an axis half can press a button action, keys
// can drive an axis (W = +1, S = -1), mouse motion and gyro are axes.
//
// Pure logic: it reads source values through InputState (InputDevices in
// the engine, a fake in tests), so every rule here is unit-tested.
// See INPUT.md.

enum class SourceKind : uint8_t {
    None,
    Key,            // code = SDL_Scancode (physical position; names come from the layout)
    MouseButton,    // code = SDL button index (1 left, 2 middle, 3 right, 4/5 side, ...)
    MouseWheel,     // code 0 = vertical, 1 = horizontal; per-frame notches
    MouseMotion,    // code 0 = x, 1 = y; per-frame pixels
    GamepadButton,  // code = SDL_GamepadButton (paddles, QAM/misc included)
    GamepadAxis,    // code = SDL_GamepadAxis; sticks -1..1, triggers 0..1
    JoyButton,      // raw joystick button index (HOTAS, wheels, pedals, any device)
    JoyAxis,        // raw joystick axis index, -1..1
    JoyHat,         // code = hat * 4 + direction (0 up, 1 right, 2 down, 3 left)
    Gyro,           // code 0..2 = pitch/yaw/roll rate, degrees per second
    Accel,          // code 0..2, m/s^2
    TouchX,         // code = touchpad * 8 + finger; 0..1 (value 0 when not touching)
    TouchY,
};

struct InputSource {
    SourceKind kind = SourceKind::None;
    uint32_t device = 0;   // 0 = any device of this kind; else InputDevices' stable device ref
    int32_t code = 0;
    int8_t half = 0;       // axes: 0 = full range, +1 = positive half only, -1 = negative half only (reported positive)

    bool valid() const { return kind != SourceKind::None; }
    bool operator==(const InputSource& o) const { return kind == o.kind && device == o.device && code == o.code && half == o.half; }
    bool operator!=(const InputSource& o) const { return !(*this == o); }
    bool isRelative() const { return kind == SourceKind::MouseMotion || kind == SourceKind::MouseWheel; }
    bool isAnalog() const;
};

// Current value of any source. Buttons 0/1, axes -1..1, relative sources
// per-frame deltas. `allowed` (may be null = everything) restricts "any
// device" sources to one player's devices in split screen.
class InputState {
public:
    virtual ~InputState() = default;
    virtual float value(const InputSource& source, const std::vector<uint32_t>* allowed) const = 0;
};

enum class ActionType : uint8_t { Button, Axis1D, Axis2D };

enum class Trigger : uint8_t {
    Press,       // fires on the down edge; held while down
    Release,     // fires on the up edge
    Hold,        // fires once held for holdTime; held from then until release
    Tap,         // fires on release if it was shorter than tapTime (waits for a double tap if one is bound)
    DoubleTap,   // fires on the second press within doubleTapWindow
    Continuous,  // held (and analog value) the whole time the source is active
    Toggle,      // each press flips the action on/off
};

struct Binding {
    std::string action;
    InputSource source;
    InputSource sourceY;                  // Axis2D from a stick: source = X, sourceY = Y, radial deadzone
    std::vector<InputSource> modifiers;   // all must be held
    Trigger trigger = Trigger::Press;
    int component = 0;                    // Axis2D fed by a single source: 0 = x, 1 = y
    float scale = 1.0f;                   // W -> move.y +1, S -> move.y -1; mouse/gyro sensitivity
    float deadzone = 0.0f;                // analog: values below this are 0 (rest rescaled to 0..1)
    float curve = 1.0f;                   // analog: value^curve (keeps the sign); >1 = finer near centre
    bool invert = false;
    float threshold = 0.5f;               // analog -> pressed
    float holdTime = 0.35f;
    float tapTime = 0.25f;
    float doubleTapWindow = 0.3f;
};

struct ActionDef {
    std::string id;           // "move", "jump", "ui.accept"
    std::string label;        // shown in the bindings screen
    std::string category;     // groups in the bindings screen ("Movement", "Combat", "Menus")
    std::string context = "game";
    ActionType type = ActionType::Button;
    bool clamp = true;        // clamp the summed value to -1..1 (length 1 for 2D); off for mouse look
};

struct ActionState {
    bool held = false;        // active now
    bool pressed = false;     // became active (or fired) this update
    bool released = false;    // stopped being active this update
    float value = 0.0f;       // Button: 0/1; Axis1D: summed value
    glm::vec2 value2{0.0f};   // Axis2D
    double heldFor = 0.0;     // seconds active
};

class InputMap {
public:
    void defineAction(const ActionDef& def);
    const std::vector<ActionDef>& actions() const { return m_actions; }
    const ActionDef* action(const std::string& id) const;

    // Bindings. Indices are stable until a removal.
    size_t addBinding(const Binding& b);
    void removeBinding(size_t index);
    void clearBindings(const std::string& action);
    const std::vector<Binding>& bindings() const { return m_bindings; }
    Binding& binding(size_t index) { return m_bindings[index]; }
    std::vector<size_t> bindingsFor(const std::string& action) const;
    // Other bindings (same context) that use the same source + modifiers +
    // trigger: shown as conflicts in the bindings screen.
    std::vector<size_t> conflicts(size_t index) const;

    void setContextEnabled(const std::string& context, bool enabled);
    bool contextEnabled(const std::string& context) const;
    // Devices this map listens to for "any device" sources (split screen).
    void setDevices(std::vector<uint32_t> devices) { m_devices = std::move(devices); }

    // Evaluates every binding. `now` in seconds (monotonic).
    void update(const InputState& state, double now);
    const ActionState& state(const std::string& action) const;
    bool held(const std::string& a) const { return state(a).held; }
    bool pressed(const std::string& a) const { return state(a).pressed; }
    bool released(const std::string& a) const { return state(a).released; }
    float axis(const std::string& a) const { return state(a).value; }
    glm::vec2 axis2(const std::string& a) const { return state(a).value2; }
    // Forget edges and toggles (after a rebind, a context switch, focus loss).
    void resetStates();

    // Bindings as JSON (actions are code-defined). load() keeps actions and
    // replaces bindings; unknown actions are dropped with a count.
    nlohmann::json save() const;
    int load(const nlohmann::json& j);
    // Snapshot of the current bindings as the defaults, and back.
    void storeDefaults() { m_defaults = m_bindings; }
    void restoreDefaults(const std::string& action = {});

private:
    struct Runtime {
        bool down = false;       // source + modifiers active last update
        double downAt = 0.0;
        double lastTapAt = -1e9; // release time of the last short press (double-tap candidate)
        bool holdFired = false;
        bool pendingTap = false; // tap waiting to see whether a double tap follows
        double pendingAt = 0.0;
        bool toggled = false;
        bool swallowNextTap = false;  // the release after a double tap isn't a tap
    };
    float sourceValue(const InputState& s, const InputSource& src) const;
    float shaped(const Binding& b, float v) const;
    bool hasDoubleTapSibling(size_t index) const;
    void rebuildIndex();

    std::vector<ActionDef> m_actions;
    std::unordered_map<std::string, size_t> m_actionIndex;
    std::vector<Binding> m_bindings, m_defaults;
    std::vector<Runtime> m_runtime;
    std::vector<ActionState> m_states;
    std::vector<std::string> m_disabledContexts;
    std::vector<uint32_t> m_devices;
    double m_lastUpdate = -1.0;
    ActionState m_none;
};

// JSON names for enums (used by save/load and the bindings UI).
const char* toString(SourceKind k);
const char* toString(Trigger t);
const char* toString(ActionType t);
SourceKind sourceKindFromString(const std::string& s);
Trigger triggerFromString(const std::string& s);
nlohmann::json toJson(const InputSource& s);
InputSource sourceFromJson(const nlohmann::json& j);

} // namespace kke
