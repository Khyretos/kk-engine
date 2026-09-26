#include "kke/InputDevices.h"

#include "kke/Log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace kke {

namespace {

double nowSeconds() { return static_cast<double>(SDL_GetTicksNS()) * 1e-9; }

float axisValue(Sint16 v) { return std::clamp(static_cast<float>(v) / 32767.0f, -1.0f, 1.0f); }

std::string guidString(SDL_GUID guid) {
    char buf[33] = {};
    SDL_GUIDToString(guid, buf, sizeof(buf));
    return buf;
}

bool isHidInstance(const std::string& c) {
    // "0003:046D:C21D.0005": bus:vendor:product.instance (the instance
    // number changes on every plug, so the port path stops before it).
    return c.size() == 19 && c[4] == ':' && c[9] == ':' && c[14] == '.';
}

const char* kPadButtonNames[] = { "A",           "B",       "X",        "Y",        "Back",      "Guide",   "Start",
                                  "L3",          "R3",      "LB",       "RB",       "D-pad Up",  "D-pad Down",
                                  "D-pad Left",  "D-pad Right", "Misc (Share/QAM)", "R4", "L4", "R5", "L5",
                                  "Touchpad",    "Misc 2",  "Misc 3",   "Misc 4",   "Misc 5",    "Misc 6" };
const char* kPadAxisNames[] = { "Left stick X", "Left stick Y", "Right stick X", "Right stick Y", "LT", "RT" };

bool sourceIsKeyboard(SourceKind k) { return k == SourceKind::Key; }
bool sourceIsMouse(SourceKind k) { return k == SourceKind::MouseButton || k == SourceKind::MouseWheel || k == SourceKind::MouseMotion; }
bool sourceIsPad(SourceKind k) {
    return k == SourceKind::GamepadButton || k == SourceKind::GamepadAxis || k == SourceKind::Gyro || k == SourceKind::Accel ||
           k == SourceKind::TouchX || k == SourceKind::TouchY;
}
bool sourceIsJoy(SourceKind k) { return k == SourceKind::JoyButton || k == SourceKind::JoyAxis || k == SourceKind::JoyHat; }

} // namespace

std::string InputDevices::Device::label() const {
    if (!alias.empty()) return alias;
    if (duplicateCount > 1) return name + " #" + std::to_string(duplicateIndex + 1);
    return name;
}

uint32_t InputDevices::hashKey(const std::string& key) {
    uint32_t h = 2166136261u; // FNV-1a: tiny, stable across platforms and runs
    for (unsigned char c : key) { h ^= c; h *= 16777619u; }
    return h ? h : 1u;
}

std::string InputDevices::portFromPath(const std::string& path) {
#if defined(__linux__)
    std::string sys;
    if (path.rfind("/dev/input/event", 0) == 0) sys = "/sys/class/input/" + path.substr(11) + "/device";
    else if (path.rfind("/dev/hidraw", 0) == 0) sys = "/sys/class/hidraw/" + path.substr(5) + "/device";
    if (sys.empty()) return {};
    std::error_code ec;
    std::filesystem::path real = std::filesystem::canonical(sys, ec);
    if (ec) return {};
    std::string out;
    for (const auto& part : real) {
        const std::string c = part.string();
        if (c == "/" || c == "sys" || c == "devices") continue;
        if (isHidInstance(c) || c == "input" || c == "hidraw") break;
        out += (out.empty() ? "" : "/") + c;
    }
    return out;
#else
    (void)path;
    return {};
#endif
}

std::vector<InputDevices::Keyed> InputDevices::keyDevices(const std::vector<Identity>& ids) {
    std::vector<Keyed> out(ids.size());
    auto hex4 = [](uint16_t v) {
        char b[8];
        std::snprintf(b, sizeof(b), "%04x", v);
        return std::string(b);
    };
    auto usableSerial = [](const std::string& s) {
        return !s.empty() && s.find_first_not_of("0 ") != std::string::npos;
    };
    for (size_t i = 0; i < ids.size(); ++i) {
        const Identity& d = ids[i];
        std::string base = (d.vendor || d.product) ? hex4(d.vendor) + ":" + hex4(d.product) : "name:" + d.name;
        if (usableSerial(d.serial)) base += "/sn:" + d.serial;
        else if (!d.port.empty()) base += "/port:" + d.port;
        else if (!d.path.empty()) base += "/path:" + d.path;
        else base += "/n:" + d.name;
        out[i].stableKey = base;
    }
    // Identical models: number them in a stable order (port, then path,
    // then serial) so #1 stays #1 across restarts.
    std::vector<size_t> order(ids.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    auto model = [&](size_t i) { return std::make_tuple(ids[i].vendor, ids[i].product, ids[i].name); };
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (model(a) != model(b)) return model(a) < model(b);
        return std::tie(ids[a].port, ids[a].path, ids[a].serial) < std::tie(ids[b].port, ids[b].path, ids[b].serial);
    });
    for (size_t s = 0; s < order.size();) {
        size_t e = s;
        while (e < order.size() && model(order[e]) == model(order[s])) ++e;
        for (size_t k = s; k < e; ++k) {
            out[order[k]].duplicateIndex = static_cast<int>(k - s);
            out[order[k]].duplicateCount = static_cast<int>(e - s);
        }
        s = e;
    }
    // Keys that still collide (same model, nothing to tell them apart):
    // fall back to the numbering.
    for (size_t i = 0; i < out.size(); ++i)
        for (size_t j = i + 1; j < out.size(); ++j)
            if (out[i].stableKey == out[j].stableKey) out[j].stableKey += "#" + std::to_string(out[j].duplicateIndex);
    for (Keyed& k : out) k.ref = hashKey(k.stableKey);
    return out;
}

// ---------------------------------------------------------------- lifetime

void InputDevices::init(const Options& options) {
    m_options = options;
    // Hints must be set before the joystick subsystem starts.
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1"); // Windows: raw input on its own thread, no missed events
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, options.steamControllerHidapi ? "1" : "0");
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, options.backgroundEvents ? "1" : "0");
    auto log = log::get("Input");
    // SDL3 returns true on success (SDL2 returned 0).
    if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
        log->warn("joystick/gamepad support unavailable: {}", SDL_GetError());
    if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) log->info("sensors unavailable: {}", SDL_GetError());
    m_initialized = true;

    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&count)) {
        for (int i = 0; i < count; ++i) openJoystick(ids[i]);
        SDL_free(ids);
    }
    if (SDL_KeyboardID* ids = SDL_GetKeyboards(&count)) {
        for (int i = 0; i < count; ++i) keyboardFor(ids[i]);
        SDL_free(ids);
    }
    if (SDL_MouseID* ids = SDL_GetMice(&count)) {
        for (int i = 0; i < count; ++i) mouseFor(ids[i]);
        SDL_free(ids);
    }
    keyboardFor(0); // events without a device id (virtual keyboards, some platforms)
    mouseFor(0);
    for (const Device& d : m_devices)
        if (d.kind == Kind::Gamepad || d.kind == Kind::Joystick)
            log->info("{}: {} ({}, {} buttons, {} axes, {} hats{}{})", d.kind == Kind::Gamepad ? "gamepad" : "joystick", d.label(),
                      d.stableKey, d.numButtons, d.numAxes, d.numHats, d.hasGyro ? ", gyro" : "", d.duplicateCount > 1 ? ", identical twin" : "");
}

void InputDevices::shutdown() {
    for (Device& d : m_devices) {
        if (d.gamepad) SDL_CloseGamepad(d.gamepad);
        else if (d.joystick) SDL_CloseJoystick(d.joystick);
        d.gamepad = nullptr;
        d.joystick = nullptr;
        d.connected = false;
    }
    if (m_initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_SENSOR);
    m_initialized = false;
}

InputDevices::~InputDevices() { shutdown(); }

InputDevices::Device& InputDevices::addOrReconnect(Device&& d) {
    for (Device& e : m_devices)
        if (e.stableKey == d.stableKey && !e.connected && e.kind == d.kind) {
            d.alias = e.alias;
            e = std::move(d);
            return e;
        }
    m_devices.push_back(std::move(d));
    return m_devices.back();
}

void InputDevices::openJoystick(SDL_JoystickID id) {
    for (const Device& d : m_devices)
        if (d.connected && d.sdlId == id && (d.kind == Kind::Gamepad || d.kind == Kind::Joystick)) return;
    Device d;
    d.sdlId = id;
    if (SDL_IsGamepad(id)) {
        d.gamepad = SDL_OpenGamepad(id);
        if (d.gamepad) d.joystick = SDL_GetGamepadJoystick(d.gamepad);
    }
    if (!d.joystick) d.joystick = SDL_OpenJoystick(id);
    if (!d.joystick) {
        log::get("Input")->warn("could not open joystick {}: {}", id, SDL_GetError());
        return;
    }
    d.kind = d.gamepad ? Kind::Gamepad : Kind::Joystick;
    d.connected = true;
    const char* name = SDL_GetJoystickName(d.joystick);
    d.name = name ? name : "Joystick";
    const char* path = SDL_GetJoystickPath(d.joystick);
    d.path = path ? path : "";
    d.port = portFromPath(d.path);
    const char* serial = SDL_GetJoystickSerial(d.joystick);
    d.serial = serial ? serial : "";
    d.vendor = SDL_GetJoystickVendor(d.joystick);
    d.product = SDL_GetJoystickProduct(d.joystick);
    d.guid = guidString(SDL_GetJoystickGUID(d.joystick));
    d.numButtons = std::max(0, SDL_GetNumJoystickButtons(d.joystick));
    d.numAxes = std::max(0, SDL_GetNumJoystickAxes(d.joystick));
    d.numHats = std::max(0, SDL_GetNumJoystickHats(d.joystick));
    d.buttons.assign(d.numButtons, 0);
    d.axes.assign(d.numAxes, 0.0f);
    d.hats.assign(d.numHats, 0);
    SDL_PropertiesID props = SDL_GetJoystickProperties(d.joystick);
    d.hasRumble = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, false);
    d.hasLed = SDL_GetBooleanProperty(props, SDL_PROP_JOYSTICK_CAP_RGB_LED_BOOLEAN, false);
    d.isSteamVirtual = d.name.find("Steam Virtual") != std::string::npos || d.name.find("Steam Deck") != std::string::npos;
    if (d.gamepad) {
        const char* t = SDL_GetGamepadStringForType(SDL_GetGamepadType(d.gamepad));
        d.typeName = t ? t : "gamepad";
        if (m_options.gamepadSensors) {
            if (SDL_GamepadHasSensor(d.gamepad, SDL_SENSOR_GYRO)) d.hasGyro = SDL_SetGamepadSensorEnabled(d.gamepad, SDL_SENSOR_GYRO, true);
            if (SDL_GamepadHasSensor(d.gamepad, SDL_SENSOR_ACCEL)) d.hasAccel = SDL_SetGamepadSensorEnabled(d.gamepad, SDL_SENSOR_ACCEL, true);
        }
        d.numTouchpads = std::max(0, SDL_GetNumGamepadTouchpads(d.gamepad));
        d.touch.resize(d.numTouchpads);
        for (int t2 = 0; t2 < d.numTouchpads; ++t2) d.touch[t2].resize(std::max(0, SDL_GetNumGamepadTouchpadFingers(d.gamepad, t2)));
    } else {
        d.typeName = "joystick";
    }
    d.lastActivity = nowSeconds();
    Device& added = addOrReconnect(std::move(d));
    (void)added;
    refreshKeying();
    for (const Device& e : m_devices)
        if (e.connected && e.sdlId == id && (e.kind == Kind::Gamepad || e.kind == Kind::Joystick))
            log::get("Input")->info("connected {} ({})", e.label(), e.stableKey);
}

void InputDevices::closeJoystick(SDL_JoystickID id) {
    for (Device& d : m_devices) {
        if (!d.connected || d.sdlId != id || (d.kind != Kind::Gamepad && d.kind != Kind::Joystick)) continue;
        log::get("Input")->info("disconnected {}", d.label());
        if (d.gamepad) SDL_CloseGamepad(d.gamepad);
        else if (d.joystick) SDL_CloseJoystick(d.joystick);
        d.gamepad = nullptr;
        d.joystick = nullptr;
        d.connected = false;
        std::fill(d.buttons.begin(), d.buttons.end(), 0);
        std::fill(d.axes.begin(), d.axes.end(), 0.0f);
        std::fill(d.hats.begin(), d.hats.end(), 0);
        d.padButtons.fill(0);
        d.padAxes.fill(0.0f);
    }
}

// Recomputes keys and twin numbering for connected joysticks; a device that
// comes back takes over its old (disconnected) entry, alias included.
void InputDevices::refreshKeying() {
    std::vector<size_t> idx;
    std::vector<Identity> ids;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        const Device& d = m_devices[i];
        if (!d.connected || (d.kind != Kind::Gamepad && d.kind != Kind::Joystick)) continue;
        idx.push_back(i);
        ids.push_back({ d.name, d.serial, d.port, d.path, d.vendor, d.product });
    }
    std::vector<Keyed> keys = keyDevices(ids);
    for (size_t k = 0; k < idx.size(); ++k) {
        Device& d = m_devices[idx[k]];
        std::string key = keys[k].stableKey;
        if (auto s = m_swapped.find(key); s != m_swapped.end()) key = s->second;
        d.stableKey = key;
        d.ref = hashKey(key);
        d.duplicateIndex = keys[k].duplicateIndex;
        d.duplicateCount = keys[k].duplicateCount;
        if (auto a = m_aliases.find(key); a != m_aliases.end()) d.alias = a->second;
    }
    // Drop stale disconnected copies of a key that is connected again.
    for (size_t i = m_devices.size(); i-- > 0;) {
        const Device& d = m_devices[i];
        if (d.connected) continue;
        for (const Device& o : m_devices)
            if (&o != &d && o.connected && o.stableKey == d.stableKey) {
                m_devices.erase(m_devices.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    }
}

InputDevices::Device& InputDevices::keyboardFor(SDL_KeyboardID id) {
    for (Device& d : m_devices)
        if (d.kind == Kind::Keyboard && d.sdlId == id) return d;
    Device d;
    d.kind = Kind::Keyboard;
    d.sdlId = id;
    d.connected = true;
    const char* name = id ? SDL_GetKeyboardNameForID(id) : nullptr;
    d.name = name && *name ? name : "Keyboard";
    int same = 0;
    for (const Device& o : m_devices) same += o.kind == Kind::Keyboard && o.name == d.name;
    d.stableKey = "keyboard:" + d.name + "#" + std::to_string(same);
    d.ref = hashKey(d.stableKey);
    d.keys.assign(SDL_SCANCODE_COUNT, 0);
    d.typeName = "keyboard";
    if (auto a = m_aliases.find(d.stableKey); a != m_aliases.end()) d.alias = a->second;
    m_devices.push_back(std::move(d));
    return m_devices.back();
}

InputDevices::Device& InputDevices::mouseFor(SDL_MouseID id) {
    for (Device& d : m_devices)
        if (d.kind == Kind::Mouse && d.sdlId == id) return d;
    Device d;
    d.kind = Kind::Mouse;
    d.sdlId = id;
    d.connected = true;
    const char* name = id ? SDL_GetMouseNameForID(id) : nullptr;
    d.name = name && *name ? name : "Mouse";
    int same = 0;
    for (const Device& o : m_devices) same += o.kind == Kind::Mouse && o.name == d.name;
    d.stableKey = "mouse:" + d.name + "#" + std::to_string(same);
    d.ref = hashKey(d.stableKey);
    d.typeName = "mouse";
    if (auto a = m_aliases.find(d.stableKey); a != m_aliases.end()) d.alias = a->second;
    m_devices.push_back(std::move(d));
    return m_devices.back();
}

// ---------------------------------------------------------------- events

void InputDevices::handleEvent(const SDL_Event& e) {
    const double t = nowSeconds();
    const bool listening = m_capture == CaptureStatus::Listening;
    auto captureDown = [&](const InputSource& s) {
        if (std::find(m_captureDown.begin(), m_captureDown.end(), s) == m_captureDown.end()) m_captureDown.push_back(s);
    };
    auto captureUp = [&](const InputSource& s) {
        if (std::find(m_captureDown.begin(), m_captureDown.end(), s) == m_captureDown.end()) return;
        m_captured.source = m_captureDown.back();
        m_captured.modifiers.assign(m_captureDown.begin(), m_captureDown.end() - 1);
        m_capture = CaptureStatus::Done;
    };
    switch (e.type) {
    case SDL_EVENT_JOYSTICK_ADDED: openJoystick(e.jdevice.which); break;
    case SDL_EVENT_JOYSTICK_REMOVED: closeJoystick(e.jdevice.which); break;
    case SDL_EVENT_KEYBOARD_ADDED: keyboardFor(e.kdevice.which).connected = true; break;
    case SDL_EVENT_KEYBOARD_REMOVED: keyboardFor(e.kdevice.which).connected = false; break;
    case SDL_EVENT_MOUSE_ADDED: mouseFor(e.mdevice.which).connected = true; break;
    case SDL_EVENT_MOUSE_REMOVED: mouseFor(e.mdevice.which).connected = false; break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        Device& d = keyboardFor(e.key.which);
        if (e.key.scancode < d.keys.size()) d.keys[e.key.scancode] = e.key.down ? 1 : 0;
        d.lastActivity = t;
        if (listening && !e.key.repeat) {
            InputSource s{ SourceKind::Key, 0, static_cast<int32_t>(e.key.scancode), 0 };
            if (e.key.down) {
                if (m_captureOptions.escapeCancels && e.key.scancode == SDL_SCANCODE_ESCAPE && m_captureDown.empty()) m_capture = CaptureStatus::Cancelled;
                else captureDown(s);
            } else {
                captureUp(s);
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (e.button.which == SDL_TOUCH_MOUSEID || e.button.which == SDL_PEN_MOUSEID) break;
        Device& d = mouseFor(e.button.which);
        if (e.button.button < d.mouseButtons.size()) d.mouseButtons[e.button.button] = e.button.down ? 1 : 0;
        d.lastActivity = t;
        if (listening) {
            InputSource s{ SourceKind::MouseButton, 0, e.button.button, 0 };
            if (e.button.down) captureDown(s);
            else captureUp(s);
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (e.motion.which == SDL_TOUCH_MOUSEID || e.motion.which == SDL_PEN_MOUSEID) break;
        Device& d = mouseFor(e.motion.which);
        d.motion += glm::vec2(e.motion.xrel, e.motion.yrel);
        if (listening && m_captureOptions.allowMouseMotion && m_captureDown.empty()) {
            m_captureMotion += glm::vec2(e.motion.xrel, e.motion.yrel);
            if (std::max(std::abs(m_captureMotion.x), std::abs(m_captureMotion.y)) > 120.0f) {
                m_captured = { { SourceKind::MouseMotion, 0, std::abs(m_captureMotion.x) >= std::abs(m_captureMotion.y) ? 0 : 1, 0 }, {} };
                m_capture = CaptureStatus::Done;
            }
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        Device& d = mouseFor(e.wheel.which);
        const float flip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
        d.wheel += glm::vec2(e.wheel.x, e.wheel.y) * flip;
        d.lastActivity = t;
        if (listening) {
            const bool vertical = std::abs(e.wheel.y) >= std::abs(e.wheel.x);
            const float v = (vertical ? e.wheel.y : e.wheel.x) * flip;
            if (v != 0.0f) {
                m_captured.source = { SourceKind::MouseWheel, 0, vertical ? 0 : 1, static_cast<int8_t>(v > 0.0f ? 1 : -1) };
                m_captured.modifiers = m_captureDown;
                m_capture = CaptureStatus::Done;
            }
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        // No stuck keys after Alt+Tab.
        for (Device& d : m_devices) {
            std::fill(d.keys.begin(), d.keys.end(), 0);
            d.mouseButtons.fill(0);
        }
        break;
    default: break;
    }
}

void InputDevices::poll() {
    const double t = nowSeconds();
    for (Device& d : m_devices) {
        if (!d.connected || !d.joystick) continue;
        bool active = false;
        for (int i = 0; i < d.numButtons; ++i) {
            const uint8_t b = SDL_GetJoystickButton(d.joystick, i) ? 1 : 0;
            active = active || (b && !d.buttons[i]);
            d.buttons[i] = b;
        }
        for (int i = 0; i < d.numAxes; ++i) {
            const float v = axisValue(SDL_GetJoystickAxis(d.joystick, i));
            active = active || std::abs(v - d.axes[i]) > 0.25f;
            // Small moves still count once they've gone far enough from the
            // last "active" reading (slow deliberate stick moves).
            d.axes[i] = v;
        }
        for (int i = 0; i < d.numHats; ++i) {
            const uint8_t h = SDL_GetJoystickHat(d.joystick, i);
            active = active || (h && h != d.hats[i]);
            d.hats[i] = h;
        }
        if (d.gamepad) {
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
                const uint8_t v = SDL_GetGamepadButton(d.gamepad, static_cast<SDL_GamepadButton>(b)) ? 1 : 0;
                active = active || (v && !d.padButtons[b]);
                d.padButtons[b] = v;
            }
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) d.padAxes[a] = axisValue(SDL_GetGamepadAxis(d.gamepad, static_cast<SDL_GamepadAxis>(a)));
            float data[3];
            if (d.hasGyro && SDL_GetGamepadSensorData(d.gamepad, SDL_SENSOR_GYRO, data, 3))
                d.gyro = glm::degrees(glm::vec3(data[0], data[1], data[2]));
            if (d.hasAccel && SDL_GetGamepadSensorData(d.gamepad, SDL_SENSOR_ACCEL, data, 3)) d.accel = glm::vec3(data[0], data[1], data[2]);
            for (int tp = 0; tp < d.numTouchpads; ++tp)
                for (size_t f = 0; f < d.touch[tp].size(); ++f) {
                    Finger& fi = d.touch[tp][f];
                    bool down = false;
                    SDL_GetGamepadTouchpadFinger(d.gamepad, tp, static_cast<int>(f), &down, &fi.x, &fi.y, &fi.pressure);
                    active = active || (down && !fi.down);
                    fi.down = down;
                }
        }
        if (active) d.lastActivity = t;
    }
    if (m_capture == CaptureStatus::Listening) captureTick();
}

void InputDevices::endFrame() {
    for (Device& d : m_devices) {
        d.motion = glm::vec2(0.0f);
        d.wheel = glm::vec2(0.0f);
    }
}

// ---------------------------------------------------------------- queries

const InputDevices::Device* InputDevices::find(uint32_t ref) const {
    for (const Device& d : m_devices)
        if (d.ref == ref) return &d;
    return nullptr;
}

InputDevices::Device* InputDevices::find(uint32_t ref) {
    for (Device& d : m_devices)
        if (d.ref == ref) return &d;
    return nullptr;
}

const InputDevices::Device* InputDevices::lastActive() const {
    const Device* best = nullptr;
    for (const Device& d : m_devices)
        if (d.connected && (d.kind == Kind::Gamepad || d.kind == Kind::Joystick) && (!best || d.lastActivity > best->lastActivity)) best = &d;
    return best;
}

void InputDevices::setAlias(uint32_t ref, const std::string& alias) {
    Device* d = find(ref);
    if (!d) return;
    d->alias = alias;
    if (alias.empty()) m_aliases.erase(d->stableKey);
    else m_aliases[d->stableKey] = alias;
}

void InputDevices::swapIdentities(uint32_t a, uint32_t b) {
    Device* da = find(a);
    Device* db = find(b);
    if (!da || !db || da == db) return;
    // Undo existing swaps first so swapping twice is a no-op.
    auto original = [&](const std::string& key) {
        for (const auto& [from, to] : m_swapped)
            if (to == key) return from;
        return key;
    };
    const std::string ka = original(da->stableKey), kb = original(db->stableKey);
    if (m_swapped.count(ka) && m_swapped[ka] == kb) {
        m_swapped.erase(ka);
        m_swapped.erase(kb);
    } else {
        m_swapped[ka] = kb;
        m_swapped[kb] = ka;
    }
    std::swap(da->stableKey, db->stableKey);
    std::swap(da->ref, db->ref);
    std::swap(da->alias, db->alias);
    std::swap(da->duplicateIndex, db->duplicateIndex);
}

bool InputDevices::rumble(uint32_t ref, float low, float high, uint32_t ms) {
    Device* d = find(ref);
    if (!d || !d->joystick) return false;
    auto u16 = [](float v) { return static_cast<Uint16>(std::clamp(v, 0.0f, 1.0f) * 65535.0f); };
    return SDL_RumbleJoystick(d->joystick, u16(low), u16(high), ms);
}

bool InputDevices::setLed(uint32_t ref, glm::vec3 rgb) {
    Device* d = find(ref);
    if (!d || !d->joystick) return false;
    auto u8 = [](float v) { return static_cast<Uint8>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
    return SDL_SetJoystickLED(d->joystick, u8(rgb.r), u8(rgb.g), u8(rgb.b));
}

void InputDevices::identify(uint32_t ref) {
    rumble(ref, 0.6f, 0.6f, 300);
    setLed(ref, glm::vec3(1.0f, 0.6f, 0.0f));
}

bool InputDevices::deviceMatches(const Device& d, const InputSource& s, const std::vector<uint32_t>* allowed) const {
    if (!d.connected) return false;
    if (allowed && std::find(allowed->begin(), allowed->end(), d.ref) == allowed->end()) return false;
    if (sourceIsKeyboard(s.kind)) return d.kind == Kind::Keyboard;
    if (sourceIsMouse(s.kind)) return d.kind == Kind::Mouse;
    if (sourceIsPad(s.kind)) return d.kind == Kind::Gamepad;
    if (sourceIsJoy(s.kind)) return d.kind == Kind::Gamepad || d.kind == Kind::Joystick;
    return false;
}

float InputDevices::deviceValue(const Device& d, const InputSource& s) const {
    const int c = s.code;
    switch (s.kind) {
    case SourceKind::Key: return c >= 0 && c < static_cast<int>(d.keys.size()) ? d.keys[c] : 0.0f;
    case SourceKind::MouseButton: return c >= 0 && c < static_cast<int>(d.mouseButtons.size()) ? d.mouseButtons[c] : 0.0f;
    case SourceKind::MouseWheel: return c == 0 ? d.wheel.y : d.wheel.x;
    case SourceKind::MouseMotion: return c == 0 ? d.motion.x : d.motion.y;
    case SourceKind::GamepadButton: return c >= 0 && c < SDL_GAMEPAD_BUTTON_COUNT ? d.padButtons[c] : 0.0f;
    case SourceKind::GamepadAxis: return c >= 0 && c < SDL_GAMEPAD_AXIS_COUNT ? d.padAxes[c] : 0.0f;
    case SourceKind::JoyButton: return c >= 0 && c < static_cast<int>(d.buttons.size()) ? d.buttons[c] : 0.0f;
    case SourceKind::JoyAxis: return c >= 0 && c < static_cast<int>(d.axes.size()) ? d.axes[c] : 0.0f;
    case SourceKind::JoyHat: {
        const int hat = c / 4, dir = c % 4;
        static const uint8_t bits[4] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
        return hat >= 0 && hat < static_cast<int>(d.hats.size()) && (d.hats[hat] & bits[dir]) ? 1.0f : 0.0f;
    }
    case SourceKind::Gyro: return c >= 0 && c < 3 ? d.gyro[c] : 0.0f;
    case SourceKind::Accel: return c >= 0 && c < 3 ? d.accel[c] : 0.0f;
    case SourceKind::TouchX:
    case SourceKind::TouchY: {
        const int tp = c / 8, f = c % 8;
        if (tp < 0 || tp >= static_cast<int>(d.touch.size()) || f >= static_cast<int>(d.touch[tp].size()) || !d.touch[tp][f].down) return 0.0f;
        return s.kind == SourceKind::TouchX ? d.touch[tp][f].x : d.touch[tp][f].y;
    }
    default: return 0.0f;
    }
}

float InputDevices::value(const InputSource& s, const std::vector<uint32_t>* allowed) const {
    if (s.device) {
        const Device* d = find(s.device);
        return d && d->connected ? deviceValue(*d, s) : 0.0f;
    }
    float best = 0.0f;
    for (const Device& d : m_devices) {
        if (!deviceMatches(d, s, allowed)) continue;
        const float v = deviceValue(d, s);
        if (s.isRelative()) best += v;               // two mice both move the view
        else if (std::abs(v) > std::abs(best)) best = v;
    }
    return best;
}

std::string InputDevices::describe(const InputSource& s) const {
    std::string prefix;
    const Device* dev = s.device ? find(s.device) : nullptr;
    if (dev) prefix = dev->label() + " ";
    const int c = s.code;
    auto dir = [&](const char* pos, const char* neg) { return s.half > 0 ? std::string(" ") + pos : s.half < 0 ? std::string(" ") + neg : std::string(); };
    switch (s.kind) {
    case SourceKind::None: return "(unbound)";
    case SourceKind::Key: {
        const char* n = SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(c), SDL_KMOD_NONE, false));
        if (!n || !*n) n = SDL_GetScancodeName(static_cast<SDL_Scancode>(c));
        return prefix + (n && *n ? n : "Key " + std::to_string(c));
    }
    case SourceKind::MouseButton: {
        static const char* names[] = { "?", "Left", "Middle", "Right", "Back", "Forward" };
        return prefix + "Mouse " + (c >= 1 && c <= 5 ? names[c] : "Button " + std::to_string(c));
    }
    case SourceKind::MouseWheel: return prefix + (c == 0 ? "Wheel" + dir("Up", "Down") : "Wheel" + dir("Right", "Left"));
    case SourceKind::MouseMotion: return prefix + (c == 0 ? "Mouse X" : "Mouse Y") + dir("+", "-");
    case SourceKind::GamepadButton: {
        std::string n = c >= 0 && c < static_cast<int>(std::size(kPadButtonNames)) ? kPadButtonNames[c] : "Button " + std::to_string(c);
        // Face buttons by what's printed on the pad (Cross on PlayStation, B on Nintendo).
        if (dev && dev->gamepad && c <= SDL_GAMEPAD_BUTTON_NORTH) {
            switch (SDL_GetGamepadButtonLabel(dev->gamepad, static_cast<SDL_GamepadButton>(c))) {
            case SDL_GAMEPAD_BUTTON_LABEL_A: n = "A"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_B: n = "B"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_X: n = "X"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_Y: n = "Y"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS: n = "Cross"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: n = "Circle"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: n = "Square"; break;
            case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: n = "Triangle"; break;
            default: break;
            }
        }
        return (dev ? prefix : std::string("Pad ")) + n;
    }
    case SourceKind::GamepadAxis: {
        std::string n = c >= 0 && c < SDL_GAMEPAD_AXIS_COUNT ? kPadAxisNames[c] : "Axis " + std::to_string(c);
        if (c == SDL_GAMEPAD_AXIS_LEFTY || c == SDL_GAMEPAD_AXIS_RIGHTY) n += dir("Down", "Up");
        else if (c <= SDL_GAMEPAD_AXIS_RIGHTX) n += dir("Right", "Left");
        return (dev ? prefix : std::string("Pad ")) + n;
    }
    case SourceKind::JoyButton: return (dev ? prefix : std::string("Joystick ")) + "Button " + std::to_string(c + 1);
    case SourceKind::JoyAxis: return (dev ? prefix : std::string("Joystick ")) + "Axis " + std::to_string(c + 1) + dir("+", "-");
    case SourceKind::JoyHat: {
        static const char* d4[] = { "Up", "Right", "Down", "Left" };
        return (dev ? prefix : std::string("Joystick ")) + "Hat " + std::to_string(c / 4 + 1) + " " + d4[c % 4];
    }
    case SourceKind::Gyro: {
        static const char* a[] = { "pitch", "yaw", "roll" };
        return (dev ? prefix : std::string("Pad ")) + "Gyro " + (c >= 0 && c < 3 ? a[c] : "?") + dir("+", "-");
    }
    case SourceKind::Accel: return (dev ? prefix : std::string("Pad ")) + "Tilt " + std::to_string(c) + dir("+", "-");
    case SourceKind::TouchX:
    case SourceKind::TouchY:
        return (dev ? prefix : std::string("Pad ")) + "Touchpad " + std::to_string(c / 8 + 1) + (s.kind == SourceKind::TouchX ? " X" : " Y");
    }
    return "?";
}

// ---------------------------------------------------------------- capture

void InputDevices::beginCapture(const CaptureOptions& options) {
    m_captureOptions = options;
    m_capture = CaptureStatus::Listening;
    m_captured = {};
    m_captureDown.clear();
    m_captureMotion = glm::vec2(0.0f);
    m_captureRest.clear();
    m_captureRestPad.clear();
    m_captureIgnore.clear();
    // Buttons already down (pad A that clicked "bind") don't count until
    // they've been released.
    for (const Device& d : m_devices) {
        if (!d.connected || !d.joystick) continue;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (d.padButtons[b]) m_captureIgnore.push_back({ SourceKind::GamepadButton, 0, b, 0 });
        for (int b = 0; b < d.numButtons; ++b)
            if (d.buttons[b]) m_captureIgnore.push_back({ SourceKind::JoyButton, d.ref, b, 0 });
        for (int h = 0; h < d.numHats; ++h)
            for (int dir = 0; dir < 4; ++dir)
                if (d.hats[h] & (1 << dir)) m_captureIgnore.push_back({ SourceKind::JoyHat, d.ref, h * 4 + dir, 0 });
    }
    // Rest positions: raw triggers and some throttles rest at -1 or
    // anywhere, so "moved" means moved from here.
    for (const Device& d : m_devices) {
        if (!d.connected || !d.joystick) continue;
        m_captureRest[d.ref] = d.axes;
        m_captureRestPad[d.ref] = d.padAxes;
    }
}

void InputDevices::captureTick() {
    auto isDown = [&](const InputSource& s) { return std::find(m_captureDown.begin(), m_captureDown.end(), s) != m_captureDown.end(); };
    std::vector<InputSource> now; // active this tick
    for (const Device& d : m_devices) {
        if (!d.connected || !d.joystick) continue;
        const auto& rest = m_captureRest[d.ref];
        const auto& restPad = m_captureRestPad[d.ref];
        bool padActive = false;
        if (d.gamepad) {
            // Standard-layout buttons bind to "any gamepad" (split screen
            // gives each player their own); everything raw names its device.
            for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
                if (d.padButtons[b]) { now.push_back({ SourceKind::GamepadButton, 0, b, 0 }); padActive = true; }
            for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
                const float delta = d.padAxes[a] - restPad[a];
                const bool held = isDown({ SourceKind::GamepadAxis, 0, a, static_cast<int8_t>(delta > 0 ? 1 : -1) });
                if (std::abs(delta) > (held ? 0.35f : 0.6f)) {
                    now.push_back({ SourceKind::GamepadAxis, 0, a, static_cast<int8_t>(delta > 0.0f ? 1 : -1) });
                    padActive = true;
                }
            }
            if (m_captureOptions.allowGyro && m_captureDown.empty())
                for (int a = 0; a < 3; ++a)
                    if (std::abs(d.gyro[a]) > 150.0f) {
                        m_captured = { { SourceKind::Gyro, 0, a, 0 }, {} };
                        m_capture = CaptureStatus::Done;
                        return;
                    }
        }
        // Raw inputs: always for plain joysticks; for gamepads only buttons
        // SDL doesn't map (extra paddles), i.e. when no standard input moved.
        if (!d.gamepad || !padActive) {
            for (int b = 0; b < d.numButtons; ++b)
                if (d.buttons[b] && !d.gamepad) now.push_back({ SourceKind::JoyButton, d.ref, b, 0 });
                else if (d.buttons[b] && d.gamepad && !padActive) now.push_back({ SourceKind::JoyButton, d.ref, b, 0 });
            if (!d.gamepad)
                for (int a = 0; a < d.numAxes; ++a) {
                    const float delta = d.axes[a] - (a < static_cast<int>(rest.size()) ? rest[a] : 0.0f);
                    const InputSource s{ SourceKind::JoyAxis, d.ref, a, static_cast<int8_t>(delta > 0.0f ? 1 : -1) };
                    if (std::abs(delta) > (isDown(s) ? 0.35f : 0.6f)) now.push_back(s);
                }
            for (int h = 0; h < d.numHats; ++h)
                for (int dir = 0; dir < 4; ++dir) {
                    static const uint8_t bits[4] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
                    if (d.hats[h] & bits[dir]) now.push_back({ SourceKind::JoyHat, d.ref, h * 4 + dir, 0 });
                }
        }
    }
    // Held-at-start inputs: forget them once released, never capture them.
    m_captureIgnore.erase(std::remove_if(m_captureIgnore.begin(), m_captureIgnore.end(),
                                         [&](const InputSource& s) { return std::find(now.begin(), now.end(), s) == now.end(); }),
                          m_captureIgnore.end());
    now.erase(std::remove_if(now.begin(), now.end(),
                             [&](const InputSource& s) { return std::find(m_captureIgnore.begin(), m_captureIgnore.end(), s) != m_captureIgnore.end(); }),
              now.end());
    // Presses in order; the first release finishes the combination.
    for (const InputSource& s : now)
        if (!isDown(s)) m_captureDown.push_back(s);
    for (const InputSource& s : m_captureDown) {
        const bool polled = sourceIsPad(s.kind) || sourceIsJoy(s.kind);
        if (!polled) continue; // keys/mouse finish from their events
        if (std::find(now.begin(), now.end(), s) == now.end()) {
            m_captured.source = m_captureDown.back();
            m_captured.modifiers.assign(m_captureDown.begin(), m_captureDown.end() - 1);
            m_capture = CaptureStatus::Done;
            return;
        }
    }
}

// ---------------------------------------------------------------- persistence

nlohmann::json InputDevices::saveDevices() const {
    nlohmann::json j = nlohmann::json::object();
    j["aliases"] = m_aliases;
    j["swapped"] = m_swapped;
    return j;
}

void InputDevices::loadDevices(const nlohmann::json& j) {
    if (!j.is_object()) return;
    if (j.contains("aliases") && j["aliases"].is_object())
        for (auto it = j["aliases"].begin(); it != j["aliases"].end(); ++it)
            if (it.value().is_string()) m_aliases[it.key()] = it.value().get<std::string>();
    if (j.contains("swapped") && j["swapped"].is_object())
        for (auto it = j["swapped"].begin(); it != j["swapped"].end(); ++it)
            if (it.value().is_string()) m_swapped[it.key()] = it.value().get<std::string>();
    refreshKeying();
    for (Device& d : m_devices)
        if (auto a = m_aliases.find(d.stableKey); a != m_aliases.end()) d.alias = a->second;
}

} // namespace kke
