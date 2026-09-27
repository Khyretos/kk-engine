#pragma once

#include "kke/ButtonPrompts.h"
#include "kke/InputDevices.h"
#include "kke/InputSanity.h"
#include "kke/InputMap.h"
#include "kke/Module.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kke {

// Owns the input devices and one InputMap per local player (split screen:
// each player's map listens to the devices assigned to them). Polls and
// evaluates in frameStart, so bindings work while the game is paused.
//
// Games define their actions and default bindings in init(), then call
// commitDefaults(): that stores them as the defaults ("Reset") and loads
// the user's saved bindings from the input file over them.
//
// Built-in "ui" context actions (ui.up/down/left/right/accept/back/
// prev/next) drive RmlUi navigation from a controller (UiModule reads
// them); remap them like any other action. See docs/INPUT.md.
class InputModule : public Module {
public:
    explicit InputModule(std::string path = "input.json", int players = 1);
    const char* name() const override { return "Input"; }
    void init(Application& app) override;
    void frameStart(const UpdateContext& ctx) override;
    void frameEnd() override;
    void onEvent(const SDL_Event& event) override;
    void shutdown() override;

    InputDevices& devices() { return m_devices; }
    InputMap& map(int player = 0) { return *m_maps[static_cast<size_t>(player)]; }
    int players() const { return static_cast<int>(m_maps.size()); }
    void setPlayers(int count);
    // Split screen: which devices player `p` listens to (empty = all).
    void assignDevices(int player, std::vector<uint32_t> devices);

    // Button prompts (kke/ButtonPrompts.h): which glyphs player `p` should
    // see, following the device they touched last (a key or the mouse:
    // keyboard; a pad button or a stick pushed past half: that pad's make;
    // a finger on the screen: touch). Starts as touch on phones, as the
    // Deck on a Steam Deck, else keyboard. promptSerial() changes whenever
    // any player's style does. KKE_PROMPT_STYLE=xbox|playstation|switch|
    // steamdeck|steamcontroller|keyboard|touch forces one (developer
    // builds; games can offer the same as a setting with forcePromptStyle).
    PromptStyle promptStyle(int player = 0) const;
    uint32_t promptSerial() const { return m_promptSerial; }
    void forcePromptStyle(std::optional<PromptStyle> style);
    ButtonPrompts& prompts() { return m_prompts; }
    const ButtonPrompts& prompts() const { return m_prompts; }
    // The style of one device (for "press A to join" under a pad that
    // was just plugged in, before it belongs to anyone).
    static PromptStyle promptStyleFor(const InputDevices::Device& d);
    // Markup for an action as player `p` sees it right now (see
    // ButtonPrompts::rml / format).
    std::string promptRml(const std::string& action, const std::string& label = {}, int player = 0) const;
    std::string promptText(const std::string& text, int player = 0) const;

    void commitDefaults();
    bool save() const;
    bool load();
    const std::string& path() const { return m_path; }

    // Convenience for defaults: bind(action, source, trigger).
    static Binding bind(const std::string& action, InputSource s, Trigger t = Trigger::Press);
    static InputSource key(SDL_Scancode sc) { return { SourceKind::Key, 0, static_cast<int32_t>(sc), 0 }; }
    static InputSource mouse(int button) { return { SourceKind::MouseButton, 0, button, 0 }; }
    static InputSource pad(SDL_GamepadButton b) { return { SourceKind::GamepadButton, 0, static_cast<int32_t>(b), 0 }; }
    static InputSource padAxis(SDL_GamepadAxis a, int8_t half = 0) { return { SourceKind::GamepadAxis, 0, static_cast<int32_t>(a), half }; }

    // The usual third/first-person character actions and their default
    // bindings (keyboard+mouse and gamepad): move, look (mouse), look.rate
    // (stick/gyro, 1 = full speed), jump, sprint, walk, crouch, fire, aim,
    // interact, camera.toggle, camera.zoom. Games add their own on top.
    static void defineCharacterActions(InputMap& m);
    // Left-handed layout: every keyboard binding moves to its mirror image
    // across the keyboard (W->O, A->;, D->K, Q->P, LShift->RShift, 1->0),
    // with left/right axis directions swapped back so "left" stays left.
    // Mouse and controller bindings are untouched.
    static void mirrorKeyboard(InputMap& m);
    static SDL_Scancode mirrorScancode(SDL_Scancode sc);

    // Virtual devices (SDL virtual joysticks) for testing without
    // hardware: KKE_VIRTUAL_INPUT=hosas,pad attaches two identical flight
    // sticks and a gamepad with gyro (pad,pad,pad: three gamepads, for
    // local multiplayer); KKE_VIRTUAL_INPUT_ANIMATE=1 moves them;
    // KKE_VIRTUAL_INPUT_LATE=<s>:<spec> plugs <spec> in <s> seconds in
    // (hot-plugging: "controller connected" prompts);
    // KKE_VIRTUAL_PAD_SCRIPT="3:back,3.6:dpad_down,4:leftx=1,5:leftx=0"
    // plays presses on the first virtual pad (<seconds>:<button> taps it
    // for 0.3 s; <seconds>:<axis>=<value> sets a stick or trigger until
    // the next change): driving a game's menus and controls headless.
    // Used by CI and screenshots. Developer builds only: shipping
    // builds ignore these variables (kke/DevTools.h).
    void attachVirtualDevices(const std::string& spec);

    // Every local key, mouse button, pad button and pad stick, judged for
    // input no human makes: rapid-fire mods, macros, scripted sticks
    // (kke/InputSanity.h, docs/ANTI_CHEAT.md). Findings are logged at info
    // level (evidence, not an engine fault); games read them here (or set
    // sanity().onFinding) to send them to their server or show them to
    // moderators. Never acts alone.
    InputSanity& sanity() { return m_sanity; }
    // The control ids it reports: kind in the top 4 bits (1 key, 2 mouse
    // button, 3 pad button, 4 pad axis), then the pad (low 12 bits of its
    // id) and the SDL scancode / button / axis in the low 8 or 16 bits.
    static uint32_t sanityControl(const SDL_Event& event);

private:
    void defineUiActions(InputMap& m);
    void animateVirtualDevices(float t);
    void playPadScript(double now);
    struct PadStep { double at = 0.0; int button = -1, axis = -1; float value = 0.0f; };
    std::vector<PadStep> m_padScript; // KKE_VIRTUAL_PAD_SCRIPT, in time order
    std::vector<std::pair<double, int>> m_padReleases; // (time, button) of scripted taps
    struct Virtual { SDL_JoystickID id = 0; SDL_Joystick* joy = nullptr; bool pad = false; int index = 0; };
    std::vector<Virtual> m_virtual;
    bool m_animateVirtual = false;
    double m_lateAt = 0.0;
    std::string m_lateSpec;

    void notePromptDevice(uint32_t deviceRef, bool keyboardOrMouse, PromptStyle style);
    void setPromptStyle(int player, PromptStyle style);
    ButtonPrompts m_prompts;
    std::vector<PromptStyle> m_promptStyles;
    std::optional<PromptStyle> m_forcedStyle;
    uint32_t m_promptSerial = 0;
    bool m_promptTouched = false; // a real input has set the style (else the start-up guess may change)

    std::string m_path;
    InputDevices m_devices;
    std::vector<std::unique_ptr<InputMap>> m_maps;
    double m_now = 0.0;
    InputSanity m_sanity;
};

} // namespace kke
