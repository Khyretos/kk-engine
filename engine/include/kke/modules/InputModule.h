#pragma once

#include "kke/InputDevices.h"
#include "kke/InputMap.h"
#include "kke/Module.h"

#include <memory>
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
// them); remap them like any other action. See INPUT.md.
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
    // sticks and a gamepad with gyro; KKE_VIRTUAL_INPUT_ANIMATE=1 moves
    // them. Used by CI and screenshots; harmless otherwise.
    void attachVirtualDevices(const std::string& spec);

private:
    void defineUiActions(InputMap& m);
    void animateVirtualDevices(float t);
    struct Virtual { SDL_JoystickID id = 0; SDL_Joystick* joy = nullptr; bool pad = false; int index = 0; };
    std::vector<Virtual> m_virtual;
    bool m_animateVirtual = false;

    std::string m_path;
    InputDevices m_devices;
    std::vector<std::unique_ptr<InputMap>> m_maps;
    double m_now = 0.0;
};

} // namespace kke
