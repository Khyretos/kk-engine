#pragma once

#include "kke/InputMap.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace kke {

// Button prompts: the picture of the button to press, drawn with Xelu's
// Free Controller & Key Prompts (CC0, assets/prompts/xelu/). A prompt
// follows the device the player is actually using: an Xbox pad shows A,
// a DualSense shows Cross, a Switch pad shows B in the same place, the
// keyboard shows Space, a touch screen shows a tap. InputModule tracks
// the style per player (promptStyle()); this class turns an action, or a
// raw button, into image files and RmlUi markup.
//
// Pure logic apart from asking SDL for the keyboard layout's key names,
// so the mappings are unit-tested (tests/test_button_prompts.cpp).
// See docs/INPUT.md "Button prompts".
enum class PromptStyle : uint8_t {
    Keyboard,        // keyboard and mouse
    Xbox,            // Xbox Series / One / 360, and any pad we can't name
    PlayStation,     // DualSense, DualShock 4 / 3
    Switch,          // Switch Pro, Joy-Cons (face buttons swapped vs Xbox)
    SteamDeck,
    SteamController,
    Touch,
};

const char* toString(PromptStyle s);                 // "keyboard", "xbox", "playstation", ...
bool promptStyleFromString(const std::string& s, PromptStyle& out);
bool isPadStyle(PromptStyle s);

// SDL_GamepadType (as an int, so this header needs no SDL) plus the USB
// ids: the Steam Deck and Steam Controller report as plain pads to SDL.
PromptStyle promptStyleForPad(int sdlGamepadType, uint16_t vendor, uint16_t product);

class ButtonPrompts {
public:
    // One picture in a prompt. `file` is relative to the prompt root
    // ("xbox_series/XboxSeriesX_A.png"); empty when Xelu has no picture
    // for it, and then `text` is drawn as a key cap instead.
    struct Glyph {
        std::string file;
        std::string text;
        bool operator==(const Glyph& o) const { return file == o.file && text == o.text; }
    };

    // Where the glyphs are, as RmlUi sees it. The default is the copy next
    // to every game's executable (engine/CMakeLists.txt copies it there);
    // "/..." is relative to the working directory in RmlUi, which the
    // engine sets to the executable's folder.
    void setRoot(std::string root) { m_root = std::move(root); }
    const std::string& root() const { return m_root; }
    // Keyboard keys: dark caps (default) or light ones.
    void setLightKeys(bool light) { m_lightKeys = light; }
    // What a touch player does for an action: a gesture name ("tap",
    // "hold", "swipe_up", "double_tap", "zoom_in", ... = Xelu's
    // Gesture_*.png). Actions without one show a tap: on a touch screen an
    // action is an on-screen button.
    void setTouchGesture(const std::string& action, const std::string& gesture) { m_touch[action] = gesture; }

    // One source (a binding's key, button, stick...) in a style. Keyboard
    // keys are named by the current layout (AZERTY shows A where QWERTY
    // shows Q) when SDL is running.
    Glyph glyph(PromptStyle style, const InputSource& source) const;
    // A key by its SDL key name ("Space", "Left Shift", "A", "F5").
    static Glyph keyGlyph(const std::string& sdlKeyName, bool light = false);
    // A touch gesture by name ("tap", "swipe_up"): empty file if unknown.
    static Glyph gestureGlyph(const std::string& gesture);

    // The glyphs that show `action` for this style, from the map's
    // bindings: modifiers first (Ctrl + E), then the source. An axis the
    // keyboard drives with several keys lists them (W A S D). Empty when
    // nothing of this style is bound. `ui.*` actions without keyboard
    // bindings show the keys RmlUi itself answers to (Enter, Esc, arrows).
    std::vector<Glyph> actionGlyphs(PromptStyle style, const InputMap& map, const std::string& action) const;

    // A raw control by name, for prompts that aren't actions ("press A to
    // join"): pad buttons by position in Xbox terms or SDL's ("a", "b",
    // "x", "y", "south", "east", "lb", "rt", "start", "back", "dpad_up",
    // "left_stick", "ls" (click), ...), "key:Space", "mouse:left",
    // "touch:tap". Shown in `style` (a pad button in the keyboard style is
    // shown as an Xbox glyph: the player asked for a pad button).
    std::vector<Glyph> namedGlyphs(PromptStyle style, const std::string& name) const;

    // RmlUi markup: <span class="kke-prompt"><img src="..."/>...<span
    // class="kke-prompt-label">label</span></span>. Label text is escaped.
    // Images are 1.6em square, sized inline (RmlUi has no !important), so
    // a document scales them through their font size without touching the
    // text: `#hint img { font-size: 15dp; }` makes them 24dp.
    // Glyphs without a picture are key caps (.kke-prompt-key).
    std::string rml(const std::vector<Glyph>& glyphs, const std::string& label = {}) const;
    // Text with {action} or {name} placeholders turned into prompts:
    // "{jump} jump  ·  hold {sprint} to run". "{{" is a literal brace. An
    // action with nothing bound on this device becomes nothing; a name
    // that is neither an action nor a control shows as a key cap. Anything
    // else is escaped, so the text may come from a player.
    std::string format(PromptStyle style, const InputMap& map, const std::string& text) const;

    // Pure mappings, public for the tests.
    static std::string padButtonFile(PromptStyle style, int sdlGamepadButton);
    static std::string padAxisFile(PromptStyle style, int sdlGamepadAxis);
    static std::string mouseFile(int sdlMouseButton, bool light = false);
    static int padButtonFromName(const std::string& name); // SDL_GamepadButton or -1
    static int padAxisFromName(const std::string& name);   // SDL_GamepadAxis or -1

private:
    std::string m_root = "/assets/prompts/xelu";
    bool m_lightKeys = false;
    std::map<std::string, std::string> m_touch;
};

} // namespace kke
