#include "kke/modules/InputModule.h"

#include "kke/Application.h"

#include "kke/DataFile.h"
#include "kke/DevTools.h"
#include "kke/Log.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <cstdio>

namespace kke {

InputModule::InputModule(std::string path, int players) : m_path(std::move(path)) { setPlayers(players); }

void InputModule::setPlayers(int count) {
    count = std::clamp(count, 1, 8);
    while (static_cast<int>(m_maps.size()) < count) {
        auto m = std::make_unique<InputMap>();
        defineUiActions(*m);
        // A new player copies player 1's actions and bindings.
        if (!m_maps.empty()) {
            for (const ActionDef& a : m_maps[0]->actions()) m->defineAction(a);
            for (const Binding& b : m_maps[0]->bindings()) m->addBinding(b);
            m->storeDefaults();
        }
        m_maps.push_back(std::move(m));
    }
    m_promptStyles.resize(m_maps.size(), m_promptStyles.empty() ? PromptStyle::Keyboard : m_promptStyles.front());
    m_maps.resize(static_cast<size_t>(count));
}

void InputModule::assignDevices(int player, std::vector<uint32_t> devices) {
    if (player >= 0 && player < players()) m_maps[static_cast<size_t>(player)]->setDevices(std::move(devices));
}

Binding InputModule::bind(const std::string& action, InputSource s, Trigger t) {
    Binding b;
    b.action = action;
    b.source = s;
    b.trigger = t;
    return b;
}

void InputModule::defineUiActions(InputMap& m) {
    // Controller-only by default: the keyboard already reaches RmlUi
    // directly (arrows, Enter, Esc, Tab), so binding keys here too would
    // move the focus twice. Add keys here for a custom layout (IJKL).
    const struct { const char* id; const char* label; } ui[] = {
        { "ui.up", "Up" }, { "ui.down", "Down" }, { "ui.left", "Left" }, { "ui.right", "Right" },
        { "ui.accept", "Accept" }, { "ui.back", "Back" }, { "ui.prev", "Previous tab" }, { "ui.next", "Next tab" },
    };
    for (auto& a : ui) m.defineAction({ a.id, a.label, "Menus", "ui" });
    m.addBinding(bind("ui.up", pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
    m.addBinding(bind("ui.down", pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)));
    m.addBinding(bind("ui.left", pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT)));
    m.addBinding(bind("ui.right", pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
    m.addBinding(bind("ui.up", padAxis(SDL_GAMEPAD_AXIS_LEFTY, -1)));
    m.addBinding(bind("ui.down", padAxis(SDL_GAMEPAD_AXIS_LEFTY, 1)));
    m.addBinding(bind("ui.left", padAxis(SDL_GAMEPAD_AXIS_LEFTX, -1)));
    m.addBinding(bind("ui.right", padAxis(SDL_GAMEPAD_AXIS_LEFTX, 1)));
    m.addBinding(bind("ui.accept", pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    m.addBinding(bind("ui.back", pad(SDL_GAMEPAD_BUTTON_EAST)));
    m.addBinding(bind("ui.prev", pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)));
    m.addBinding(bind("ui.next", pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
}

void InputModule::defineCharacterActions(InputMap& m) {
    auto def = [&](const char* id, const char* label, const char* cat, ActionType t = ActionType::Button, bool clamp = true) {
        m.defineAction({ id, label, cat, "game", t, clamp });
    };
    def("move", "Move", "Movement", ActionType::Axis2D);
    def("look", "Look (mouse)", "Movement", ActionType::Axis2D, false);
    def("look.rate", "Look (stick / gyro)", "Movement", ActionType::Axis2D, false);
    def("jump", "Jump", "Movement");
    def("sprint", "Sprint", "Movement");
    def("walk", "Walk", "Movement");
    def("crouch", "Crouch", "Movement");
    def("fire", "Fire", "Combat");
    def("aim", "Aim", "Combat");
    def("interact", "Interact / push", "Combat");
    def("camera.toggle", "First / third person", "Camera");
    def("camera.zoom", "Camera distance", "Camera", ActionType::Axis1D, false);
    def("audio.ping", "Ping surroundings (hear the walls)", "Accessibility");
    def("voice.talk", "Push to talk (voice chat)", "Voice");

    auto dirKey = [&](SDL_Scancode sc, int component, float scale) {
        Binding b = bind("move", key(sc), Trigger::Continuous);
        b.component = component;
        b.scale = scale;
        m.addBinding(b);
    };
    dirKey(SDL_SCANCODE_W, 1, 1.0f);
    dirKey(SDL_SCANCODE_S, 1, -1.0f);
    dirKey(SDL_SCANCODE_D, 0, 1.0f);
    dirKey(SDL_SCANCODE_A, 0, -1.0f);
    Binding stick = bind("move", padAxis(SDL_GAMEPAD_AXIS_LEFTX), Trigger::Continuous);
    stick.sourceY = padAxis(SDL_GAMEPAD_AXIS_LEFTY);
    stick.deadzone = 0.15f;
    stick.invert = true; // SDL's stick Y is down-positive; +Y is forward
    m.addBinding(stick);
    Binding mx = bind("look", { SourceKind::MouseMotion, 0, 0, 0 }, Trigger::Continuous);
    Binding my = bind("look", { SourceKind::MouseMotion, 0, 1, 0 }, Trigger::Continuous);
    my.component = 1;
    my.invert = true; // mouse down = look down
    m.addBinding(mx);
    m.addBinding(my);
    Binding rs = bind("look.rate", padAxis(SDL_GAMEPAD_AXIS_RIGHTX), Trigger::Continuous);
    rs.sourceY = padAxis(SDL_GAMEPAD_AXIS_RIGHTY);
    rs.deadzone = 0.12f;
    rs.curve = 1.6f; // fine aim near the centre
    rs.invert = true;
    m.addBinding(rs);
    m.addBinding(bind("jump", key(SDL_SCANCODE_SPACE)));
    m.addBinding(bind("jump", pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    m.addBinding(bind("sprint", key(SDL_SCANCODE_LSHIFT), Trigger::Continuous));
    m.addBinding(bind("sprint", pad(SDL_GAMEPAD_BUTTON_LEFT_STICK), Trigger::Toggle));
    m.addBinding(bind("walk", key(SDL_SCANCODE_LALT), Trigger::Continuous));
    m.addBinding(bind("crouch", key(SDL_SCANCODE_C), Trigger::Toggle));
    m.addBinding(bind("crouch", pad(SDL_GAMEPAD_BUTTON_EAST), Trigger::Toggle));
    m.addBinding(bind("fire", mouse(SDL_BUTTON_LEFT), Trigger::Continuous));
    Binding rt = bind("fire", padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), Trigger::Continuous);
    rt.threshold = 0.4f;
    m.addBinding(rt);
    m.addBinding(bind("aim", mouse(SDL_BUTTON_RIGHT), Trigger::Continuous));
    m.addBinding(bind("aim", padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), Trigger::Continuous));
    m.addBinding(bind("interact", key(SDL_SCANCODE_E)));
    m.addBinding(bind("interact", pad(SDL_GAMEPAD_BUTTON_NORTH)));
    m.addBinding(bind("camera.toggle", key(SDL_SCANCODE_V)));
    m.addBinding(bind("camera.toggle", pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK)));
    m.addBinding(bind("camera.zoom", { SourceKind::MouseWheel, 0, 0, 0 }, Trigger::Continuous));
    m.addBinding(bind("audio.ping", key(SDL_SCANCODE_Q)));
    m.addBinding(bind("voice.talk", key(SDL_SCANCODE_B))); // held while down
}

SDL_Scancode InputModule::mirrorScancode(SDL_Scancode sc) {
    // Pairs mirrored across the G/H line (and the modifiers across the space bar).
    static const SDL_Scancode pairs[][2] = {
        { SDL_SCANCODE_1, SDL_SCANCODE_0 }, { SDL_SCANCODE_2, SDL_SCANCODE_9 }, { SDL_SCANCODE_3, SDL_SCANCODE_8 },
        { SDL_SCANCODE_4, SDL_SCANCODE_7 }, { SDL_SCANCODE_5, SDL_SCANCODE_6 },
        { SDL_SCANCODE_Q, SDL_SCANCODE_P }, { SDL_SCANCODE_W, SDL_SCANCODE_O }, { SDL_SCANCODE_E, SDL_SCANCODE_I },
        { SDL_SCANCODE_R, SDL_SCANCODE_U }, { SDL_SCANCODE_T, SDL_SCANCODE_Y },
        { SDL_SCANCODE_A, SDL_SCANCODE_SEMICOLON }, { SDL_SCANCODE_S, SDL_SCANCODE_L }, { SDL_SCANCODE_D, SDL_SCANCODE_K },
        { SDL_SCANCODE_F, SDL_SCANCODE_J }, { SDL_SCANCODE_G, SDL_SCANCODE_H },
        { SDL_SCANCODE_Z, SDL_SCANCODE_SLASH }, { SDL_SCANCODE_X, SDL_SCANCODE_PERIOD }, { SDL_SCANCODE_C, SDL_SCANCODE_COMMA },
        { SDL_SCANCODE_V, SDL_SCANCODE_M }, { SDL_SCANCODE_B, SDL_SCANCODE_N },
        { SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT }, { SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL }, { SDL_SCANCODE_LALT, SDL_SCANCODE_RALT },
        { SDL_SCANCODE_TAB, SDL_SCANCODE_BACKSLASH }, { SDL_SCANCODE_CAPSLOCK, SDL_SCANCODE_RETURN },
    };
    for (const auto& p : pairs) {
        if (sc == p[0]) return p[1];
        if (sc == p[1]) return p[0];
    }
    return sc;
}

void InputModule::mirrorKeyboard(InputMap& m) {
    auto mirror = [](InputSource& s) {
        if (s.kind == SourceKind::Key) s.code = mirrorScancode(static_cast<SDL_Scancode>(s.code));
    };
    for (size_t i = 0; i < m.bindings().size(); ++i) {
        Binding& b = m.binding(i);
        const bool key = b.source.kind == SourceKind::Key;
        mirror(b.source);
        mirror(b.sourceY);
        for (InputSource& mod : b.modifiers) mirror(mod);
        // A key that meant "left" now sits on the right of its cluster
        // (A -> ;), so its axis direction flips to keep meaning left.
        const ActionDef* def = m.action(b.action);
        if (key && def && def->type == ActionType::Axis2D && b.component == 0) b.scale = -b.scale;
    }
    m.resetStates();
}

void InputModule::init(Application& app) {
    m_app = &app;
    m_claimId = app.addEventClaim([this](const SDL_Event& e) { return claimTouch(e); });
    m_devices.init();
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    for (PromptStyle& s : m_promptStyles) s = PromptStyle::Touch;
#endif
    if (dev::kEnabled) {
        if (const char* forced = std::getenv("KKE_PROMPT_STYLE"); forced && *forced) {
            PromptStyle s{};
            if (promptStyleFromString(forced, s)) forcePromptStyle(s);
            else log::get(name())->warn("KKE_PROMPT_STYLE='{}' is not a prompt style (keyboard, xbox, playstation, switch, steamdeck, steamcontroller, touch)", forced);
        }
    }
    m_sanity.onFinding = [this](const InputSanity::Finding& f) {
        log::get(name())->info("input sanity: {} on control {:#x}: {}", toString(f.kind), f.control, f.detail);
    };
    // Device names/swaps load now (bindings load in commitDefaults(), after
    // the game has defined its actions).
    std::string error;
    bool exists = false;
    nlohmann::json j;
    const bool parsed = datafile::loadPath(m_path, j, &error, &exists); // input.json or input.yml
    if (exists && (!parsed || !j.is_object())) log::get(name())->warn("{}", error.empty() ? m_path + ": not an object" : error);
    else if (exists && j.contains("devices")) {
        try {
            m_devices.loadDevices(j["devices"]);
        } catch (const std::exception& e) {
            log::get(name())->warn("{}: {}", m_path, e.what());
        }
    }
    commitDefaults(); // UI actions; games call it again after adding theirs
    // Virtual devices are a developer tool (headless tests, recordings):
    // a shipping build never creates them (kke/DevTools.h), so they
    // can't be used to inject input.
    if (const char* v = dev::env("KKE_VIRTUAL_INPUT"); v && *v) attachVirtualDevices(v);
    // KKE_VIRTUAL_INPUT_LATE=<s>:<spec>: plugged in <s> seconds in (hot-plug tests).
    if (const char* v = dev::env("KKE_VIRTUAL_INPUT_LATE"); v && *v) {
        const std::string late = v;
        const size_t colon = late.find(':');
        if (colon != std::string::npos) {
            m_lateAt = std::atof(late.substr(0, colon).c_str());
            m_lateSpec = late.substr(colon + 1);
        }
    }
    m_animateVirtual = dev::flag("KKE_VIRTUAL_INPUT_ANIMATE");
    if (const char* v = dev::env("KKE_VIRTUAL_PAD_SCRIPT"); v && *v) {
        std::string spec = v;
        size_t start = 0;
        while (start <= spec.size()) {
            const size_t comma = std::min(spec.find(',', start), spec.size());
            const std::string item = spec.substr(start, comma - start);
            start = comma + 1;
            const size_t colon = item.find(':');
            if (colon == std::string::npos) continue;
            PadStep step;
            step.at = std::atof(item.substr(0, colon).c_str());
            std::string control = item.substr(colon + 1);
            const size_t eq = control.find('=');
            if (eq != std::string::npos) {
                step.value = static_cast<float>(std::atof(control.substr(eq + 1).c_str()));
                control = control.substr(0, eq);
                // SDL's names (leftx, lefty, rightx, righty, lefttrigger,
                // righttrigger), or the prompt names (lt, rt, ...).
                step.axis = SDL_GetGamepadAxisFromString(control.c_str());
                if (step.axis == SDL_GAMEPAD_AXIS_INVALID) step.axis = ButtonPrompts::padAxisFromName(control);
            } else {
                // "a*1.5": held for 1.5 s instead of tapped.
                if (const size_t star = control.find('*'); star != std::string::npos) {
                    step.hold = std::max(0.05f, static_cast<float>(std::atof(control.substr(star + 1).c_str())));
                    control = control.substr(0, star);
                }
                step.button = ButtonPrompts::padButtonFromName(control);
            }
            if (step.button < 0 && step.axis < 0) {
                log::get(name())->warn("KKE_VIRTUAL_PAD_SCRIPT: no pad control called '{}'", control);
                continue;
            }
            m_padScript.push_back(step);
        }
        std::stable_sort(m_padScript.begin(), m_padScript.end(), [](const PadStep& a, const PadStep& b) { return a.at < b.at; });
    }
    if (const char* v = dev::env("KKE_VIRTUAL_TOUCH_SCRIPT"); v && *v) {
        // "3:down 1 0.15 0.8,3.5:move 1 0.15 0.6,5:up 1,6:tap 0.9 0.8":
        // fingers as SDL events, positions 0..1 of the window.
        std::string spec = v;
        size_t start = 0;
        while (start <= spec.size()) {
            const size_t comma = std::min(spec.find(',', start), spec.size());
            const std::string item = spec.substr(start, comma - start);
            start = comma + 1;
            TouchStep step;
            char kind[8] = {};
            float a = 0.0f, b = 0.0f, c = 0.0f;
            const int n = std::sscanf(item.c_str(), "%lf:%7s %f %f %f", &step.at, kind, &a, &b, &c);
            const std::string k = n >= 2 ? kind : "";
            if (k == "tap" && n >= 4) {
                step.type = SDL_EVENT_FINGER_DOWN;
                step.finger = 99;
                step.pos = { a, b };
                m_touchScript.push_back(step);
                step.at += 0.1;
                step.type = SDL_EVENT_FINGER_UP;
            } else if ((k == "down" || k == "move") && n >= 5) {
                step.type = k == "down" ? SDL_EVENT_FINGER_DOWN : SDL_EVENT_FINGER_MOTION;
                step.finger = static_cast<uint64_t>(a);
                step.pos = { b, c };
            } else if (k == "up" && n >= 3) {
                step.type = SDL_EVENT_FINGER_UP;
                step.finger = static_cast<uint64_t>(a);
            } else {
                log::get(name())->warn("KKE_VIRTUAL_TOUCH_SCRIPT: can't read '{}' (want <s>:down|move <finger> <x> <y>, <s>:up <finger>, <s>:tap <x> <y>)", item);
                continue;
            }
            m_touchScript.push_back(step);
        }
        std::stable_sort(m_touchScript.begin(), m_touchScript.end(), [](const TouchStep& x, const TouchStep& y) { return x.at < y.at; });
    }
}

void InputModule::playTouchScript(double now) {
    while (!m_touchScript.empty() && now >= m_touchScript.front().at) {
        TouchStep s = m_touchScript.front();
        m_touchScript.erase(m_touchScript.begin());
        if (s.type == SDL_EVENT_FINGER_UP) {
            if (auto it = m_scriptFingers.find(s.finger); it != m_scriptFingers.end()) s.pos = it->second;
            m_scriptFingers.erase(s.finger);
        } else {
            m_scriptFingers[s.finger] = s.pos;
        }
        SDL_Event e{};
        e.type = s.type;
        e.tfinger.timestamp = SDL_GetTicksNS();
        e.tfinger.touchID = 1;
        e.tfinger.fingerID = s.finger;
        e.tfinger.x = s.pos.x;
        e.tfinger.y = s.pos.y;
        e.tfinger.pressure = s.type == SDL_EVENT_FINGER_UP ? 0.0f : 1.0f;
        if (m_app) e.tfinger.windowID = SDL_GetWindowID(m_app->window().handle());
        SDL_PushEvent(&e);
    }
}

void InputModule::playPadScript(double now) {
    const Virtual* pad = nullptr;
    for (const Virtual& v : m_virtual)
        if (v.pad && v.joy) { pad = &v; break; }
    if (!pad) return;
    for (auto it = m_padReleases.begin(); it != m_padReleases.end();) {
        if (now >= it->first) {
            SDL_SetJoystickVirtualButton(pad->joy, it->second, false);
            it = m_padReleases.erase(it);
        } else {
            ++it;
        }
    }
    size_t done = 0;
    for (; done < m_padScript.size() && m_padScript[done].at <= now; ++done) {
        const PadStep& s = m_padScript[done];
        if (s.button >= 0) {
            SDL_SetJoystickVirtualButton(pad->joy, s.button, true);
            m_padReleases.emplace_back(now + double(s.hold), s.button);
        } else {
            // A virtual trigger's raw axis spans the whole range (SDL maps
            // -32768..32767 onto released..pressed), so 0 is half pulled:
            // map the script's 0..1 onto it.
            const bool trigger = s.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || s.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            const float raw = trigger ? std::clamp(s.value, 0.0f, 1.0f) * 65535.0f - 32768.0f : std::clamp(s.value, -1.0f, 1.0f) * 32767.0f;
            SDL_SetJoystickVirtualAxis(pad->joy, s.axis, static_cast<Sint16>(raw));
        }
    }
    m_padScript.erase(m_padScript.begin(), m_padScript.begin() + static_cast<std::ptrdiff_t>(done));
}

void InputModule::attachVirtualDevices(const std::string& spec) {
    auto attach = [&](bool pad, int index) {
        SDL_VirtualJoystickDesc d;
        SDL_INIT_INTERFACE(&d);
        SDL_VirtualJoystickSensorDesc sensors[2] = { { SDL_SENSOR_GYRO, 100.0f }, { SDL_SENSOR_ACCEL, 100.0f } };
        if (pad) {
            d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
            d.name = "KKE virtual gamepad";
            d.vendor_id = 0x28de; // Valve-style ids so it reads like a Steam Controller
            d.product_id = 0x1302;
            d.naxes = SDL_GAMEPAD_AXIS_COUNT;
            d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
            d.button_mask = 0;
            for (int b = 0; b <= SDL_GAMEPAD_BUTTON_LEFT_PADDLE2; ++b) d.button_mask |= 1u << b;
            d.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1u;
            d.nsensors = 2;
            d.sensors = sensors;
        } else {
            d.type = SDL_JOYSTICK_TYPE_FLIGHT_STICK;
            d.name = "KKE virtual flight stick"; // two of these = a HOSAS pair
            d.vendor_id = 0x231d;
            d.product_id = 0x7777;
            d.naxes = 5;
            d.nbuttons = 24;
            d.nhats = 1;
        }
        SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
        if (!id) {
            log::get(name())->warn("virtual device failed: {}", SDL_GetError());
            return;
        }
        Virtual v{ id, SDL_OpenJoystick(id), pad, index };
        m_virtual.push_back(v);
        log::get(name())->info("attached {} (virtual)", d.name);
    };
    if (spec.find("hosas") != std::string::npos) {
        attach(false, 0);
        attach(false, 1);
    }
    // One gamepad per "pad" (pad,pad,pad: three, for local multiplayer).
    int pads = 0;
    for (size_t at = spec.find("pad"); at != std::string::npos; at = spec.find("pad", at + 3)) attach(true, pads++);
}

void InputModule::animateVirtualDevices(float t) {
    auto s16 = [](float v) { return static_cast<Sint16>(std::clamp(v, -1.0f, 1.0f) * 32767.0f); };
    for (const Virtual& v : m_virtual) {
        if (!v.joy) continue;
        const float ph = t * (v.index ? 0.6f : 1.0f) + v.index * 1.7f;
        if (v.pad) {
            // Right stick (the left one navigates menus).
            SDL_SetJoystickVirtualAxis(v.joy, SDL_GAMEPAD_AXIS_RIGHTX, s16(std::sin(ph)));
            SDL_SetJoystickVirtualAxis(v.joy, SDL_GAMEPAD_AXIS_RIGHTY, s16(std::cos(ph)));
            SDL_SetJoystickVirtualAxis(v.joy, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, s16(0.5f + 0.5f * std::sin(ph * 2.0f)));
            // Only buttons that don't drive menus (not D-pad/A/B/LB/RB/Back),
            // so an animated pad can't wander off the screen being tested.
            static const int safe[] = { SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                                        SDL_GAMEPAD_BUTTON_MISC1, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
                                        SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 };
            const int b = safe[static_cast<int>(t * 2.0f) % std::size(safe)];
            for (int k = 0; k < SDL_GAMEPAD_BUTTON_COUNT; ++k) SDL_SetJoystickVirtualButton(v.joy, k, k == b);
            const float gyro[3] = { 0.3f * std::sin(ph), 1.5f * std::cos(ph * 0.5f), 0.0f }; // rad/s
            const float accel[3] = { 0.0f, 9.81f, 0.0f };
            const Uint64 ns = SDL_GetTicksNS();
            SDL_SendJoystickVirtualSensorData(v.joy, SDL_SENSOR_GYRO, ns, gyro, 3);
            SDL_SendJoystickVirtualSensorData(v.joy, SDL_SENSOR_ACCEL, ns, accel, 3);
        } else {
            SDL_SetJoystickVirtualAxis(v.joy, 0, s16(std::sin(ph)));
            SDL_SetJoystickVirtualAxis(v.joy, 1, s16(std::sin(ph * 1.3f)));
            SDL_SetJoystickVirtualAxis(v.joy, 2, s16(std::sin(ph * 0.4f)));
            SDL_SetJoystickVirtualAxis(v.joy, 3, s16(-1.0f + std::fmod(t * 0.2f, 2.0f))); // throttle sweep
            const int b = static_cast<int>(t * 3.0f + v.index * 5) % 24;
            for (int k = 0; k < 24; ++k) SDL_SetJoystickVirtualButton(v.joy, k, k == b);
            static const Uint8 hats[] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
            SDL_SetJoystickVirtualHat(v.joy, 0, hats[static_cast<int>(t) % 4]);
        }
    }
}

void InputModule::commitDefaults() {
    for (auto& m : m_maps) m->storeDefaults();
    load();
    m_touchActions = 0; // guess the touch layout again from the actions there are now
}

// ---------------------------------------------------------------- touch controls

void InputModule::setTouchLayout(const TouchLayoutOptions& options) {
    m_touchOptions = options;
    m_touchActions = 0;
}

bool InputModule::touchEnabled() const {
    if (m_touch.editing()) return true;
    if (m_touch.layout().empty() || m_maps.empty() || !m_maps[0]->contextEnabled("game")) return false;
    return std::none_of(m_touchHiders.begin(), m_touchHiders.end(), [](const std::function<bool()>& h) { return h && h(); });
}

bool InputModule::touchShown() const { return m_touch.editing() || (touchEnabled() && promptStyle(0) == PromptStyle::Touch); }

void InputModule::editTouch(bool editing) {
    if (editing == m_touch.editing()) return;
    m_touch.setEditing(editing);
    if (!editing) {
        m_touchSaved = m_touch.save().dump();
        if (!save()) log::get(name())->warn("could not save {}", m_path);
    }
}

void InputModule::updateTouch(double now) {
    const float dt = m_touchApplied < 0.0 ? 0.0f : static_cast<float>(now - m_touchApplied);
    m_touchApplied = now;
    if (m_app) {
        const VkExtent2D e = m_app->renderer().extent();
        m_touch.setScreen({ static_cast<float>(e.width), static_cast<float>(e.height) }, m_app->uiSafeRect());
    }
    // Guess the layout from the actions (again when a script adds some),
    // then put the player's own back over it.
    if (!m_maps.empty() && m_maps[0]->actions().size() != m_touchActions) {
        m_touchActions = m_maps[0]->actions().size();
        m_touch.setDefaults(TouchControls::autoLayout(*m_maps[0], m_touchOptions));
        std::string shown;
        for (const TouchControl& c : m_touch.defaults())
            shown += std::string(shown.empty() ? "" : ", ") + TouchControls::kindName(c.kind) +
                     (c.kind == TouchControl::Kind::Stick ? (c.stick ? " right" : " left") : c.action.empty() ? "" : " " + c.action);
        log::get(name())->info("touch controls: {}", shown);
        if (!m_touchSaved.empty()) {
            const nlohmann::json j = nlohmann::json::parse(m_touchSaved, nullptr, false);
            if (!j.is_discarded()) m_touch.load(j, *m_maps[0]);
        }
    }
    const bool enabled = touchEnabled();
    if (!enabled && m_touchWasEnabled) {
        m_touch.releaseAll(); // a menu took the screen: nothing stays held
        m_touchMouse = false;
    }
    m_touchWasEnabled = enabled;
    if (!m_maps.empty()) m_touch.apply(*m_maps[0], dt);
}

bool InputModule::claimTouch(const SDL_Event& e) {
    if (!touchEnabled()) return false;
    const glm::vec2 frame = [&] {
        if (!m_app) return glm::vec2(0.0f);
        const VkExtent2D x = m_app->renderer().extent();
        return glm::vec2(static_cast<float>(x.width), static_cast<float>(x.height));
    }();
    // Window points (mouse events) to 0..1.
    auto normalized = [](SDL_WindowID id, float x, float y, glm::vec2& out) {
        int w = 0, h = 0;
        SDL_Window* window = SDL_GetWindowFromID(id);
        if (!window || !SDL_GetWindowSize(window, &w, &h) || w <= 0 || h <= 0) return false;
        out = { x / static_cast<float>(w), y / static_cast<float>(h) };
        return true;
    };
    // The controls are drawn over the game's HUD, so a control under the
    // finger wins over a HUD prompt; while editing, the edit bar wins.
    auto onUi = [&](glm::vec2 n) {
        return m_uiHit && m_uiHit(n.x, n.y) && (m_touch.editing() || m_touch.controlAt(n * frame) < 0);
    };
    constexpr uint64_t kMouse = ~uint64_t{ 0 }; // the real mouse, while editing
    switch (e.type) {
    case SDL_EVENT_FINGER_DOWN: {
        const glm::vec2 n(e.tfinger.x, e.tfinger.y);
        if (onUi(n) || !m_touch.fingerDown(e.tfinger.fingerID, n * frame)) return false;
        notePromptDevice(0, false, PromptStyle::Touch);
        return true;
    }
    case SDL_EVENT_FINGER_MOTION: return m_touch.fingerMove(e.tfinger.fingerID, glm::vec2(e.tfinger.x, e.tfinger.y) * frame);
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: return m_touch.fingerUp(e.tfinger.fingerID);
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        glm::vec2 n;
        if (!normalized(e.button.windowID, e.button.x, e.button.y, n) || onUi(n)) return false;
        if (e.button.which == SDL_TOUCH_MOUSEID) {
            // SDL's mouse made from the first finger: the finger's own
            // events above decide; the copy mustn't click in the game.
            m_touchMouse = m_touch.wouldTake(n * frame);
            return m_touchMouse;
        }
        return m_touch.editing() && m_touch.fingerDown(kMouse, n * frame);
    }
    case SDL_EVENT_MOUSE_MOTION: {
        if (e.motion.which == SDL_TOUCH_MOUSEID) return m_touchMouse;
        if (!m_touch.owns(kMouse)) return false;
        glm::vec2 n;
        return normalized(e.motion.windowID, e.motion.x, e.motion.y, n) && m_touch.fingerMove(kMouse, n * frame);
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button != SDL_BUTTON_LEFT) return false;
        if (e.button.which == SDL_TOUCH_MOUSEID) {
            const bool mine = m_touchMouse;
            m_touchMouse = false;
            return mine;
        }
        return m_touch.fingerUp(kMouse);
    default: return false;
    }
}

bool InputModule::load() {
    std::string error;
    bool exists = false;
    nlohmann::json j;
    const bool parsed = datafile::loadPath(m_path, j, &error, &exists);
    if (!exists) return false;
    if (!parsed || !j.is_object()) {
        log::get(name())->warn("could not read {} (using defaults)", error.empty() ? m_path + ": not an object" : error);
        return false;
    }
    try {
        const nlohmann::json& list = j.value("players", nlohmann::json::array());
        for (size_t p = 0; p < m_maps.size() && p < list.size(); ++p) {
            const int dropped = m_maps[p]->load(list[p]);
            if (dropped < 0) m_maps[p]->restoreDefaults();
            // Actions the file doesn't mention yet (added by an update) keep their defaults.
            for (const ActionDef& a : m_maps[p]->actions())
                if (m_maps[p]->bindingsFor(a.id).empty()) m_maps[p]->restoreDefaults(a.id);
        }
        if (const auto t = j.find("touch"); t != j.end() && t->is_object()) {
            m_touchSaved = t->dump();
            m_touchActions = 0; // put back over the guessed layout next frame
        }
        return true;
    } catch (const std::exception& e) {
        log::get(name())->warn("could not read {}: {} (using defaults)", m_path, e.what());
        return false;
    }
}

bool InputModule::save() const {
    nlohmann::json j{ { "version", 1 }, { "devices", m_devices.saveDevices() }, { "players", nlohmann::json::array() } };
    for (const auto& m : m_maps) j["players"].push_back(m->save());
    j["touch"] = m_touch.save();
    const std::string target = datafile::saveTarget(m_path).string(); // input.yml stays YAML
    const std::string tmp = target + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << datafile::dump(j, datafile::formatOf(target).value_or(datafile::Format::Json));
        if (!f) return false;
    }
    std::remove(target.c_str());
    return std::rename(tmp.c_str(), target.c_str()) == 0; // no half-written file on a crash
}

void InputModule::frameStart(const UpdateContext& ctx) {
    m_now = ctx.totalTime;
    if (!m_lateSpec.empty() && m_now >= m_lateAt) {
        attachVirtualDevices(m_lateSpec);
        m_lateSpec.clear();
    }
    if (m_animateVirtual) animateVirtualDevices(ctx.totalTime);
    if (!m_padScript.empty() || !m_padReleases.empty()) playPadScript(m_now);
    if (!m_touchScript.empty()) playTouchScript(m_now);
    m_devices.poll();
    if (!m_promptTouched) {
        // Until someone presses something, a Steam Deck's own controls are
        // the best guess on a Deck.
        for (const InputDevices::Device& d : m_devices.devices())
            if (d.connected && d.kind == InputDevices::Kind::Gamepad && promptStyleFor(d) == PromptStyle::SteamDeck)
                for (int p = 0; p < players(); ++p) setPromptStyle(p, PromptStyle::SteamDeck);
    }
    updateTouch(m_now);
    // The touch sticks are "any controller's" sticks for player 1.
    class WithTouch : public InputState {
    public:
        WithTouch(const InputState& base, const TouchControls& touch) : m_base(base), m_touch(touch) {}
        float value(const InputSource& s, const std::vector<uint32_t>* allowed) const override {
            const float v = m_base.value(s, allowed);
            const float full = m_touch.sourceValue(s);
            if (full == 0.0f) return v;
            const float t = s.half > 0 ? std::max(0.0f, full) : s.half < 0 ? std::max(0.0f, -full) : full;
            return std::abs(t) > std::abs(v) ? t : v;
        }

    private:
        const InputState& m_base;
        const TouchControls& m_touch;
    };
    const WithTouch withTouch(m_devices, m_touch);
    for (size_t i = 0; i < m_maps.size(); ++i) m_maps[i]->update(i == 0 && touchEnabled() ? static_cast<const InputState&>(withTouch) : m_devices, m_now);
    m_sanity.update(static_cast<double>(SDL_GetTicksNS()) * 1e-9); // same clock as event timestamps
}

void InputModule::frameEnd() { m_devices.endFrame(); }

uint32_t InputModule::sanityControl(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: return (1u << 28) | (static_cast<uint32_t>(e.key.scancode) & 0xFFFFu);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: return (2u << 28) | e.button.button;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: return (3u << 28) | ((static_cast<uint32_t>(e.gbutton.which) & 0xFFFu) << 8) | e.gbutton.button;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: return (4u << 28) | ((static_cast<uint32_t>(e.gaxis.which) & 0xFFFu) << 8) | e.gaxis.axis;
    default: return 0;
    }
}

bool InputModule::screenButtonEvent(const SDL_Event& event) {
    auto press = [&](const HeldScreenButton& b, bool down) {
        if (b.player >= 0 && b.player < players()) m_maps[static_cast<size_t>(b.player)]->setScreenButton(b.action, down);
    };
    auto find = [&](float nx, float ny, HeldScreenButton& out) { return m_screenButtonAt && m_screenButtonAt(nx, ny, out.player, out.action); };
    auto windowPoint = [](SDL_WindowID id, float x, float y, float& nx, float& ny) {
        int w = 0, h = 0;
        SDL_Window* window = SDL_GetWindowFromID(id);
        if (!window || !SDL_GetWindowSize(window, &w, &h) || w <= 0 || h <= 0) return false;
        nx = x / static_cast<float>(w);
        ny = y / static_cast<float>(h);
        return true;
    };
    switch (event.type) {
    case SDL_EVENT_FINGER_DOWN: {
        HeldScreenButton b;
        if (!find(event.tfinger.x, event.tfinger.y, b)) return false;
        press(b, true);
        m_fingerButtons[event.tfinger.fingerID] = std::move(b);
        return true;
    }
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        auto it = m_fingerButtons.find(event.tfinger.fingerID);
        if (it == m_fingerButtons.end()) return false;
        press(it->second, false);
        m_fingerButtons.erase(it);
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        if (event.button.button != SDL_BUTTON_LEFT) return false;
        float nx = 0.0f, ny = 0.0f;
        HeldScreenButton b;
        if (!windowPoint(event.button.windowID, event.button.x, event.button.y, nx, ny) || !find(nx, ny, b)) return false;
        if (event.button.which == SDL_TOUCH_MOUSEID) {
            m_touchMouseOnButton = true; // the finger's own event pressed it
        } else {
            press(b, true);
            m_mouseButton = std::move(b);
        }
        return true;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button != SDL_BUTTON_LEFT) return false;
        if (event.button.which == SDL_TOUCH_MOUSEID && m_touchMouseOnButton) {
            m_touchMouseOnButton = false;
            return true;
        }
        if (event.button.which != SDL_TOUCH_MOUSEID && m_mouseButton) {
            press(*m_mouseButton, false);
            m_mouseButton.reset();
            return true;
        }
        return false;
    default: return false;
    }
}

void InputModule::onEvent(const SDL_Event& event) {
    // A press on an on-screen button is the button's, not a click in the game.
    if (!screenButtonEvent(event)) m_devices.handleEvent(event);
    // OS timestamps (ns), not frame times: frames would make every human
    // look machine-regular (kke/InputSanity.h).
    const double t = static_cast<double>(event.common.timestamp) * 1e-9;
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (!event.key.repeat) m_sanity.button(sanityControl(event), event.type == SDL_EVENT_KEY_DOWN, t);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        m_sanity.button(sanityControl(event), event.type == SDL_EVENT_MOUSE_BUTTON_DOWN, t);
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        m_sanity.button(sanityControl(event), event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN, t);
        break;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        m_sanity.axis(sanityControl(event), static_cast<float>(event.gaxis.value) / 32767.0f, t);
        break;
    default: break;
    }

    // Button prompts follow the device that was just used.
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        if (!event.key.repeat) notePromptDevice(0, true, PromptStyle::Keyboard);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.which != SDL_TOUCH_MOUSEID) notePromptDevice(0, true, PromptStyle::Keyboard);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.which != SDL_TOUCH_MOUSEID) notePromptDevice(0, true, PromptStyle::Keyboard);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        // A real push of the mouse, not a jitter or a finger's emulated one.
        if (event.motion.which != SDL_TOUCH_MOUSEID && std::abs(event.motion.xrel) + std::abs(event.motion.yrel) > 6.0f)
            notePromptDevice(0, true, PromptStyle::Keyboard);
        break;
    case SDL_EVENT_FINGER_DOWN: notePromptDevice(0, false, PromptStyle::Touch); break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
        const SDL_JoystickID which = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? event.gbutton.which : event.gaxis.which;
        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(static_cast<int>(event.gaxis.value)) < 16384) break;
        for (const InputDevices::Device& d : m_devices.devices())
            if (d.connected && d.kind == InputDevices::Kind::Gamepad && d.sdlId == which) notePromptDevice(d.ref, false, promptStyleFor(d));
        break;
    }
    default: break;
    }
}

PromptStyle InputModule::promptStyleFor(const InputDevices::Device& d) {
    const SDL_GamepadType type = d.gamepad ? SDL_GetGamepadType(d.gamepad) : SDL_GetGamepadTypeForID(d.sdlId);
    return promptStyleForPad(static_cast<int>(type), d.vendor, d.product);
}

void InputModule::notePromptDevice(uint32_t deviceRef, bool keyboardOrMouse, PromptStyle style) {
    m_promptTouched = true;
    for (int p = 0; p < players(); ++p) {
        const std::vector<uint32_t>& mine = m_maps[static_cast<size_t>(p)]->devices();
        bool listens = mine.empty() || (deviceRef != 0 && std::find(mine.begin(), mine.end(), deviceRef) != mine.end());
        if (!listens && (keyboardOrMouse || style == PromptStyle::Touch)) {
            // Split screen: the keyboard/mouse and the screen belong to
            // whoever was given a keyboard or mouse.
            for (uint32_t ref : mine)
                if (const InputDevices::Device* d = m_devices.find(ref); d && (d->kind == InputDevices::Kind::Keyboard || d->kind == InputDevices::Kind::Mouse))
                    listens = true;
        }
        if (listens) setPromptStyle(p, style);
    }
}

void InputModule::setPromptStyle(int player, PromptStyle style) {
    PromptStyle& s = m_promptStyles[static_cast<size_t>(player)];
    if (s == style) return;
    s = style;
    ++m_promptSerial;
}

PromptStyle InputModule::promptStyle(int player) const {
    if (m_forcedStyle) return *m_forcedStyle;
    return m_promptStyles[static_cast<size_t>(std::clamp(player, 0, players() - 1))];
}

void InputModule::forcePromptStyle(std::optional<PromptStyle> style) {
    if (style == m_forcedStyle) return;
    m_forcedStyle = style;
    ++m_promptSerial;
}

std::string InputModule::promptRml(const std::string& action, const std::string& label, int player) const {
    const int p = std::clamp(player, 0, players() - 1);
    const InputMap& m = *m_maps[static_cast<size_t>(p)];
    const PromptStyle style = promptStyle(p);
    if (style == PromptStyle::Touch && m_prompts.isTouchButton(m, action)) {
        const ActionDef* def = m.action(action);
        return ButtonPrompts::touchButton(action, !label.empty() ? label : !def->label.empty() ? def->label : action, p,
                                         ButtonPrompts::touchColour(m, action));
    }
    std::vector<ButtonPrompts::Glyph> g = m.action(action) ? m_prompts.actionGlyphs(style, m, action) : m_prompts.namedGlyphs(style, action);
    return m_prompts.rml(g, label);
}

std::string InputModule::promptText(const std::string& text, int player) const {
    const int p = std::clamp(player, 0, players() - 1);
    return m_prompts.format(promptStyle(p), *m_maps[static_cast<size_t>(p)], text, p);
}

void InputModule::shutdown() {
    if (m_app && m_claimId) m_app->removeEventClaim(m_claimId);
    m_claimId = 0;
    for (Virtual& v : m_virtual) {
        if (v.joy) SDL_CloseJoystick(v.joy);
        SDL_DetachVirtualJoystick(v.id);
    }
    m_virtual.clear();
    m_devices.shutdown();
}

} // namespace kke
