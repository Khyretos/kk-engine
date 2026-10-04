#include "kke/TouchControls.h"

#include "kke/InputMap.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace {

using kke::InputSource;
using kke::SourceKind;
using kke::TouchControl;
using kke::TouchControls;

class NoInput : public kke::InputState {
public:
    float value(const InputSource&, const std::vector<uint32_t>*) const override { return 0.0f; }
};

kke::Binding bind(const std::string& action, InputSource s, kke::Trigger t = kke::Trigger::Press) {
    kke::Binding b;
    b.action = action;
    b.source = s;
    b.trigger = t;
    return b;
}
InputSource pad(int button) { return { SourceKind::GamepadButton, 0, button, 0 }; }
InputSource axis(int a, int8_t half = 0) { return { SourceKind::GamepadAxis, 0, a, half }; }
InputSource key(int code) { return { SourceKind::Key, 0, code, 0 }; }

// A small third-person game: move on the left stick, look.rate on the
// right, jump on A, fire on RT, crouch toggled on B, a debug key, a menu
// action on Start.
kke::InputMap characterMap() {
    kke::InputMap m;
    m.defineAction({ "move", "Move", "Movement", "game", kke::ActionType::Axis2D });
    m.defineAction({ "look.rate", "Look", "Movement", "game", kke::ActionType::Axis2D, false });
    m.defineAction({ "jump", "Jump", "Movement" });
    m.defineAction({ "fire", "Fire", "Combat" });
    m.defineAction({ "crouch", "Crouch", "Movement" });
    m.defineAction({ "panels", "Developer panels", "Debug" });
    m.defineAction({ "menu", "Menu", "Race" });
    m.defineAction({ "throttle", "Throttle", "Driving", "game", kke::ActionType::Axis1D });
    m.defineAction({ "ui.accept", "Accept", "Menus", "ui" });
    kke::Binding mv = bind("move", axis(0), kke::Trigger::Continuous);
    mv.sourceY = axis(1);
    mv.invert = true;
    m.addBinding(mv);
    kke::Binding look = bind("look.rate", axis(2), kke::Trigger::Continuous);
    look.sourceY = axis(3);
    m.addBinding(look);
    m.addBinding(bind("jump", pad(0)));
    m.addBinding(bind("fire", axis(5, 1), kke::Trigger::Continuous));
    m.addBinding(bind("crouch", pad(1), kke::Trigger::Toggle));
    m.addBinding(bind("panels", key(58)));
    m.addBinding(bind("menu", pad(6)));
    m.addBinding(bind("throttle", axis(4, 1), kke::Trigger::Continuous));
    m.addBinding(bind("ui.accept", pad(0)));
    return m;
}

const TouchControl* find(const std::vector<TouchControl>& l, TouchControl::Kind kind, const std::string& action = {}) {
    for (const TouchControl& c : l)
        if (c.kind == kind && (action.empty() || c.action == action)) return &c;
    return nullptr;
}

int indexOf(const TouchControls& t, TouchControl::Kind kind, const std::string& action = {}) {
    for (size_t i = 0; i < t.layout().size(); ++i)
        if (t.layout()[i].kind == kind && (action.empty() || t.layout()[i].action == action)) return static_cast<int>(i);
    return -1;
}

struct Rig {
    kke::InputMap map = characterMap();
    TouchControls touch;
    NoInput none;
    double t = 0.0;
    Rig() {
        touch.setDefaults(TouchControls::autoLayout(map));
        touch.setScreen({ 1920.0f, 1080.0f }, { 0.0f, 0.0f, 1920.0f, 1080.0f });
    }
    void step(float dt = 1.0f / 60.0f) {
        touch.apply(map, dt);
        t += dt;
        map.update(none, t);
    }
    glm::vec2 at(TouchControl::Kind kind, const std::string& action = {}) const {
        return touch.centre(touch.layout()[static_cast<size_t>(indexOf(touch, kind, action))]);
    }
};

} // namespace

TEST(TouchControls, GuessesALayoutFromWhatAControllerPlayerPresses) {
    const kke::InputMap m = characterMap();
    const std::vector<TouchControl> l = TouchControls::autoLayout(m);
    ASSERT_NE(find(l, TouchControl::Kind::Stick), nullptr);
    EXPECT_EQ(find(l, TouchControl::Kind::Stick)->stick, 0);
    ASSERT_NE(find(l, TouchControl::Kind::Look), nullptr);
    EXPECT_EQ(find(l, TouchControl::Kind::Look)->action, "look.rate");
    EXPECT_NE(find(l, TouchControl::Kind::Pause), nullptr);
    // A first, then B, then the triggers; never debug keys, Start or menu actions.
    std::vector<std::string> buttons;
    for (const TouchControl& c : l)
        if (c.kind == TouchControl::Kind::Button) buttons.push_back(c.action);
    EXPECT_EQ(buttons, (std::vector<std::string>{ "jump", "crouch", "fire", "throttle" }));
    EXPECT_TRUE(find(l, TouchControl::Kind::Button, "crouch")->latch); // toggled on a pad: a tap latches
    EXPECT_FALSE(find(l, TouchControl::Kind::Button, "jump")->latch);
    // The game's own list wins; a right stick instead of the look drag.
    kke::TouchLayoutOptions o;
    o.buttons = { "fire" };
    o.look = kke::TouchLayoutOptions::Look::Stick;
    const std::vector<TouchControl> l2 = TouchControls::autoLayout(m, o);
    EXPECT_EQ(find(l2, TouchControl::Kind::Look), nullptr);
    int sticks = 0, buttons2 = 0;
    for (const TouchControl& c : l2) {
        sticks += c.kind == TouchControl::Kind::Stick;
        buttons2 += c.kind == TouchControl::Kind::Button;
    }
    EXPECT_EQ(sticks, 2);
    EXPECT_EQ(buttons2, 1);
}

TEST(TouchControls, ButtonActionsSkipMenusAndSticks) {
    const std::vector<std::string> a = TouchControls::buttonActions(characterMap());
    EXPECT_EQ(a, (std::vector<std::string>{ "jump", "fire", "crouch", "menu", "throttle" }));
}

TEST(TouchControls, ControlsStayWholeOnAnyScreenShape) {
    Rig r;
    for (glm::vec2 size : { glm::vec2(1920, 1080), glm::vec2(1080, 2400), glm::vec2(800, 800) }) {
        r.touch.setScreen(size, { 0.0f, 0.0f, size.x, size.y });
        for (const TouchControl& c : r.touch.layout()) {
            if (c.kind == TouchControl::Kind::Look) continue;
            const glm::vec2 p = r.touch.centre(c);
            const float rad = r.touch.radius(c);
            EXPECT_GE(p.x - rad, -0.01f);
            EXPECT_GE(p.y - rad, -0.01f);
            EXPECT_LE(p.x + rad, size.x + 0.01f);
            EXPECT_LE(p.y + rad, size.y + 0.01f);
        }
    }
    // The safe area moves them in from a notch.
    r.touch.setScreen({ 1920, 1080 }, { 100.0f, 0.0f, 1720.0f, 1080.0f });
    EXPECT_GE(r.at(TouchControl::Kind::Stick).x - r.touch.radius(r.touch.layout()[0]), 99.9f);
}

TEST(TouchControls, TheStickPushesTheLeftStick) {
    Rig r;
    const glm::vec2 c = r.at(TouchControl::Kind::Stick);
    const float rad = r.touch.radius(r.touch.layout()[static_cast<size_t>(indexOf(r.touch, TouchControl::Kind::Stick))]);
    EXPECT_TRUE(r.touch.fingerDown(7, c));
    EXPECT_TRUE(r.touch.fingerMove(7, c + glm::vec2(0.0f, -2.0f * rad))); // up, past the rim
    const glm::vec2 v = r.touch.stick(0);
    EXPECT_NEAR(v.x, 0.0f, 1e-5f);
    EXPECT_NEAR(v.y, -1.0f, 1e-5f); // a controller's convention: up is -y
    EXPECT_FLOAT_EQ(r.touch.sourceValue(axis(1)), -1.0f);
    EXPECT_FLOAT_EQ(r.touch.sourceValue({ SourceKind::GamepadAxis, 3, 1, 0 }), 0.0f); // one real pad's own axis
    EXPECT_FLOAT_EQ(r.touch.sourceValue(axis(3)), 0.0f);
    r.touch.fingerUp(7);
    EXPECT_EQ(r.touch.stick(0), glm::vec2(0.0f));
}

TEST(TouchControls, ButtonsHoldActionsAndNeverLoseAQuickTap) {
    Rig r;
    const glm::vec2 jump = r.at(TouchControl::Kind::Button, "jump");
    EXPECT_TRUE(r.touch.fingerDown(1, jump));
    r.step();
    EXPECT_TRUE(r.map.pressed("jump"));
    r.step();
    EXPECT_TRUE(r.map.held("jump"));
    r.touch.fingerUp(1);
    r.step();
    EXPECT_TRUE(r.map.released("jump"));
    // Down and up before the next frame.
    r.touch.fingerDown(2, jump);
    r.touch.fingerUp(2);
    r.step();
    EXPECT_TRUE(r.map.pressed("jump"));
    // An axis action is held at 1.
    r.touch.fingerDown(3, r.at(TouchControl::Kind::Button, "throttle"));
    r.step();
    EXPECT_FLOAT_EQ(r.map.axis("throttle"), 1.0f);
    r.touch.fingerUp(3);
    r.step();
    EXPECT_FLOAT_EQ(r.map.axis("throttle"), 0.0f);
}

TEST(TouchControls, LatchedButtonsToggle) {
    Rig r;
    const glm::vec2 crouch = r.at(TouchControl::Kind::Button, "crouch");
    r.touch.fingerDown(1, crouch);
    r.touch.fingerUp(1);
    r.step();
    r.step();
    EXPECT_TRUE(r.map.held("crouch"));
    r.touch.fingerDown(1, crouch);
    r.touch.fingerUp(1);
    r.step();
    EXPECT_FALSE(r.map.held("crouch"));
}

TEST(TouchControls, TheLookDragTurnsAsFastAsTheFinger) {
    Rig r;
    const glm::vec2 free(1100.0f, 300.0f); // no control here
    EXPECT_EQ(r.touch.controlAt(free), -1);
    EXPECT_TRUE(r.touch.wouldTake(free));
    EXPECT_TRUE(r.touch.fingerDown(4, free));
    r.touch.fingerMove(4, free + glm::vec2(54.0f, -27.0f)); // right and up
    r.step(0.05f);
    const glm::vec2 look = r.map.axis2("look.rate");
    EXPECT_NEAR(look.x, 54.0f / (1080.0f * 0.05f), 1e-4f);
    EXPECT_NEAR(look.y, 27.0f / (1080.0f * 0.05f), 1e-4f); // drag up looks up
    r.step(0.05f); // the finger stays still: no turning
    EXPECT_EQ(r.map.axis2("look.rate"), glm::vec2(0.0f));
    r.touch.fingerUp(4);
    // Without a look drag, free space isn't the controls'.
    kke::TouchLayoutOptions o;
    o.look = kke::TouchLayoutOptions::Look::None;
    r.touch.setDefaults(TouchControls::autoLayout(r.map, o));
    EXPECT_FALSE(r.touch.wouldTake(free));
    EXPECT_FALSE(r.touch.fingerDown(5, free));
}

TEST(TouchControls, PauseButtonAsksOnce) {
    Rig r;
    EXPECT_TRUE(r.touch.fingerDown(1, r.at(TouchControl::Kind::Pause)));
    EXPECT_TRUE(r.touch.takePause());
    EXPECT_FALSE(r.touch.takePause());
}

TEST(TouchControls, EditingMovesResizesAndRebinds) {
    Rig r;
    r.touch.setEditing(true);
    const int jump = indexOf(r.touch, TouchControl::Kind::Button, "jump");
    const glm::vec2 from = r.at(TouchControl::Kind::Button, "jump");
    EXPECT_TRUE(r.touch.fingerDown(1, from));
    EXPECT_EQ(r.touch.selected(), jump);
    r.touch.fingerMove(1, { 300.0f, 200.0f }); // to the top left
    r.touch.fingerUp(1);
    r.step();
    EXPECT_FALSE(r.map.held("jump")); // editing presses nothing
    const TouchControl& moved = r.touch.layout()[static_cast<size_t>(jump)];
    EXPECT_EQ(moved.anchor, glm::vec2(0.0f, 0.0f));
    EXPECT_NEAR(r.touch.centre(moved).x, 300.0f, 1e-3f);
    EXPECT_NEAR(r.touch.centre(moved).y, 200.0f, 1e-3f);
    // Empty space while editing isn't taken (the edit bar is there).
    EXPECT_FALSE(r.touch.fingerDown(2, { 960.0f, 540.0f }));
    const float before = moved.size;
    r.touch.resize(jump, 1.5f);
    EXPECT_NEAR(r.touch.layout()[static_cast<size_t>(jump)].size, before * 1.5f, 1e-5f);
    r.touch.resize(jump, 100.0f);
    EXPECT_LE(r.touch.layout()[static_cast<size_t>(jump)].size, 0.42f + 1e-5f);
    r.touch.setAction(jump, "fire");
    r.touch.setEditing(false);
    r.touch.fingerDown(3, { 300.0f, 200.0f });
    r.step();
    EXPECT_TRUE(r.map.held("fire"));
    EXPECT_FALSE(r.map.held("jump"));
    // Reset brings the game's layout back.
    r.touch.resetToDefaults();
    EXPECT_EQ(r.touch.layout(), r.touch.defaults());
}

TEST(TouchControls, SavesOnlyWhatThePlayerChanged) {
    Rig r;
    nlohmann::json j = r.touch.save();
    EXPECT_FALSE(j.contains("controls")); // the game's defaults may still change
    const int jump = indexOf(r.touch, TouchControl::Kind::Button, "jump");
    r.touch.resize(jump, 1.25f);
    r.touch.opacity = 0.3f;
    j = r.touch.save();
    ASSERT_TRUE(j.contains("controls"));
    // A renamed action is dropped on load, the rest comes back.
    j["controls"].push_back({ { "kind", "button" }, { "action", "gone" } });
    TouchControls other;
    other.setDefaults(TouchControls::autoLayout(r.map));
    ASSERT_TRUE(other.load(j, r.map));
    EXPECT_EQ(other.layout(), r.touch.layout());
    EXPECT_FLOAT_EQ(other.opacity, 0.3f);
    EXPECT_FALSE(other.load(nlohmann::json::array(), r.map));
}
