#include "kke/InputMap.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <map>
#include <tuple>

namespace {

using kke::InputSource;
using kke::SourceKind;
using kke::Trigger;

// Source values set by the test; device 0 in a query = any device.
class FakeState : public kke::InputState {
public:
    std::map<std::tuple<int, uint32_t, int>, float> values;
    void set(SourceKind k, int code, float v, uint32_t device = 1) { values[{ int(k), device, code }] = v; }
    float value(const InputSource& s, const std::vector<uint32_t>* allowed) const override {
        float best = 0.0f;
        for (const auto& [key, v] : values) {
            auto [k, dev, code] = key;
            if (k != int(s.kind) || code != s.code) continue;
            if (s.device != 0 && s.device != dev) continue;
            if (allowed && std::find(allowed->begin(), allowed->end(), dev) == allowed->end()) continue;
            if (std::abs(v) > std::abs(best)) best = v;
        }
        return best;
    }
};

InputSource key(int code) { return { SourceKind::Key, 0, code, 0 }; }
InputSource pad(int button) { return { SourceKind::GamepadButton, 0, button, 0 }; }
InputSource axis(int a, int8_t half = 0) { return { SourceKind::GamepadAxis, 0, a, half }; }

kke::Binding bind(const std::string& action, InputSource s, Trigger t = Trigger::Press) {
    kke::Binding b;
    b.action = action;
    b.source = s;
    b.trigger = t;
    return b;
}

struct Rig {
    kke::InputMap map;
    FakeState state;
    double t = 0.0;
    void step(double dt = 1.0 / 60.0) { t += dt; map.update(state, t); }
    // Holds `code` for `seconds`, stepping at 60 Hz; returns whether `action` fired.
    bool pressFor(int code, double seconds, const std::string& action) {
        bool fired = false;
        state.set(SourceKind::Key, code, 1.0f);
        for (double e = 0.0; e < seconds; e += 1.0 / 60.0) { step(); fired = fired || map.pressed(action); }
        state.set(SourceKind::Key, code, 0.0f);
        step();
        fired = fired || map.pressed(action);
        return fired;
    }
    bool idleFor(double seconds, const std::string& action) {
        bool fired = false;
        for (double e = 0.0; e < seconds; e += 1.0 / 60.0) { step(); fired = fired || map.pressed(action); }
        return fired;
    }
};

} // namespace

TEST(InputMap, PressHoldReleaseEdges) {
    Rig r;
    r.map.defineAction({ "jump", "Jump", "Movement" });
    r.map.addBinding(bind("jump", key(44)));
    r.step();
    EXPECT_FALSE(r.map.held("jump"));
    r.state.set(SourceKind::Key, 44, 1.0f);
    r.step();
    EXPECT_TRUE(r.map.pressed("jump"));
    EXPECT_TRUE(r.map.held("jump"));
    r.step();
    EXPECT_FALSE(r.map.pressed("jump")); // one frame only
    EXPECT_TRUE(r.map.held("jump"));
    EXPECT_GT(r.map.state("jump").heldFor, 0.0);
    r.state.set(SourceKind::Key, 44, 0.0f);
    r.step();
    EXPECT_TRUE(r.map.released("jump"));
    EXPECT_FALSE(r.map.held("jump"));
}

TEST(InputMap, TapAndHoldShareOneKey) {
    // Tarkov's lean: tap Q = toggle lean, hold Q = lean while held.
    Rig r;
    r.map.defineAction({ "lean.toggle", "Lean (tap)" });
    r.map.defineAction({ "lean.hold", "Lean (hold)" });
    auto hold = bind("lean.hold", key(20), Trigger::Hold);
    hold.holdTime = 0.3f;
    r.map.addBinding(bind("lean.toggle", key(20), Trigger::Tap));
    r.map.addBinding(hold);
    // A short press: tap only.
    bool held = false;
    r.state.set(SourceKind::Key, 20, 1.0f);
    for (int i = 0; i < 6; ++i) { r.step(); held = held || r.map.held("lean.hold"); }
    r.state.set(SourceKind::Key, 20, 0.0f);
    r.step();
    EXPECT_TRUE(r.map.pressed("lean.toggle"));
    EXPECT_FALSE(held);
    // A long press: hold only, from 0.3 s on.
    bool tapped = false;
    r.state.set(SourceKind::Key, 20, 1.0f);
    for (int i = 0; i < 12; ++i) { r.step(); EXPECT_FALSE(r.map.held("lean.hold")); }
    for (int i = 0; i < 30; ++i) { r.step(); tapped = tapped || r.map.pressed("lean.toggle"); }
    EXPECT_TRUE(r.map.held("lean.hold"));
    r.state.set(SourceKind::Key, 20, 0.0f);
    r.step();
    EXPECT_FALSE(tapped || r.map.pressed("lean.toggle"));
    EXPECT_TRUE(r.map.released("lean.hold"));
}

TEST(InputMap, DoubleTapWinsAndSingleTapWaitsForTheWindow) {
    Rig r;
    r.map.defineAction({ "reload", "Reload" });
    r.map.defineAction({ "reload.fast", "Quick reload" });
    r.map.addBinding(bind("reload", key(21), Trigger::Tap));
    r.map.addBinding(bind("reload.fast", key(21), Trigger::DoubleTap));
    // Single tap: fires, but only after the double-tap window passes.
    EXPECT_FALSE(r.pressFor(21, 0.05, "reload"));
    EXPECT_TRUE(r.idleFor(0.5, "reload"));
    // Double tap: the double fires, the single never does.
    bool single = false, dbl = false;
    single = r.pressFor(21, 0.05, "reload");
    r.idleFor(0.1, "reload");
    r.state.set(SourceKind::Key, 21, 1.0f);
    r.step();
    dbl = r.map.pressed("reload.fast");
    r.state.set(SourceKind::Key, 21, 0.0f);
    r.step();
    single = single || r.idleFor(0.6, "reload");
    EXPECT_TRUE(dbl);
    EXPECT_FALSE(single);
    // Two taps too far apart: two singles, no double.
    EXPECT_FALSE(r.pressFor(21, 0.05, "reload.fast"));
    EXPECT_TRUE(r.idleFor(0.5, "reload"));
    EXPECT_FALSE(r.pressFor(21, 0.05, "reload.fast"));
    EXPECT_TRUE(r.idleFor(0.5, "reload"));
}

TEST(InputMap, ToggleFlipsOnEachPress) {
    Rig r;
    r.map.defineAction({ "crouch", "Crouch" });
    r.map.addBinding(bind("crouch", key(6), Trigger::Toggle));
    r.pressFor(6, 0.1, "crouch");
    EXPECT_TRUE(r.map.held("crouch"));
    r.idleFor(0.2, "crouch");
    EXPECT_TRUE(r.map.held("crouch"));
    r.pressFor(6, 0.1, "crouch");
    EXPECT_FALSE(r.map.held("crouch"));
}

TEST(InputMap, ChordSuppressesThePlainBinding) {
    Rig r;
    r.map.defineAction({ "use", "Use" });
    r.map.defineAction({ "inspect", "Inspect" });
    r.map.addBinding(bind("use", key(8)));
    auto chord = bind("inspect", key(8));
    chord.modifiers = { key(224) }; // Ctrl
    r.map.addBinding(chord);
    r.state.set(SourceKind::Key, 224, 1.0f);
    r.state.set(SourceKind::Key, 8, 1.0f);
    r.step();
    EXPECT_TRUE(r.map.pressed("inspect"));
    EXPECT_FALSE(r.map.held("use"));
    r.state.set(SourceKind::Key, 224, 0.0f);
    r.state.set(SourceKind::Key, 8, 0.0f);
    r.step();
    r.state.set(SourceKind::Key, 8, 1.0f);
    r.step();
    EXPECT_TRUE(r.map.pressed("use"));
    EXPECT_FALSE(r.map.held("inspect"));
}

TEST(InputMap, KeysAndStickFeedOneMoveAxis) {
    Rig r;
    kke::ActionDef move{ "move", "Move", "Movement" };
    move.type = kke::ActionType::Axis2D;
    r.map.defineAction(move);
    auto w = bind("move", key(26), Trigger::Continuous);
    w.component = 1;
    auto s = w;
    s.source = key(22);
    s.scale = -1.0f;
    auto d = bind("move", key(7), Trigger::Continuous);
    r.map.addBinding(w);
    r.map.addBinding(s);
    r.map.addBinding(d);
    auto stick = bind("move", axis(0), Trigger::Continuous);
    stick.sourceY = axis(1);
    stick.deadzone = 0.2f;
    stick.invert = true; // SDL's stick Y is down-positive
    r.map.addBinding(stick);

    r.state.set(SourceKind::Key, 26, 1.0f);
    r.state.set(SourceKind::Key, 7, 1.0f);
    r.step();
    EXPECT_NEAR(glm::length(r.map.axis2("move")), 1.0f, 1e-5f); // W+D clamped to length 1
    EXPECT_GT(r.map.axis2("move").x, 0.6f);
    EXPECT_GT(r.map.axis2("move").y, 0.6f);
    r.state.set(SourceKind::Key, 26, 0.0f);
    r.state.set(SourceKind::Key, 7, 0.0f);
    // Stick inside the radial deadzone: nothing.
    r.state.set(SourceKind::GamepadAxis, 0, 0.1f);
    r.state.set(SourceKind::GamepadAxis, 1, -0.1f);
    r.step();
    EXPECT_FALSE(r.map.held("move"));
    // Stick pushed up (negative SDL Y): forward.
    r.state.set(SourceKind::GamepadAxis, 0, 0.0f);
    r.state.set(SourceKind::GamepadAxis, 1, -1.0f);
    r.step();
    EXPECT_NEAR(r.map.axis2("move").y, 1.0f, 1e-4f);
    EXPECT_TRUE(r.map.held("move"));
}

TEST(InputMap, AxisHalfAsAButtonAndButtonAsAnAxis) {
    Rig r;
    r.map.defineAction({ "fire", "Fire" });
    kke::ActionDef throttle{ "throttle", "Throttle" };
    throttle.type = kke::ActionType::Axis1D;
    r.map.defineAction(throttle);
    auto trigger = bind("fire", axis(5)); // right trigger past the threshold
    trigger.threshold = 0.6f;
    r.map.addBinding(trigger);
    r.map.addBinding(bind("throttle", pad(0), Trigger::Continuous)); // A = full throttle
    auto back = bind("throttle", pad(1), Trigger::Continuous);
    back.scale = -0.5f;
    r.map.addBinding(back);

    r.state.set(SourceKind::GamepadAxis, 5, 0.5f);
    r.step();
    EXPECT_FALSE(r.map.held("fire"));
    r.state.set(SourceKind::GamepadAxis, 5, 0.7f);
    r.step();
    EXPECT_TRUE(r.map.pressed("fire"));
    r.state.set(SourceKind::GamepadButton, 1, 1.0f);
    r.step();
    EXPECT_FLOAT_EQ(r.map.axis("throttle"), -0.5f);
    r.state.set(SourceKind::GamepadButton, 0, 1.0f);
    r.step();
    EXPECT_FLOAT_EQ(r.map.axis("throttle"), 0.5f);
}

TEST(InputMap, DeadzoneCurveInvertShapeAnalog) {
    Rig r;
    kke::ActionDef a{ "pitch", "Pitch" };
    a.type = kke::ActionType::Axis1D;
    r.map.defineAction(a);
    auto b = bind("pitch", { SourceKind::JoyAxis, 0, 1, 0 }, Trigger::Continuous);
    b.deadzone = 0.1f;
    b.curve = 2.0f;
    b.invert = true;
    r.map.addBinding(b);
    r.state.set(SourceKind::JoyAxis, 1, 0.05f);
    r.step();
    EXPECT_FLOAT_EQ(r.map.axis("pitch"), 0.0f);
    r.state.set(SourceKind::JoyAxis, 1, 0.55f); // (0.55-0.1)/0.9 = 0.5, squared = 0.25, inverted
    r.step();
    EXPECT_NEAR(r.map.axis("pitch"), -0.25f, 1e-5f);
}

TEST(InputMap, IdenticalDevicesAreBoundSeparately) {
    // HOSAS: two identical sticks; the same button index on each does
    // different things because bindings name the device.
    Rig r;
    r.map.defineAction({ "left.fire", "Left fire" });
    r.map.defineAction({ "right.fire", "Right fire" });
    r.map.addBinding(bind("left.fire", { SourceKind::JoyButton, 111, 0, 0 }));
    r.map.addBinding(bind("right.fire", { SourceKind::JoyButton, 222, 0, 0 }));
    r.state.set(SourceKind::JoyButton, 0, 1.0f, 222);
    r.step();
    EXPECT_TRUE(r.map.held("right.fire"));
    EXPECT_FALSE(r.map.held("left.fire"));
}

TEST(InputMap, SplitScreenOnlyHearsItsOwnDevices) {
    Rig r;
    r.map.defineAction({ "jump", "Jump" });
    r.map.addBinding(bind("jump", pad(0))); // "any gamepad"
    r.map.setDevices({ 7 });
    r.state.set(SourceKind::GamepadButton, 0, 1.0f, 8); // another player's pad
    r.step();
    EXPECT_FALSE(r.map.held("jump"));
    r.state.set(SourceKind::GamepadButton, 0, 1.0f, 7);
    r.step();
    EXPECT_TRUE(r.map.held("jump"));
}

TEST(InputMap, DisabledContextIsSilent) {
    Rig r;
    r.map.defineAction({ "ui.accept", "Accept", "Menus", "ui" });
    r.map.addBinding(bind("ui.accept", pad(0)));
    r.map.setContextEnabled("ui", false);
    r.state.set(SourceKind::GamepadButton, 0, 1.0f);
    r.step();
    EXPECT_FALSE(r.map.held("ui.accept"));
    r.map.setContextEnabled("ui", true);
    r.step();
    EXPECT_TRUE(r.map.pressed("ui.accept"));
}

TEST(InputMap, SaveLoadRoundTripAndConflicts) {
    kke::InputMap m;
    m.defineAction({ "jump", "Jump" });
    m.defineAction({ "use", "Use" });
    auto b = bind("jump", key(44), Trigger::Hold);
    b.holdTime = 0.5f;
    b.modifiers = { pad(9) };
    m.addBinding(b);
    m.addBinding(bind("use", { SourceKind::JoyHat, 5, 2, 0 }));
    m.addBinding(bind("jump", { SourceKind::JoyHat, 5, 2, 0 }));
    EXPECT_EQ(m.conflicts(1), std::vector<size_t>{ 2 });
    nlohmann::json j = m.save();
    kke::InputMap n;
    n.defineAction({ "jump", "Jump" });
    n.defineAction({ "use", "Use" });
    EXPECT_EQ(n.load(j), 0);
    ASSERT_EQ(n.bindings().size(), 3u);
    EXPECT_EQ(n.bindings()[0].trigger, Trigger::Hold);
    EXPECT_FLOAT_EQ(n.bindings()[0].holdTime, 0.5f);
    ASSERT_EQ(n.bindings()[0].modifiers.size(), 1u);
    EXPECT_EQ(n.bindings()[0].modifiers[0], pad(9));
    EXPECT_EQ(n.bindings()[1].source.device, 5u);
    // Unknown actions and garbage are dropped, not fatal.
    j["bindings"].push_back({ { "action", "nope" }, { "source", { { "kind", "key" }, { "code", 4 } } } });
    j["bindings"].push_back(42);
    EXPECT_EQ(n.load(j), 2);
    EXPECT_EQ(n.load(nlohmann::json::array()), -1);
}

TEST(InputMap, RestoreDefaultsPerAction) {
    kke::InputMap m;
    m.defineAction({ "jump", "Jump" });
    m.defineAction({ "use", "Use" });
    m.addBinding(bind("jump", key(44)));
    m.addBinding(bind("use", key(8)));
    m.storeDefaults();
    m.clearBindings("jump");
    m.addBinding(bind("jump", key(5)));
    m.binding(0).source = key(9); // "use" rebound
    m.restoreDefaults("jump");
    ASSERT_EQ(m.bindingsFor("jump").size(), 1u);
    EXPECT_EQ(m.bindings()[m.bindingsFor("jump")[0]].source, key(44));
    EXPECT_EQ(m.bindings()[m.bindingsFor("use")[0]].source, key(9)); // untouched
    m.restoreDefaults();
    EXPECT_EQ(m.bindings()[m.bindingsFor("use")[0]].source, key(8));
}

#include "kke/modules/InputModule.h"

TEST(InputModule, LeftHandedMirrorKeepsDirections) {
    kke::InputMap m;
    kke::InputModule::defineCharacterActions(m);
    kke::InputModule::mirrorKeyboard(m);
    FakeState st;
    double t = 0.0;
    auto step = [&] { t += 1.0 / 60.0; m.update(st, t); };
    // K is left of L (the mirrored S): moving left.
    st.set(SourceKind::Key, SDL_SCANCODE_K, 1.0f);
    step();
    EXPECT_LT(m.axis2("move").x, -0.9f);
    st.set(SourceKind::Key, SDL_SCANCODE_K, 0.0f);
    st.set(SourceKind::Key, SDL_SCANCODE_O, 1.0f); // mirrored W: forward
    step();
    EXPECT_GT(m.axis2("move").y, 0.9f);
    st.set(SourceKind::Key, SDL_SCANCODE_O, 0.0f);
    // Right Shift sprints; Left Shift no longer does.
    st.set(SourceKind::Key, SDL_SCANCODE_LSHIFT, 1.0f);
    step();
    EXPECT_FALSE(m.held("sprint"));
    st.set(SourceKind::Key, SDL_SCANCODE_RSHIFT, 1.0f);
    step();
    EXPECT_TRUE(m.held("sprint"));
    // Mirroring twice is the identity.
    EXPECT_EQ(kke::InputModule::mirrorScancode(kke::InputModule::mirrorScancode(SDL_SCANCODE_Q)), SDL_SCANCODE_Q);
    EXPECT_EQ(kke::InputModule::mirrorScancode(SDL_SCANCODE_F1), SDL_SCANCODE_F1);
}
