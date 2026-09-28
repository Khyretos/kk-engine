#include "kke/ButtonPrompts.h"
#include "kke/InputMap.h"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

using kke::ButtonPrompts;
using kke::InputSource;
using kke::PromptStyle;
using kke::SourceKind;

const PromptStyle kPads[] = { PromptStyle::Xbox, PromptStyle::PlayStation, PromptStyle::Switch, PromptStyle::SteamDeck, PromptStyle::SteamController };

bool exists(const std::string& rel) {
    return std::filesystem::exists(std::filesystem::path(KKE_SOURCE_DIR) / "assets/prompts/xelu" / rel);
}

kke::Binding bind(const std::string& action, InputSource s) {
    kke::Binding b;
    b.action = action;
    b.source = s;
    return b;
}

InputSource key(SDL_Scancode sc) { return { SourceKind::Key, 0, static_cast<int32_t>(sc), 0 }; }
InputSource pad(SDL_GamepadButton b) { return { SourceKind::GamepadButton, 0, static_cast<int32_t>(b), 0 }; }
InputSource padAxis(SDL_GamepadAxis a) { return { SourceKind::GamepadAxis, 0, static_cast<int32_t>(a), 0 }; }

} // namespace

TEST(ButtonPrompts, StylesRoundTripThroughNames) {
    for (PromptStyle s : { PromptStyle::Keyboard, PromptStyle::Xbox, PromptStyle::PlayStation, PromptStyle::Switch,
                           PromptStyle::SteamDeck, PromptStyle::SteamController, PromptStyle::Touch }) {
        PromptStyle back{};
        ASSERT_TRUE(kke::promptStyleFromString(kke::toString(s), back)) << kke::toString(s);
        EXPECT_EQ(back, s);
    }
    PromptStyle s{};
    EXPECT_TRUE(kke::promptStyleFromString("PS5", s));
    EXPECT_EQ(s, PromptStyle::PlayStation);
    EXPECT_FALSE(kke::promptStyleFromString("toaster", s));
}

TEST(ButtonPrompts, PadMakesPickTheirOwnGlyphs) {
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_XBOXONE, 0x045e, 0x0b12), PromptStyle::Xbox);
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_PS5, 0x054c, 0x0ce6), PromptStyle::PlayStation);
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_PS4, 0x054c, 0x09cc), PromptStyle::PlayStation);
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO, 0x057e, 0x2009), PromptStyle::Switch);
    // The Deck and the Steam Controller look like plain pads to SDL.
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1205), PromptStyle::SteamDeck);
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_STANDARD, 0x28de, 0x1102), PromptStyle::SteamController);
    // Unknown pads get Xbox glyphs: the layout nearly every PC pad copies.
    EXPECT_EQ(kke::promptStyleForPad(SDL_GAMEPAD_TYPE_STANDARD, 0x1234, 0x5678), PromptStyle::Xbox);
}

TEST(ButtonPrompts, FaceButtonsFollowPositionNotLetter) {
    EXPECT_EQ(ButtonPrompts::padButtonFile(PromptStyle::Xbox, SDL_GAMEPAD_BUTTON_SOUTH), "xbox_series/XboxSeriesX_A.png");
    EXPECT_EQ(ButtonPrompts::padButtonFile(PromptStyle::PlayStation, SDL_GAMEPAD_BUTTON_SOUTH), "ps5/PS5_Cross.png");
    // Nintendo's B sits where Xbox's A does.
    EXPECT_EQ(ButtonPrompts::padButtonFile(PromptStyle::Switch, SDL_GAMEPAD_BUTTON_SOUTH), "switch/Switch_B.png");
    EXPECT_EQ(ButtonPrompts::padButtonFile(PromptStyle::Switch, SDL_GAMEPAD_BUTTON_EAST), "switch/Switch_A.png");
    EXPECT_EQ(ButtonPrompts::padButtonFile(PromptStyle::SteamDeck, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1), "steam_deck/SteamDeck_L4.png");
    EXPECT_EQ(ButtonPrompts::padAxisFile(PromptStyle::PlayStation, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER), "ps5/PS5_R2.png");
    EXPECT_EQ(ButtonPrompts::padAxisFile(PromptStyle::Xbox, SDL_GAMEPAD_AXIS_LEFTY), "xbox_series/XboxSeriesX_Left_Stick.png");
}

TEST(ButtonPrompts, EveryMappedGlyphIsInThePack) {
    for (PromptStyle s : kPads) {
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
            const std::string f = ButtonPrompts::padButtonFile(s, b);
            if (!f.empty()) {
                EXPECT_TRUE(exists(f)) << f;
            }
        }
        for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; ++a) {
            const std::string f = ButtonPrompts::padAxisFile(s, a);
            EXPECT_FALSE(f.empty()) << kke::toString(s) << " axis " << a;
            EXPECT_TRUE(exists(f)) << f;
        }
    }
    // Every face, shoulder, trigger, d-pad, stick and start/back has a glyph on every pad.
    for (PromptStyle s : kPads)
        for (SDL_GamepadButton b : { SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
                                     SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
                                     SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                                     SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
                                     SDL_GAMEPAD_BUTTON_DPAD_RIGHT })
            EXPECT_FALSE(ButtonPrompts::padButtonFile(s, b).empty()) << kke::toString(s) << " button " << b;
    for (int m = 1; m <= 5; ++m) {
        EXPECT_TRUE(exists(ButtonPrompts::mouseFile(m))) << m;
        EXPECT_TRUE(exists(ButtonPrompts::mouseFile(m, true))) << m;
    }
    for (const char* k : { "Space", "Return", "Escape", "Tab", "Left Shift", "Right Shift", "Left Ctrl", "Left Alt", "Left GUI",
                           "Up", "Down", "Left", "Right", "Backspace", "Delete", "PageUp", "Keypad 7", "F1", "F12", "A", "Z", "0", "9",
                           "-", "=", ";", "'", "`", "[", "]", ",", ".", "/", "Keypad Enter", "Keypad +", "Keypad *" }) {
        const ButtonPrompts::Glyph dark = ButtonPrompts::keyGlyph(k), light = ButtonPrompts::keyGlyph(k, true);
        EXPECT_FALSE(dark.file.empty()) << k;
        EXPECT_TRUE(exists(dark.file)) << dark.file;
        EXPECT_TRUE(exists(light.file)) << light.file;
    }
    for (const char* g : { "tap", "double_tap", "hold", "swipe_up", "swipe_left", "zoom_in", "Scroll_Down" }) {
        const ButtonPrompts::Glyph glyph = ButtonPrompts::gestureGlyph(g);
        EXPECT_TRUE(exists(glyph.file)) << g << " -> " << glyph.file;
    }
}

TEST(ButtonPrompts, KeysWithoutAPictureFallBackToTheirName) {
    const ButtonPrompts::Glyph g = ButtonPrompts::keyGlyph("\\");
    EXPECT_TRUE(g.file.empty());
    EXPECT_EQ(g.text, "\\");
    EXPECT_TRUE(ButtonPrompts::keyGlyph("F13").file.empty());
    EXPECT_TRUE(ButtonPrompts::gestureGlyph("wave").file.empty());
}

TEST(ButtonPrompts, ActionShowsTheBindingForTheDeviceInUse) {
    kke::InputMap map;
    map.defineAction({ "jump", "Jump" });
    map.addBinding(bind("jump", key(SDL_SCANCODE_SPACE)));
    map.addBinding(bind("jump", pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    ButtonPrompts prompts;

    auto kb = prompts.actionGlyphs(PromptStyle::Keyboard, map, "jump");
    ASSERT_EQ(kb.size(), 1u);
    EXPECT_EQ(kb[0].file, "keyboard_dark/Space_Key_Dark.png");
    auto ps = prompts.actionGlyphs(PromptStyle::PlayStation, map, "jump");
    ASSERT_EQ(ps.size(), 1u);
    EXPECT_EQ(ps[0].file, "ps5/PS5_Cross.png");
    auto touch = prompts.actionGlyphs(PromptStyle::Touch, map, "jump");
    ASSERT_EQ(touch.size(), 1u);
    EXPECT_EQ(touch[0].file, "others/gestures/Gesture_Tap.png");
    prompts.setTouchGesture("jump", "swipe_up");
    EXPECT_EQ(prompts.actionGlyphs(PromptStyle::Touch, map, "jump")[0].file, "others/gestures/Gesture_Swipe_Up.png");
}

TEST(ButtonPrompts, ChordsAndKeyAxes) {
    kke::InputMap map;
    map.defineAction({ "save", "Save" });
    kke::Binding b = bind("save", key(SDL_SCANCODE_S));
    b.modifiers.push_back(key(SDL_SCANCODE_LCTRL));
    map.addBinding(b);
    ButtonPrompts prompts;
    auto g = prompts.actionGlyphs(PromptStyle::Keyboard, map, "save");
    ASSERT_EQ(g.size(), 2u);
    EXPECT_EQ(g[0].file, "keyboard_dark/Ctrl_Key_Dark.png");
    EXPECT_EQ(g[1].file, "keyboard_dark/S_Key_Dark.png");
    // Nothing bound for pads: no glyphs (the caller shows the label alone).
    EXPECT_TRUE(prompts.actionGlyphs(PromptStyle::Xbox, map, "save").empty());

    kke::InputMap move;
    move.defineAction({ "move", "Move", "", "game", kke::ActionType::Axis2D });
    // Bound in another order than they read: D, S, A, W.
    const struct { SDL_Scancode sc; int component; float scale; } keys[] = {
        { SDL_SCANCODE_D, 0, 1.0f }, { SDL_SCANCODE_S, 1, -1.0f }, { SDL_SCANCODE_A, 0, -1.0f }, { SDL_SCANCODE_W, 1, 1.0f } };
    for (const auto& k : keys) {
        kke::Binding b = bind("move", key(k.sc));
        b.component = k.component;
        b.scale = k.scale;
        move.addBinding(b);
    }
    kke::Binding stick = bind("move", padAxis(SDL_GAMEPAD_AXIS_LEFTX));
    stick.sourceY = padAxis(SDL_GAMEPAD_AXIS_LEFTY);
    move.addBinding(stick);
    auto wasd = prompts.actionGlyphs(PromptStyle::Keyboard, move, "move");
    ASSERT_EQ(wasd.size(), 4u);
    EXPECT_EQ(wasd[0].file, "keyboard_dark/W_Key_Dark.png");
    EXPECT_EQ(wasd[1].file, "keyboard_dark/A_Key_Dark.png");
    EXPECT_EQ(wasd[2].file, "keyboard_dark/S_Key_Dark.png");
    EXPECT_EQ(wasd[3].file, "keyboard_dark/D_Key_Dark.png");
    auto pad = prompts.actionGlyphs(PromptStyle::Switch, move, "move");
    ASSERT_EQ(pad.size(), 1u);
    EXPECT_EQ(pad[0].file, "switch/Switch_Left_Stick.png");
}

TEST(ButtonPrompts, MenuActionsShowTheKeysRmlUiAnswers) {
    kke::InputMap map;
    map.defineAction({ "ui.accept", "Accept", "Menus", "ui" });
    map.addBinding(bind("ui.accept", pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    ButtonPrompts prompts;
    auto g = prompts.actionGlyphs(PromptStyle::Keyboard, map, "ui.accept");
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].file, "keyboard_dark/Enter_Key_Dark.png");
}

TEST(ButtonPrompts, NamedControlsForPromptsThatAreNotActions) {
    ButtonPrompts prompts;
    EXPECT_EQ(prompts.namedGlyphs(PromptStyle::Switch, "a")[0].file, "switch/Switch_B.png"); // "a" = the south button
    EXPECT_EQ(prompts.namedGlyphs(PromptStyle::PlayStation, "pad:start")[0].file, "ps5/PS5_Options.png");
    EXPECT_EQ(prompts.namedGlyphs(PromptStyle::Keyboard, "a")[0].file, "xbox_series/XboxSeriesX_A.png");
    EXPECT_EQ(prompts.namedGlyphs(PromptStyle::Xbox, "rt")[0].file, "xbox_series/XboxSeriesX_RT.png");
    EXPECT_EQ(prompts.namedGlyphs(PromptStyle::Keyboard, "mouse:right")[0].file, "keyboard_dark/Mouse_Right_Key_Dark.png");
    EXPECT_EQ(prompts.namedGlyphs(PromptStyle::Keyboard, "touch:hold")[0].file, "others/gestures/Gesture_Hold.png");
    EXPECT_TRUE(prompts.namedGlyphs(PromptStyle::Xbox, "nonsense").empty());
}

TEST(ButtonPrompts, MarkupEscapesTextAndPointsAtTheRoot) {
    kke::InputMap map;
    map.defineAction({ "jump", "Jump" });
    map.addBinding(bind("jump", pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    ButtonPrompts prompts;
    const std::string r = prompts.rml({ { "xbox_series/XboxSeriesX_A.png", "A" }, { "", "<b>" } }, "Jump & run");
    EXPECT_NE(r.find("<img src=\"/assets/prompts/xelu/xbox_series/XboxSeriesX_A.png\""), std::string::npos) << r;
    EXPECT_EQ(r.find("<b>"), std::string::npos) << r;
    EXPECT_NE(r.find("Jump &amp; run"), std::string::npos) << r;

    const std::string f = prompts.format(PromptStyle::Xbox, map, "{jump} to jump, {{literal}, <i>");
    EXPECT_NE(f.find("XboxSeriesX_A.png"), std::string::npos) << f;
    EXPECT_NE(f.find(" to jump, {literal}, &lt;i&gt;"), std::string::npos) << f;
}

TEST(ButtonPrompts, TouchTurnsButtonActionsIntoTappableButtons) {
    kke::InputMap map;
    map.defineAction({ "aim", "Aim" });
    map.defineAction({ "reach", "Reach" });
    map.defineAction({ "move", "Move", "", "game", kke::ActionType::Axis2D });
    ButtonPrompts prompts;

    // The words after each placeholder are its button's label.
    const std::string f = prompts.format(PromptStyle::Touch, map, "{aim} aim \xC2\xB7 {reach} reach far, {move} move", 1);
    EXPECT_NE(f.find("data-kke-action=\"aim\" data-kke-player=\"1\""), std::string::npos) << f;
    EXPECT_NE(f.find(">aim</span> \xC2\xB7 "), std::string::npos) << f;
    EXPECT_NE(f.find(">reach far</span>, "), std::string::npos) << f;
    // An axis can't be tapped: it keeps its picture.
    EXPECT_EQ(f.find("data-kke-action=\"move\""), std::string::npos) << f;
    // Neighbouring actions get different colours; a lone placeholder is
    // labelled with the action's name.
    EXPECT_NE(ButtonPrompts::touchButton("aim", "a", 0, ButtonPrompts::touchColour(map, "aim")),
              ButtonPrompts::touchButton("aim", "a", 0, ButtonPrompts::touchColour(map, "reach")));
    EXPECT_NE(prompts.format(PromptStyle::Touch, map, "{aim}").find(">Aim</span>"), std::string::npos);
    // A gesture of its own shows the gesture instead.
    prompts.setTouchGesture("aim", "swipe_up");
    EXPECT_NE(prompts.format(PromptStyle::Touch, map, "{aim} aim").find("Gesture_Swipe_Up.png"), std::string::npos);
}

TEST(ButtonPrompts, TouchKeepsSentencesAndMouseClicksAsText) {
    kke::InputMap map;
    map.defineAction({ "rally", "Rally" });
    map.defineAction({ "select", "Select" });
    map.defineAction({ "call", "Call everyone to the middle of the screen" });
    map.addBinding(bind("select", { kke::SourceKind::MouseButton, 0, SDL_BUTTON_LEFT, 0 }));
    ButtonPrompts prompts;

    // A mouse click is a tap on the scene, not a button.
    const std::string click = prompts.format(PromptStyle::Touch, map, "{select} select a soldier");
    EXPECT_EQ(click.find("kke-touch-button"), std::string::npos) << click;
    EXPECT_NE(click.find("select a soldier"), std::string::npos) << click;

    // A sentence after the placeholder stays a sentence beside the button.
    const std::string long_ = prompts.format(PromptStyle::Touch, map, "{rally} call everyone to the crosshair");
    EXPECT_NE(long_.find(">Rally</span>"), std::string::npos) << long_;
    EXPECT_NE(long_.find("call everyone to the crosshair"), std::string::npos) << long_;
    // No short label of its own: the sentence's first word is the button.
    const std::string first = prompts.format(PromptStyle::Touch, map, "{call} call everyone to the crosshair");
    EXPECT_NE(first.find(">call</span> everyone to the crosshair"), std::string::npos) << first;

    // Punctuation ends a label.
    const std::string punct = prompts.format(PromptStyle::Touch, map, "{rally} group 1 (Ctrl: store)");
    EXPECT_NE(punct.find(">group 1</span>"), std::string::npos) << punct;
    EXPECT_NE(punct.find("(Ctrl: store)"), std::string::npos) << punct;
}
