#include "kke/modules/InputModule.h"

#include "kke/Log.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <fstream>

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
    m.addBinding(bind("audio.ping", pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)));
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

void InputModule::init(Application&) {
    m_devices.init();
    // Device names/swaps load now (bindings load in commitDefaults(), after
    // the game has defined its actions).
    std::ifstream f(m_path);
    if (f) {
        try {
            nlohmann::json j = nlohmann::json::parse(f);
            if (j.contains("devices")) m_devices.loadDevices(j["devices"]);
        } catch (const std::exception& e) {
            log::get(name())->warn("{}: {}", m_path, e.what());
        }
    }
    commitDefaults(); // UI actions; games call it again after adding theirs
    if (const char* v = std::getenv("KKE_VIRTUAL_INPUT"); v && *v) attachVirtualDevices(v);
    if (const char* a = std::getenv("KKE_VIRTUAL_INPUT_ANIMATE")) m_animateVirtual = *a && *a != '0';
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
    if (spec.find("pad") != std::string::npos) attach(true, 0);
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
}

bool InputModule::load() {
    std::ifstream f(m_path);
    if (!f) return false;
    try {
        nlohmann::json j = nlohmann::json::parse(f);
        const nlohmann::json& list = j.value("players", nlohmann::json::array());
        for (size_t p = 0; p < m_maps.size() && p < list.size(); ++p) {
            const int dropped = m_maps[p]->load(list[p]);
            if (dropped < 0) m_maps[p]->restoreDefaults();
            // Actions the file doesn't mention yet (added by an update) keep their defaults.
            for (const ActionDef& a : m_maps[p]->actions())
                if (m_maps[p]->bindingsFor(a.id).empty()) m_maps[p]->restoreDefaults(a.id);
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
    const std::string tmp = m_path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        if (!f) return false;
        f << j.dump(2);
        if (!f) return false;
    }
    std::remove(m_path.c_str());
    return std::rename(tmp.c_str(), m_path.c_str()) == 0; // no half-written file on a crash
}

void InputModule::frameStart(const UpdateContext& ctx) {
    m_now = ctx.totalTime;
    if (m_animateVirtual) animateVirtualDevices(ctx.totalTime);
    m_devices.poll();
    for (auto& m : m_maps) m->update(m_devices, m_now);
}

void InputModule::frameEnd() { m_devices.endFrame(); }

void InputModule::onEvent(const SDL_Event& event) { m_devices.handleEvent(event); }

void InputModule::shutdown() {
    for (Virtual& v : m_virtual) {
        if (v.joy) SDL_CloseJoystick(v.joy);
        SDL_DetachVirtualJoystick(v.id);
    }
    m_virtual.clear();
    m_devices.shutdown();
}

} // namespace kke
