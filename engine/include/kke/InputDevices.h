#pragma once

#include "kke/InputMap.h"

#include <SDL3/SDL.h>
#include <glm/glm.hpp>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace kke {

// Every input device SDL can see, with an identity that survives restarts
// and hot-plugging, and a value for every InputSource (InputState).
//
// Identity: devices get a stable key = vendor:product + serial number, or
// the USB port (Linux: the sysfs topology, not /dev/input/eventN which
// changes between boots; Windows: the HID instance path), or the name and
// an ordinal. Its hash is the `device` field bindings use. Identical twins
// (two of the same flight stick in a HOSAS setup, two of the same pad) are
// grouped and numbered #1/#2 by port so they keep their bindings, and the
// user can name them ("Left stick") and swap them in the input tester.
//
// Gamepads (SDL's standard layout, incl. paddles/QAM) are also raw
// joysticks, so buttons SDL doesn't map are still bindable. Sensors (gyro,
// accelerometer), touchpads, rumble and LEDs are opened when present.
// Keyboards and mice are devices too (a left-hand keypad can be bound on
// its own) where the platform reports them separately.
class InputDevices : public InputState {
public:
    enum class Kind : uint8_t { Keyboard, Mouse, Gamepad, Joystick };

    struct Finger { bool down = false; float x = 0.0f, y = 0.0f, pressure = 0.0f; };

    struct Device {
        uint32_t ref = 0;              // stable, = hash(stableKey)
        Kind kind = Kind::Joystick;
        uint32_t sdlId = 0;            // SDL instance id while connected
        bool connected = false;
        std::string name, typeName, guid, serial, path, port, stableKey, alias;
        uint16_t vendor = 0, product = 0;
        int duplicateIndex = 0, duplicateCount = 1; // #1 of 2 identical devices
        // Capabilities.
        int numButtons = 0, numAxes = 0, numHats = 0, numTouchpads = 0;
        bool hasGyro = false, hasAccel = false, hasRumble = false, hasLed = false;
        bool isSteamVirtual = false;   // Steam Input's virtual pad (the real device is hidden by Steam)
        // State (joysticks/gamepads).
        std::vector<uint8_t> buttons;
        std::vector<float> axes;
        std::vector<uint8_t> hats;
        std::array<uint8_t, SDL_GAMEPAD_BUTTON_COUNT> padButtons{};
        std::array<float, SDL_GAMEPAD_AXIS_COUNT> padAxes{};
        glm::vec3 gyro{0.0f}, accel{0.0f}; // deg/s, m/s^2
        std::vector<std::vector<Finger>> touch;
        // State (keyboards/mice).
        std::vector<uint8_t> keys;
        std::array<uint8_t, 32> mouseButtons{};
        glm::vec2 motion{0.0f}, wheel{0.0f}; // this frame
        double lastActivity = -1.0;          // seconds (SDL ticks), for "which one did I just touch"
        SDL_Gamepad* gamepad = nullptr;
        SDL_Joystick* joystick = nullptr;

        std::string label() const;           // alias, or name (+ " #2")
    };

    // Identity for keying: pure, so HOSAS numbering is unit-tested.
    struct Identity { std::string name, serial, port, path; uint16_t vendor = 0, product = 0; };
    struct Keyed { std::string stableKey; uint32_t ref = 0; int duplicateIndex = 0, duplicateCount = 1; };
    static std::vector<Keyed> keyDevices(const std::vector<Identity>& ids);
    static uint32_t hashKey(const std::string& key);
    // Linux: /dev/input/eventN or /dev/hidrawN -> "usb-1-2:1.0"-style port; "" elsewhere.
    static std::string portFromPath(const std::string& path);

    struct Options {
        bool steamControllerHidapi = true; // SDL's own Steam Controller driver (gyro, paddles, QAM)
        bool gamepadSensors = true;
        bool backgroundEvents = false;     // keep reading pads while unfocused
    };
    void init(const Options& options);
    void init() { init(Options{}); }
    void shutdown();
    ~InputDevices() override;

    // Call for every SDL event, then poll() once per frame before reading,
    // then endFrame() after everything read this frame's deltas.
    void handleEvent(const SDL_Event& e);
    void poll();
    void endFrame();

    const std::vector<Device>& devices() const { return m_devices; }
    const Device* find(uint32_t ref) const;
    Device* find(uint32_t ref);
    // The device touched most recently (buttons/axes past 0.5): for "press
    // a button on the stick you want to name".
    const Device* lastActive() const;
    void setAlias(uint32_t ref, const std::string& alias);
    // Swaps two identical devices' identities (plugged into each other's ports).
    void swapIdentities(uint32_t a, uint32_t b);
    bool rumble(uint32_t ref, float low, float high, uint32_t ms);
    bool setLed(uint32_t ref, glm::vec3 rgb);
    void identify(uint32_t ref); // rumble + LED flash where supported

    float value(const InputSource& source, const std::vector<uint32_t>* allowed) const override;
    std::string describe(const InputSource& s) const; // "Keyboard W", "Pad Cross", "HOSAS #2 Button 5"

    // Rebinding: press a combination, release to finish. Earlier inputs
    // still held become modifiers (Ctrl, a HOSAS shift button, LB).
    struct CaptureOptions {
        bool allowMouseMotion = false; // for axis actions: move the mouse to bind it
        bool allowGyro = false;        // for axis actions: turn the controller
        bool escapeCancels = true;
    };
    struct Captured { InputSource source; std::vector<InputSource> modifiers; };
    enum class CaptureStatus { Idle, Listening, Done, Cancelled };
    void beginCapture(const CaptureOptions& options);
    void cancelCapture() { m_capture = CaptureStatus::Cancelled; }
    CaptureStatus captureStatus() const { return m_capture; }
    const Captured& captured() const { return m_captured; }

    // Aliases and swapped identities, as JSON (lives next to the bindings).
    nlohmann::json saveDevices() const;
    void loadDevices(const nlohmann::json& j);

private:
    Device& addOrReconnect(Device&& d);
    void openJoystick(SDL_JoystickID id);
    void closeJoystick(SDL_JoystickID id);
    void refreshKeying();
    Device& keyboardFor(SDL_KeyboardID id);
    Device& mouseFor(SDL_MouseID id);
    void captureTick();
    bool deviceMatches(const Device& d, const InputSource& s, const std::vector<uint32_t>* allowed) const;
    float deviceValue(const Device& d, const InputSource& s) const;

    std::vector<Device> m_devices;
    std::map<std::string, std::string> m_aliases;  // stableKey -> alias
    std::map<std::string, std::string> m_swapped;  // stableKey -> the key it answers to
    Options m_options;
    bool m_initialized = false;

    CaptureStatus m_capture = CaptureStatus::Idle;
    CaptureOptions m_captureOptions;
    Captured m_captured;
    std::vector<InputSource> m_captureDown;               // pressed since beginCapture, in order
    std::map<uint32_t, std::vector<float>> m_captureRest; // axis baselines per device
    std::map<uint32_t, std::array<float, SDL_GAMEPAD_AXIS_COUNT>> m_captureRestPad;
    glm::vec2 m_captureMotion{0.0f};
};

} // namespace kke
