// Any controller drives (Kees, 2026-09-28: "any type of controller, even a
// flight stick should be able to race"):
//
//   - a gamepad and the keyboard: RacingModule.cpp's bindings;
//   - a flight stick, out of the box: the stick steers, push it forward
//     for the gas and pull it back to brake, the trigger is the handbrake,
//     button 2 the camera, 3 look back, 4 back on the track, 5 / 6 the
//     gears, the hat looks round (and moves through the menus, the trigger
//     picks, button 2 goes back);
//   - a wheel and pedals (or a stick with a throttle lever), after a
//     short set-up: Settings > Driving > "Set up a wheel or flight stick",
//     or the pause menu. Turn it left, turn it right, press the gas,
//     press the brake: each is found by what moved, so any wheel works,
//     pedals on their own USB plug too. Pedals rest at one end of their
//     axis (a plain binding can't say that), so the game reads them
//     itself (readWheel). Saved in racing_wheel.json (YAML works too).
//
// The lobby seats a flight stick or a wheel like a controller: its
// trigger (button 1) joins (LobbyModule::setFlightSticks).

#include "RacingModule.h"

#include "kke/DataFile.h"
#include "kke/InputDevices.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace racing {

namespace {

const char* kWheelFile = "racing_wheel.json";
constexpr float kMoved = 0.5f;     // an axis this far from where it was is the one being moved
constexpr float kHoldFor = 0.6f;   // s held there to count
constexpr float kDoneFor = 2.5f;   // s "all set" stays up

kke::InputSource joyAxis(int axis, int8_t half = 0) { return { kke::SourceKind::JoyAxis, 0, axis, half }; }
kke::InputSource joyButton(int button) { return { kke::SourceKind::JoyButton, 0, button, 0 }; }
kke::InputSource joyHat(int direction) { return { kke::SourceKind::JoyHat, 0, direction, 0 }; }

nlohmann::json toJson(const AxisSetup& a) { return { { "device", a.device }, { "axis", a.axis }, { "rest", a.rest }, { "full", a.full } }; }
AxisSetup axisFrom(const nlohmann::json& j) {
    AxisSetup a;
    if (!j.is_object()) return a;
    a.device = j.value("device", std::string());
    a.axis = j.value("axis", -1);
    a.rest = j.value("rest", 0.0f);
    a.full = j.value("full", 1.0f);
    return a;
}

const char* kSteps[] = { "Turn the wheel (or stick) all the way LEFT", "Now all the way RIGHT", "Let go. Press the GAS all the way",
                         "Let go. Press the BRAKE all the way" };
const char* kStepSubs[] = { "and hold it there", "and hold it there", "a pedal, a throttle lever, or the stick pushed forward",
                            "a pedal, or the stick pulled back" };

} // namespace

// A flight stick's defaults (the first stick axis, the trigger, the hat).
// A wheel's pedals aren't bound here: they rest at an end of their axis,
// so they wait for the set-up.
void RacingModule::bindJoysticks(kke::InputMap& in) {
    using IM = kke::InputModule;
    kke::Binding steer = IM::bind("steer", joyAxis(0), kke::Trigger::Continuous);
    steer.deadzone = 0.04f;
    in.addBinding(steer);
    kke::Binding gas = IM::bind("throttle", joyAxis(1, -1), kke::Trigger::Continuous);
    kke::Binding brake = IM::bind("brake", joyAxis(1, 1), kke::Trigger::Continuous);
    gas.deadzone = brake.deadzone = 0.08f;
    in.addBinding(gas);
    in.addBinding(brake);
    in.addBinding(IM::bind("handbrake", joyButton(0), kke::Trigger::Continuous));
    in.addBinding(IM::bind("camera", joyButton(1)));
    in.addBinding(IM::bind("look.back", joyButton(2), kke::Trigger::Continuous));
    in.addBinding(IM::bind("reset.car", joyButton(3)));
    in.addBinding(IM::bind("shift.up", joyButton(5)));
    in.addBinding(IM::bind("shift.down", joyButton(4)));
    in.addBinding(IM::bind("race.again.over", joyButton(0))); // not mid-race: it's the handbrake too
    // The hat looks round, the way the right stick does.
    const struct {
        int dir, component;
        float scale;
    } hat[] = { { 1, 0, 1.0f }, { 3, 0, -1.0f }, { 0, 1, -1.0f }, { 2, 1, 1.0f } };
    for (const auto& h : hat) {
        kke::Binding b = IM::bind("look", joyHat(h.dir), kke::Trigger::Continuous);
        b.component = h.component;
        b.scale = h.scale;
        in.addBinding(b);
    }
    // The menus (GameShellModule reads ui.*; shell.open opens the pause
    // menu): the hat moves, the trigger picks, button 2 goes back, button
    // 7 (a stick's base, a wheel's start) pauses.
    const struct {
        const char* action;
        int dir;
    } nav[] = { { "ui.up", 0 }, { "ui.right", 1 }, { "ui.down", 2 }, { "ui.left", 3 } };
    for (const auto& n : nav) in.addBinding(IM::bind(n.action, joyHat(n.dir)));
    in.addBinding(IM::bind("ui.accept", joyButton(0)));
    in.addBinding(IM::bind("ui.back", joyButton(1)));
    if (!in.action("shell.open")) in.defineAction({ "shell.open", "Pause menu (a flight stick or wheel)", "Menus", "shell" });
    in.addBinding(IM::bind("shell.open", joyButton(6)));
}

void RacingModule::loadWheelSetup() {
    nlohmann::json j;
    std::string error;
    bool exists = false;
    if (!kke::datafile::loadPath(kWheelFile, j, &error, &exists)) {
        if (exists) kke::log::get(name())->warn("could not read {}: {}", kWheelFile, error);
        return;
    }
    m_wheel.steerLeft = axisFrom(j.value("steer_left", nlohmann::json()));
    m_wheel.steerRight = axisFrom(j.value("steer_right", nlohmann::json()));
    m_wheel.gas = axisFrom(j.value("gas", nlohmann::json()));
    m_wheel.brake = axisFrom(j.value("brake", nlohmann::json()));
    if (!m_wheel.any()) return;
    kke::log::get(name())->info("a wheel or stick is set up ({})", kWheelFile);
    for (int p = 0; p < m_input->players(); ++p) dropJoystickPedals(m_input->map(p));
}

void RacingModule::saveWheelSetup() const {
    const nlohmann::json j = { { "steer_left", toJson(m_wheel.steerLeft) },
                               { "steer_right", toJson(m_wheel.steerRight) },
                               { "gas", toJson(m_wheel.gas) },
                               { "brake", toJson(m_wheel.brake) } };
    std::string error;
    if (!kke::datafile::saveFile(kke::datafile::saveTarget(kWheelFile), j, &error))
        kke::log::get(name())->warn("could not save {}: {}", kWheelFile, error);
}

// The axis's value on its device, if that device is plugged in and is one
// of this player's (`devices` empty: everything is). -2 otherwise.
float RacingModule::wheelAxis(const AxisSetup& a, const std::vector<uint32_t>& devices) const {
    if (!a.valid()) return -2.0f;
    for (const kke::InputDevices::Device& d : m_input->devices().devices()) {
        if (d.stableKey != a.device || !d.connected) continue;
        if (!devices.empty() && std::find(devices.begin(), devices.end(), d.ref) == devices.end()) return -2.0f;
        if (a.axis >= static_cast<int>(d.axes.size())) return -2.0f;
        return d.axes[static_cast<size_t>(a.axis)];
    }
    return -2.0f;
}

// A set-up wheel's steering and pedals on top of what the bindings say.
void RacingModule::readWheel(Car& c, float& steer, float& throttle, float& brake) const {
    if (!m_wheel.any() || m_wheelStep >= 0) return;
    const std::vector<uint32_t>& devices = m_input->map(c.player).devices();
    const float v = wheelAxis(m_wheel.steerLeft, devices);
    if (v > -1.5f && m_wheel.steerRight.valid()) {
        // Which side of the middle it's on, then how far to that side's lock.
        const float centre = m_wheel.steerLeft.rest;
        const bool leftSide = (v - centre) * (m_wheel.steerLeft.full - centre) > 0.0f;
        const float amount = leftSide ? m_wheel.steerLeft.amount(v) : m_wheel.steerRight.amount(v);
        const float dead = 0.03f;
        const float s = std::max(0.0f, amount - dead) / (1.0f - dead);
        steer = std::clamp(steer + (leftSide ? -s : s), -1.0f, 1.0f);
    }
    const float g = wheelAxis(m_wheel.gas, devices);
    if (g > -1.5f) throttle = std::max(throttle, std::max(0.0f, m_wheel.gas.amount(g) - 0.04f) / 0.96f);
    const float b = wheelAxis(m_wheel.brake, devices);
    if (b > -1.5f) brake = std::max(brake, std::max(0.0f, m_wheel.brake.amount(b) - 0.04f) / 0.96f);
}

void RacingModule::startWheelSetup() {
    m_wheelBaseline.clear();
    for (const kke::InputDevices::Device& d : m_input->devices().devices())
        if (d.connected && d.kind == kke::InputDevices::Kind::Joystick && !d.axes.empty()) m_wheelBaseline[d.stableKey] = d.axes;
    m_wheelHeld = 0.0f;
    m_wheelCandidate = {};
    if (m_wheelBaseline.empty()) {
        m_wheelStep = -1;
        m_wheelPrompt = "No wheel or flight stick found";
        m_wheelSub = "Plug it in and try again. Gamepads work already.";
        m_wheelDone = kDoneFor;
        return;
    }
    m_wheelStep = 0;
    m_wheelSkip = true; // the button that opened this is still down
    if (m_lobby) m_lobby->setSuspended(true);
    kke::log::get(name())->info("wheel set-up: {} stick(s) or wheel(s) plugged in", m_wheelBaseline.size());
}

void RacingModule::updateWheelSetup(float dt) {
    if (m_wheelStep < 0) {
        if (m_wheelDone > 0.0f && (m_wheelDone -= dt) <= 0.0f) m_wheelPrompt.clear();
        return;
    }
    kke::InputDevices& devices = m_input->devices();
    // B or Backspace skips a step (no pedals, no throttle lever).
    bool skip = devices.value(kke::InputModule::key(SDL_SCANCODE_BACKSPACE), nullptr) > 0.5f ||
                devices.value(kke::InputModule::pad(SDL_GAMEPAD_BUTTON_EAST), nullptr) > 0.5f;
    const bool skipPressed = skip && !m_wheelSkip;
    m_wheelSkip = skip;

    // What moved most since the step began, and how far.
    AxisSetup best;
    float bestMove = kMoved;
    float now = 0.0f;
    for (const kke::InputDevices::Device& d : devices.devices()) {
        const auto base = m_wheelBaseline.find(d.stableKey);
        if (!d.connected || base == m_wheelBaseline.end()) continue;
        for (size_t i = 0; i < d.axes.size() && i < base->second.size(); ++i) {
            const int axis = static_cast<int>(i);
            const bool steering = d.stableKey == m_wheel.steerLeft.device && axis == m_wheel.steerLeft.axis;
            if (m_wheelStep == 1 && !steering) continue; // right: the same axis as left
            if (m_wheelStep >= 2 && steering) continue;  // the pedals: anything but the steering
            if (m_wheelStep == 3 && d.stableKey == m_wheel.gas.device && axis == m_wheel.gas.axis) continue;
            const float rest = m_wheelStep == 1 ? m_wheel.steerLeft.rest : base->second[i];
            const float moved = d.axes[i] - rest;
            if (m_wheelStep == 1 && moved * (m_wheel.steerLeft.full - rest) > 0.0f) continue; // still on the left
            if (std::fabs(moved) > bestMove) {
                bestMove = std::fabs(moved);
                best.device = d.stableKey;
                best.axis = axis;
                best.rest = rest;
                now = d.axes[i];
            }
        }
    }
    if (best.axis >= 0 && best.device == m_wheelCandidate.device && best.axis == m_wheelCandidate.axis) {
        m_wheelHeld += dt;
        // All the way: the furthest it got while held.
        if (std::fabs(now - best.rest) > std::fabs(m_wheelCandidate.full - m_wheelCandidate.rest)) m_wheelCandidate.full = now;
    } else {
        m_wheelCandidate = best;
        m_wheelCandidate.full = now;
        m_wheelHeld = 0.0f;
    }

    const int step = m_wheelStep;
    m_wheelPrompt = kSteps[step];
    m_wheelSub = std::string(kStepSubs[step]) + (step >= 1 ? "   ·   B or Backspace: skip" : "   ·   B or Backspace: stop");
    m_wheelMeter = m_wheelCandidate.axis >= 0 ? std::min(1.0f, m_wheelHeld / kHoldFor) : 0.0f;
    if (m_wheelHeld < kHoldFor && !skipPressed) return;

    // Taken (or skipped): on to the next.
    AxisSetup* slot[] = { &m_wheel.steerLeft, &m_wheel.steerRight, &m_wheel.gas, &m_wheel.brake };
    if (skipPressed) {
        if (step == 0) {
            // Stopped before anything: the old set-up stays.
            loadWheelSetup();
            m_wheelStep = -1;
            m_wheelPrompt.clear();
            if (m_lobby) m_lobby->setSuspended(false);
            return;
        }
        *slot[step] = {};
    } else {
        *slot[step] = m_wheelCandidate;
        kke::log::get(name())->info("wheel set-up: {} is axis {} ({:.2f} at rest, {:.2f} all the way)", kSteps[step], m_wheelCandidate.axis + 1,
                                    m_wheelCandidate.rest, m_wheelCandidate.full);
    }
    if (step == 0) m_wheel.steerRight = {};
    m_wheelHeld = 0.0f;
    m_wheelCandidate = {};
    // The pedals' rest is where they are now (the wheel let go of).
    for (const kke::InputDevices::Device& d : devices.devices())
        if (m_wheelBaseline.count(d.stableKey)) m_wheelBaseline[d.stableKey] = d.axes;
    if (++m_wheelStep < 4) return;

    // Done: the stick's own bindings for what the set-up now does are
    // dropped (they'd add a resting pedal to the brake), the rest stay.
    m_wheelStep = -1;
    m_wheelMeter = 0.0f;
    if (m_lobby) m_lobby->setSuspended(false);
    saveWheelSetup();
    for (int p = 0; p < m_input->players(); ++p) dropJoystickPedals(m_input->map(p));
    std::string what = m_wheel.steerLeft.valid() && m_wheel.steerRight.valid() ? "steering" : "";
    if (m_wheel.gas.valid()) what += what.empty() ? "gas" : ", gas";
    if (m_wheel.brake.valid()) what += what.empty() ? "brake" : " and brake";
    m_wheelPrompt = what.empty() ? "Nothing set up" : "All set";
    m_wheelSub = what.empty() ? "The stick keeps its usual controls." : "Your " + what + " saved for next time.";
    m_wheelDone = kDoneFor;
}

// With a wheel set up, the flight-stick defaults on the axes it uses go:
// the wheel's own reading replaces them.
void RacingModule::dropJoystickPedals(kke::InputMap& in) const {
    const bool steering = m_wheel.steerLeft.valid() && m_wheel.steerRight.valid();
    const bool pedals = m_wheel.gas.valid() || m_wheel.brake.valid();
    for (const char* action : { "steer", "throttle", "brake" }) {
        if (std::string(action) == "steer" ? !steering : !pedals) continue;
        std::vector<size_t> found = in.bindingsFor(action);
        std::sort(found.rbegin(), found.rend());
        for (size_t i : found)
            if (in.bindings()[i].source.kind == kke::SourceKind::JoyAxis) in.removeBinding(i);
    }
}

} // namespace racing
