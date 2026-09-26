#include "InputScreen.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>

#include <algorithm>
#include <cmath>
#include <typeindex>

namespace kke_demo {

using kke::Binding;
using kke::InputModule;
using kke::InputSource;
using kke::SourceKind;
using kke::Trigger;

namespace {

std::string fmt2(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%+.2f", v);
    return b;
}

std::string pct(float v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f%%", v);
    return b;
}

InputScreen::AxisView axisView(const std::string& name, float v, bool centered = true) {
    InputScreen::AxisView a;
    a.name = name;
    a.value = fmt2(v);
    v = std::clamp(v, -1.0f, 1.0f);
    if (centered) {
        a.left = pct(50.0f + std::min(v, 0.0f) * 50.0f);
        a.width = pct(std::abs(v) * 50.0f);
    } else { // triggers: 0..1 from the left
        a.left = "0%";
        a.width = pct(std::max(v, 0.0f) * 100.0f);
    }
    return a;
}

std::string triggerLabel(const Binding& b) {
    char buf[48];
    switch (b.trigger) {
    case Trigger::Press: return "press";
    case Trigger::Release: return "release";
    case Trigger::Hold: std::snprintf(buf, sizeof(buf), "hold %.2fs", b.holdTime); return buf;
    case Trigger::Tap: return "tap";
    case Trigger::DoubleTap: return "double tap";
    case Trigger::Continuous: return "while held";
    case Trigger::Toggle: return "toggle";
    }
    return "?";
}

const char* kPadButtonShort[] = { "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB", "Up", "Down", "Left", "Right",
                                  "Misc/QAM", "R4", "L4", "R5", "L5", "Touch", "M2", "M3", "M4", "M5", "M6" };

} // namespace

std::vector<kke::ModuleDependency> InputScreen::dependencies() const {
    return { { std::type_index(typeid(kke::InputModule)), true, "devices and bindings" },
             { std::type_index(typeid(kke::UiModule)), true, "the input screen is an RmlUi document" } };
}

// The standard character actions plus a few Tarkov-style extras, so every
// trigger type has something to try.
void InputScreen::defineGameActions() {
    kke::InputMap& m = m_input->map(0);
    InputModule::defineCharacterActions(m);
    auto def = [&](const char* id, const char* label, const char* cat) { m.defineAction({ id, label, cat, "game" }); };
    def("lean.left", "Lean left", "Movement");
    def("lean.right", "Lean right", "Movement");
    def("reload", "Reload", "Combat");
    def("reload.quick", "Quick reload (drop mag)", "Combat");
    def("ammo.check", "Check ammo", "Combat");
    def("inventory", "Inventory", "Interface");
    auto add = [&](Binding b) { m.addBinding(b); };
    add(InputModule::bind("lean.left", InputModule::key(SDL_SCANCODE_Q), Trigger::Hold));
    add(InputModule::bind("lean.right", InputModule::key(SDL_SCANCODE_E), Trigger::Hold));
    add(InputModule::bind("reload", InputModule::key(SDL_SCANCODE_R), Trigger::Tap));
    add(InputModule::bind("reload.quick", InputModule::key(SDL_SCANCODE_R), Trigger::DoubleTap));
    add(InputModule::bind("reload", InputModule::pad(SDL_GAMEPAD_BUTTON_WEST), Trigger::Tap));
    add(InputModule::bind("reload.quick", InputModule::pad(SDL_GAMEPAD_BUTTON_WEST), Trigger::DoubleTap));
    Binding ammo = InputModule::bind("ammo.check", InputModule::key(SDL_SCANCODE_T));
    ammo.modifiers = { InputModule::key(SDL_SCANCODE_LALT) }; // Tarkov's Alt+T
    add(ammo);
    Binding ammoPad = InputModule::bind("ammo.check", InputModule::pad(SDL_GAMEPAD_BUTTON_WEST), Trigger::Hold);
    ammoPad.holdTime = 0.5f;
    add(ammoPad);
    add(InputModule::bind("inventory", InputModule::key(SDL_SCANCODE_TAB)));
    add(InputModule::bind("inventory", InputModule::pad(SDL_GAMEPAD_BUTTON_BACK)));
    m_input->commitDefaults();
}

void InputScreen::init(kke::Application& app) {
    m_app = &app;
    m_input = app.getModule<kke::InputModule>();
    m_ui = app.getModule<kke::UiModule>();
    defineGameActions();

    Rml::DataModelConstructor c = m_ui->context()->CreateDataModel("input");
    if (auto s = c.RegisterStruct<DeviceView>()) {
        s.RegisterMember("ref", &DeviceView::ref);
        s.RegisterMember("label", &DeviceView::label);
        s.RegisterMember("type", &DeviceView::type);
        s.RegisterMember("info", &DeviceView::info);
        s.RegisterMember("twin", &DeviceView::twin);
        s.RegisterMember("active", &DeviceView::active);
        s.RegisterMember("selected", &DeviceView::selected);
        s.RegisterMember("connected", &DeviceView::connected);
        s.RegisterMember("rumble", &DeviceView::rumble);
    }
    if (auto s = c.RegisterStruct<Cell>()) {
        s.RegisterMember("name", &Cell::name);
        s.RegisterMember("on", &Cell::on);
    }
    if (auto s = c.RegisterStruct<AxisView>()) {
        s.RegisterMember("name", &AxisView::name);
        s.RegisterMember("value", &AxisView::value);
        s.RegisterMember("left", &AxisView::left);
        s.RegisterMember("width", &AxisView::width);
    }
    if (auto s = c.RegisterStruct<BindView>()) {
        s.RegisterMember("index", &BindView::index);
        s.RegisterMember("text", &BindView::text);
        s.RegisterMember("trigger", &BindView::trigger);
        s.RegisterMember("conflict", &BindView::conflict);
    }
    c.RegisterArray<std::vector<BindView>>();
    if (auto s = c.RegisterStruct<ActionView>()) {
        s.RegisterMember("id", &ActionView::id);
        s.RegisterMember("label", &ActionView::label);
        s.RegisterMember("category", &ActionView::category);
        s.RegisterMember("value", &ActionView::value);
        s.RegisterMember("held", &ActionView::held);
        s.RegisterMember("flash", &ActionView::flash);
        s.RegisterMember("type", &ActionView::type);
        s.RegisterMember("first", &ActionView::first);
        s.RegisterMember("bindings", &ActionView::bindings);
    }
    c.RegisterArray<std::vector<DeviceView>>();
    c.RegisterArray<std::vector<Cell>>();
    c.RegisterArray<std::vector<AxisView>>();
    c.RegisterArray<std::vector<ActionView>>();
    c.Bind("devices", &m_devices);
    c.Bind("selected", &m_selected);
    c.Bind("selected_name", &m_selectedName);
    c.Bind("selected_key", &m_selectedKey);
    c.Bind("alias", &m_alias);
    c.Bind("buttons", &m_buttons);
    c.Bind("pad_buttons", &m_padButtons);
    c.Bind("hats", &m_hats);
    c.Bind("axes", &m_axes);
    c.Bind("sensors", &m_sensors);
    c.Bind("touch", &m_touch);
    c.Bind("actions", &m_actions);
    c.Bind("capturing", &m_capturing);
    c.Bind("capture_hint", &m_captureHint);
    c.Bind("last_input", &m_lastInput);
    c.Bind("status", &m_status);

    auto arg = [](const Rml::VariantList& a, size_t i) { return i < a.size() ? a[i].Get<Rml::String>() : Rml::String(); };
    auto argInt = [](const Rml::VariantList& a, size_t i) { return i < a.size() ? a[i].Get<int>() : -1; };
    c.BindEventCallback("select_device", [this, arg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        m_selected = arg(a, 0);
        if (const auto* d = m_input->devices().find(static_cast<uint32_t>(std::stoul(m_selected.empty() ? "0" : m_selected))))
            m_alias = d->alias;
        m_model.DirtyVariable("alias");
    });
    c.BindEventCallback("identify", [this, arg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        m_input->devices().identify(static_cast<uint32_t>(std::stoul(arg(a, 0))));
    });
    c.BindEventCallback("rename", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (m_selected.empty()) return;
        m_input->devices().setAlias(static_cast<uint32_t>(std::stoul(m_selected)), m_alias);
        m_status = m_alias.empty() ? "Name cleared (save to keep)" : "Named '" + m_alias + "' (save to keep)";
    });
    c.BindEventCallback("swap_twin", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        if (m_selected.empty()) return;
        auto& devs = m_input->devices();
        const uint32_t ref = static_cast<uint32_t>(std::stoul(m_selected));
        const auto* d = devs.find(ref);
        if (!d || d->duplicateCount < 2) return;
        for (const auto& o : devs.devices())
            if (o.ref != ref && o.connected && o.name == d->name && o.vendor == d->vendor && o.product == d->product) {
                devs.swapIdentities(ref, o.ref);
                m_selected = std::to_string(o.ref);
                m_status = "Swapped: the two identical devices traded names and bindings (save to keep)";
                break;
            }
    });
    c.BindEventCallback("bind", [this, arg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        startCapture(arg(a, 0) + (a.size() > 1 ? "|" + arg(a, 1) : ""));
    });
    c.BindEventCallback("cancel_capture", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m_input->devices().cancelCapture();
    });
    c.BindEventCallback("unbind", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        m_input->map(0).removeBinding(static_cast<size_t>(argInt(a, 0)));
        m_input->map(0).resetStates();
    });
    c.BindEventCallback("cycle_trigger", [this, argInt](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        const int i = argInt(a, 0);
        auto& m = m_input->map(0);
        if (i < 0 || i >= static_cast<int>(m.bindings().size())) return;
        static const Trigger order[] = { Trigger::Press, Trigger::Hold, Trigger::Tap, Trigger::DoubleTap, Trigger::Toggle, Trigger::Release, Trigger::Continuous };
        Binding& b = m.binding(static_cast<size_t>(i));
        size_t k = 0;
        while (k < std::size(order) && order[k] != b.trigger) ++k;
        b.trigger = order[(k + 1) % std::size(order)];
        m.resetStates();
    });
    c.BindEventCallback("reset_action", [this, arg](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& a) {
        m_input->map(0).restoreDefaults(arg(a, 0));
    });
    c.BindEventCallback("reset_all", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m_input->map(0).restoreDefaults();
        m_status = "All bindings back to defaults (save to keep)";
    });
    c.BindEventCallback("left_handed", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        InputModule::mirrorKeyboard(m_input->map(0));
        m_status = "Keyboard mirrored for the left hand (WASD -> OKL;, Shift -> Right Shift ...). Press again to mirror back. Save to keep.";
    });
    c.BindEventCallback("save", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m_status = m_input->save() ? "Saved to " + m_input->path() : "Could not write " + m_input->path();
    });
    c.BindEventCallback("reload", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m_input->map(0).restoreDefaults();
        m_status = m_input->load() ? "Reloaded " + m_input->path() : "No saved bindings: using defaults";
    });
    m_model = c.GetModelHandle();
}

void InputScreen::startCapture(const std::string& spec) {
    m_captureAction = spec;
    const std::string id = spec.substr(0, spec.find('|'));
    const kke::ActionDef* def = m_input->map(0).action(id);
    if (!def) return;
    kke::InputDevices::CaptureOptions o;
    o.allowMouseMotion = def->type != kke::ActionType::Button;
    o.allowGyro = def->type != kke::ActionType::Button;
    m_input->devices().beginCapture(o);
    m_capturing = true;
    // While listening, nothing else reacts to what you press.
    m_input->map(0).setContextEnabled("game", false);
    m_input->map(0).setContextEnabled("ui", false);
    m_captureHint = "Binding '" + def->label + "': press a key, button, hat or combination (hold modifiers first), release to finish." +
                    (o.allowMouseMotion ? " Move the mouse, a stick or turn the controller to bind an axis." : "") + " Esc cancels.";
}

void InputScreen::finishCapture() {
    auto& devs = m_input->devices();
    auto& map = m_input->map(0);
    map.setContextEnabled("game", true);
    map.setContextEnabled("ui", true);
    map.resetStates();
    m_capturing = false;
    if (devs.captureStatus() != kke::InputDevices::CaptureStatus::Done) {
        m_status = "Cancelled";
        devs.cancelCapture();
        return;
    }
    const auto cap = devs.captured();
    const std::string id = m_captureAction.substr(0, m_captureAction.find('|'));
    const std::string dir = m_captureAction.find('|') == std::string::npos ? "" : m_captureAction.substr(m_captureAction.find('|') + 1);
    const kke::ActionDef* def = map.action(id);
    if (!def) return;
    Binding b;
    b.action = id;
    b.source = cap.source;
    b.modifiers = cap.modifiers;
    b.trigger = def->type == kke::ActionType::Button ? Trigger::Press : Trigger::Continuous;
    const bool axis = cap.source.kind == SourceKind::GamepadAxis || cap.source.kind == SourceKind::JoyAxis;
    if (def->type != kke::ActionType::Button) {
        if (dir == "down" || dir == "left" || dir == "neg") b.scale = -1.0f;
        if (dir == "up" || dir == "down") b.component = 1;
    }
    if (def->type == kke::ActionType::Axis2D && axis) {
        // A stick: bind both of its axes as one 2D binding.
        const int base = cap.source.code - cap.source.code % 2;
        b.source = { cap.source.kind, cap.source.device, base, 0 };
        b.sourceY = { cap.source.kind, cap.source.device, base + 1, 0 };
        b.deadzone = 0.15f;
        b.invert = true;
        b.scale = 1.0f;
        b.component = 0;
    } else if (def->type == kke::ActionType::Axis2D && (cap.source.kind == SourceKind::MouseMotion || cap.source.kind == SourceKind::Gyro)) {
        // Mouse / gyro: both axes (x = yaw, y = pitch).
        const bool gyro = cap.source.kind == SourceKind::Gyro;
        Binding x = b, y = b;
        x.source = { cap.source.kind, cap.source.device, gyro ? 1 : 0, 0 };
        y.source = { cap.source.kind, cap.source.device, gyro ? 0 : 1, 0 };
        x.scale = y.scale = gyro ? 1.0f / 180.0f : 1.0f; // gyro deg/s -> "full stick" at 180 deg/s
        x.component = 0;
        y.component = 1;
        x.invert = gyro; // turning left is +yaw
        y.invert = !gyro;
        map.addBinding(x);
        map.addBinding(y);
        m_status = "Bound " + devs.describe(x.source) + " + " + devs.describe(y.source) + " to " + def->label;
        return;
    } else if (def->type == kke::ActionType::Axis1D && axis) {
        b.source.half = 0;
        b.deadzone = 0.08f;
    }
    map.addBinding(b);
    std::string text;
    for (const InputSource& m : b.modifiers) text += devs.describe(m) + " + ";
    m_status = "Bound " + text + devs.describe(b.source) + " to " + def->label;
}

void InputScreen::frameStart(const kke::UpdateContext&) {
    if (m_capturing && m_input->devices().captureStatus() != kke::InputDevices::CaptureStatus::Listening) finishCapture();
}

void InputScreen::refreshDevices() {
    const auto& devs = m_input->devices();
    const double now = static_cast<double>(SDL_GetTicksNS()) * 1e-9;
    m_devices.clear();
    // Pads and sticks first, then keyboards and mice.
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& d : devs.devices()) {
            const bool joy = d.kind == kke::InputDevices::Kind::Gamepad || d.kind == kke::InputDevices::Kind::Joystick;
            if ((pass == 0) != joy) continue;
            if (!joy && (!d.connected || d.sdlId == 0)) continue; // 0 = the catch-all for id-less events
            DeviceView v;
            v.ref = std::to_string(d.ref);
            v.label = d.label();
            v.type = d.typeName;
            v.connected = d.connected;
            v.rumble = d.hasRumble || d.hasLed;
            v.active = d.lastActivity >= 0.0 && now - d.lastActivity < 0.3;
            v.selected = v.ref == m_selected;
            if (joy) {
                char buf[160];
                std::snprintf(buf, sizeof(buf), "%04x:%04x  %d buttons, %d axes, %d hats%s%s%s%s", d.vendor, d.product, d.numButtons, d.numAxes, d.numHats,
                              d.hasGyro ? ", gyro" : "", d.hasAccel ? ", accel" : "", d.numTouchpads ? ", touchpad" : "", d.connected ? "" : "  (disconnected)");
                v.info = buf;
                if (d.isSteamVirtual) v.info += "  Steam Input virtual pad: disable Steam Input for this game to see the real device";
            }
            if (d.duplicateCount > 1) v.twin = "identical #" + std::to_string(d.duplicateIndex + 1) + " of " + std::to_string(d.duplicateCount);
            m_devices.push_back(std::move(v));
        }
    if (m_selected.empty() || !devs.find(static_cast<uint32_t>(std::stoul(m_selected)))) {
        if (const auto* d = devs.lastActive()) m_selected = std::to_string(d->ref);
    }
}

void InputScreen::refreshLive() {
    m_buttons.clear();
    m_padButtons.clear();
    m_hats.clear();
    m_axes.clear();
    m_sensors.clear();
    m_touch.clear();
    const auto* d = m_selected.empty() ? nullptr : m_input->devices().find(static_cast<uint32_t>(std::stoul(m_selected)));
    if (!d) {
        m_selectedName = "No controller selected";
        m_selectedKey.clear();
        return;
    }
    m_selectedName = d->label() + "  (" + d->name + ")";
    m_selectedKey = d->stableKey;
    if (d->kind == kke::InputDevices::Kind::Keyboard) {
        for (size_t k = 0; k < d->keys.size(); ++k)
            if (d->keys[k]) m_buttons.push_back({ SDL_GetScancodeName(static_cast<SDL_Scancode>(k)), true });
        return;
    }
    if (d->kind == kke::InputDevices::Kind::Mouse) {
        static const char* names[] = { "", "Left", "Middle", "Right", "Back", "Forward" };
        for (int b = 1; b <= 5; ++b) m_buttons.push_back({ names[b], d->mouseButtons[b] != 0 });
        return;
    }
    if (d->gamepad) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b)
            if (SDL_GamepadHasButton(d->gamepad, static_cast<SDL_GamepadButton>(b)))
                m_padButtons.push_back({ kPadButtonShort[b], d->padButtons[b] != 0 });
        static const char* an[] = { "Left X", "Left Y", "Right X", "Right Y", "LT", "RT" };
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a)
            if (SDL_GamepadHasAxis(d->gamepad, static_cast<SDL_GamepadAxis>(a))) m_axes.push_back(axisView(an[a], d->padAxes[a], a < 4));
    }
    for (int b = 0; b < d->numButtons; ++b) m_buttons.push_back({ std::to_string(b + 1), d->buttons[b] != 0 });
    for (int a = 0; a < d->numAxes; ++a) m_axes.push_back(axisView("Raw axis " + std::to_string(a + 1), d->axes[a]));
    static const char* hd[] = { "Up", "Right", "Down", "Left" };
    static const uint8_t bits[] = { SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT };
    for (int h = 0; h < d->numHats; ++h)
        for (int k = 0; k < 4; ++k) m_hats.push_back({ "Hat " + std::to_string(h + 1) + " " + hd[k], (d->hats[h] & bits[k]) != 0 });
    char buf[200];
    if (d->hasGyro || d->hasAccel) {
        std::snprintf(buf, sizeof(buf), "Gyro (deg/s)  pitch %+7.1f  yaw %+7.1f  roll %+7.1f      Accel (m/s2)  %+5.2f %+5.2f %+5.2f", d->gyro.x, d->gyro.y,
                      d->gyro.z, d->accel.x, d->accel.y, d->accel.z);
        m_sensors = buf;
    }
    for (size_t t = 0; t < d->touch.size(); ++t)
        for (size_t f = 0; f < d->touch[t].size(); ++f)
            if (d->touch[t][f].down) {
                std::snprintf(buf, sizeof(buf), "Pad %zu finger %zu: %.2f, %.2f  ", t + 1, f + 1, d->touch[t][f].x, d->touch[t][f].y);
                m_touch += buf;
            }
}

void InputScreen::refreshActions() {
    auto& map = m_input->map(0);
    auto& devs = m_input->devices();
    // Game actions first, menu navigation last.
    std::vector<const kke::ActionDef*> defs;
    for (int pass = 0; pass < 2; ++pass)
        for (const kke::ActionDef& d : map.actions())
            if ((d.context == "ui") == (pass == 1)) defs.push_back(&d);
    m_flash.resize(defs.size(), 0.0f);
    m_actions.resize(defs.size());
    for (size_t a = 0; a < defs.size(); ++a) {
        const kke::ActionDef& def = *defs[a];
        const kke::ActionState& st = map.state(def.id);
        ActionView& v = m_actions[a];
        v.id = def.id;
        v.label = def.label;
        v.category = def.category;
        v.type = kke::toString(def.type);
        v.first = a == 0 || defs[a - 1]->category != def.category;
        v.held = st.held;
        if (st.pressed) m_flash[a] = 0.35f;
        v.flash = m_flash[a] > 0.0f;
        if (def.type == kke::ActionType::Button) v.value = st.held ? "ON" : "";
        else if (def.type == kke::ActionType::Axis1D) v.value = fmt2(st.value);
        else v.value = fmt2(st.value2.x) + "  " + fmt2(st.value2.y);
        v.bindings.clear();
        for (size_t i : map.bindingsFor(def.id)) {
            const Binding& b = map.bindings()[i];
            BindView bv;
            bv.index = static_cast<int>(i);
            for (const InputSource& m : b.modifiers) bv.text += devs.describe(m) + " + ";
            bv.text += devs.describe(b.source);
            if (b.sourceY.valid()) bv.text += " / " + devs.describe(b.sourceY);
            if (def.type != kke::ActionType::Button && !b.sourceY.valid() && !b.source.isAnalog())
                bv.text += def.type == kke::ActionType::Axis2D ? (b.component == 1 ? (b.scale < 0 ? "  (down)" : "  (up)") : (b.scale < 0 ? "  (left)" : "  (right)"))
                                                              : (b.scale < 0 ? "  (-)" : "  (+)");
            bv.trigger = triggerLabel(b);
            bv.conflict = !map.conflicts(i).empty();
            v.bindings.push_back(std::move(bv));
        }
    }
}

void InputScreen::update(const kke::UpdateContext& ctx) {
    for (float& f : m_flash) f = std::max(0.0f, f - ctx.dt);
    // A readable "what did I just press" line, for testing bindings.
    for (const auto& d : m_input->devices().devices())
        if (d.lastActivity >= 0.0 && static_cast<double>(SDL_GetTicksNS()) * 1e-9 - d.lastActivity < 0.05) m_lastInput = "Last touched: " + d.label();
    refreshDevices();
    refreshLive();
    refreshActions();
    m_model.DirtyAllVariables();
}

} // namespace kke_demo
