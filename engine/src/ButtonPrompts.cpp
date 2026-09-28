#include "kke/ButtonPrompts.h"

#include "kke/RmlTextSafety.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace kke {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// How well a source suits a style: higher first, 0 = not shown.
int rank(PromptStyle style, SourceKind k) {
    if (style == PromptStyle::Keyboard) {
        switch (k) {
        case SourceKind::Key: return 4;
        case SourceKind::MouseButton: return 3;
        case SourceKind::MouseWheel: return 2;
        case SourceKind::MouseMotion: return 1;
        default: return 0;
        }
    }
    if (k == SourceKind::GamepadButton) return 2;
    if (k == SourceKind::GamepadAxis) return 1;
    return 0;
}

// Folder and file prefix per pad style.
const char* padPrefix(PromptStyle s) {
    switch (s) {
    case PromptStyle::PlayStation: return "ps5/PS5_";
    case PromptStyle::Switch: return "switch/Switch_";
    case PromptStyle::SteamDeck: return "steam_deck/SteamDeck_";
    case PromptStyle::SteamController: return "others/steam/Steam_";
    default: return "xbox_series/XboxSeriesX_";
    }
}

} // namespace

const char* toString(PromptStyle s) {
    switch (s) {
    case PromptStyle::Keyboard: return "keyboard";
    case PromptStyle::Xbox: return "xbox";
    case PromptStyle::PlayStation: return "playstation";
    case PromptStyle::Switch: return "switch";
    case PromptStyle::SteamDeck: return "steamdeck";
    case PromptStyle::SteamController: return "steamcontroller";
    case PromptStyle::Touch: return "touch";
    }
    return "keyboard";
}

bool promptStyleFromString(const std::string& s, PromptStyle& out) {
    const std::string l = lower(s);
    for (PromptStyle p : { PromptStyle::Keyboard, PromptStyle::Xbox, PromptStyle::PlayStation, PromptStyle::Switch,
                           PromptStyle::SteamDeck, PromptStyle::SteamController, PromptStyle::Touch }) {
        if (l == toString(p)) { out = p; return true; }
    }
    if (l == "ps" || l == "ps5" || l == "ps4" || l == "dualsense") { out = PromptStyle::PlayStation; return true; }
    if (l == "deck" || l == "steam_deck") { out = PromptStyle::SteamDeck; return true; }
    if (l == "steam" || l == "steam_controller") { out = PromptStyle::SteamController; return true; }
    if (l == "pad" || l == "gamepad" || l == "controller") { out = PromptStyle::Xbox; return true; }
    if (l == "mouse" || l == "kbm") { out = PromptStyle::Keyboard; return true; }
    return false;
}

bool isPadStyle(PromptStyle s) { return s != PromptStyle::Keyboard && s != PromptStyle::Touch; }

PromptStyle promptStyleForPad(int sdlGamepadType, uint16_t vendor, uint16_t product) {
    if (vendor == 0x28de) { // Valve
        if (product == 0x1205) return PromptStyle::SteamDeck;
        if (product == 0x1102 || product == 0x1142) return PromptStyle::SteamController;
    }
    switch (static_cast<SDL_GamepadType>(sdlGamepadType)) {
    case SDL_GAMEPAD_TYPE_PS3:
    case SDL_GAMEPAD_TYPE_PS4:
    case SDL_GAMEPAD_TYPE_PS5: return PromptStyle::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PromptStyle::Switch;
    default: return PromptStyle::Xbox;
    }
}

std::string ButtonPrompts::padButtonFile(PromptStyle style, int b) {
    // Face buttons by position; each style's own names.
    struct Faces { const char* south; const char* east; const char* west; const char* north; };
    Faces f{ "A", "B", "X", "Y" };
    if (style == PromptStyle::PlayStation) f = { "Cross", "Circle", "Square", "Triangle" };
    if (style == PromptStyle::Switch) f = { "B", "A", "Y", "X" }; // Nintendo's letters sit swapped
    const bool ps = style == PromptStyle::PlayStation;
    const bool sw = style == PromptStyle::Switch;
    const bool deck = style == PromptStyle::SteamDeck;
    const bool steam = style == PromptStyle::SteamController;
    const char* name = nullptr;
    switch (static_cast<SDL_GamepadButton>(b)) {
    case SDL_GAMEPAD_BUTTON_SOUTH: name = f.south; break;
    case SDL_GAMEPAD_BUTTON_EAST: name = f.east; break;
    case SDL_GAMEPAD_BUTTON_WEST: name = f.west; break;
    case SDL_GAMEPAD_BUTTON_NORTH: name = f.north; break;
    case SDL_GAMEPAD_BUTTON_BACK: name = ps ? "Share" : sw ? "Minus" : deck ? "Square" : steam ? "Back" : "View"; break;
    case SDL_GAMEPAD_BUTTON_START: name = ps ? "Options" : sw ? "Plus" : deck ? "Menu" : steam ? "Start" : "Menu"; break;
    case SDL_GAMEPAD_BUTTON_GUIDE: name = sw ? "Home" : deck ? "Steam" : steam ? "System" : nullptr; break;
    case SDL_GAMEPAD_BUTTON_MISC1: name = ps ? "Microphone" : sw ? "Square" : deck ? "Dots" : steam ? nullptr : "Share"; break;
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: name = ps ? "Touch_Pad" : nullptr; break;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: name = steam ? "Stick" : "Left_Stick_Click"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: name = steam ? "Right_Track_Center" : "Right_Stick_Click"; break;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: name = ps || deck ? "L1" : "LB"; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: name = ps || deck ? "R1" : "RB"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: name = steam ? "Left_Track_Up" : "Dpad_Up"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: name = steam ? "Left_Track_Down" : "Dpad_Down"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: name = steam ? "Left_Track_Left" : "Dpad_Left"; break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: name = steam ? "Left_Track_Right" : "Dpad_Right"; break;
    // Back paddles: the Deck's L4/R4 (upper) and L5/R5, the Steam
    // Controller's grips. Xbox Elite / DualSense Edge paddles have no glyph.
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: name = deck ? "R4" : steam ? "Right_Grip" : nullptr; break;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: name = deck ? "L4" : steam ? "Left_Grip" : nullptr; break;
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: name = deck ? "R5" : nullptr; break;
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: name = deck ? "L5" : nullptr; break;
    default: break;
    }
    return name ? std::string(padPrefix(style)) + name + ".png" : std::string();
}

std::string ButtonPrompts::padAxisFile(PromptStyle style, int a) {
    const bool ps = style == PromptStyle::PlayStation;
    const bool deck = style == PromptStyle::SteamDeck;
    const bool steam = style == PromptStyle::SteamController;
    const char* name = nullptr;
    switch (static_cast<SDL_GamepadAxis>(a)) {
    case SDL_GAMEPAD_AXIS_LEFTX:
    case SDL_GAMEPAD_AXIS_LEFTY: name = steam ? "Stick" : "Left_Stick"; break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
    case SDL_GAMEPAD_AXIS_RIGHTY: name = steam ? "Right_Track" : "Right_Stick"; break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: name = ps || deck ? "L2" : "LT"; break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: name = ps || deck ? "R2" : "RT"; break;
    default: break;
    }
    return name ? std::string(padPrefix(style)) + name + ".png" : std::string();
}

std::string ButtonPrompts::mouseFile(int button, bool light) {
    const char* name = button == SDL_BUTTON_LEFT ? "Mouse_Left" : button == SDL_BUTTON_RIGHT ? "Mouse_Right"
                     : button == SDL_BUTTON_MIDDLE ? "Mouse_Middle" : "Mouse_Simple";
    return std::string(light ? "keyboard_light/" : "keyboard_dark/") + name + (light ? "_Key_Light.png" : "_Key_Dark.png");
}

ButtonPrompts::Glyph ButtonPrompts::keyGlyph(const std::string& keyName, bool light) {
    // SDL key names (SDL_GetKeyName) -> Xelu's file stems.
    static const std::map<std::string, std::string> named = {
        { "space", "Space" }, { "return", "Enter" }, { "enter", "Enter" }, { "keypad enter", "Enter_Tall" },
        { "escape", "Esc" }, { "tab", "Tab" }, { "backspace", "Backspace" }, { "capslock", "Caps_Lock" },
        { "left shift", "Shift" }, { "right shift", "Shift_Alt" }, { "left ctrl", "Ctrl" }, { "right ctrl", "Ctrl" },
        { "left alt", "Alt" }, { "right alt", "Alt" }, { "left gui", "Win" }, { "right gui", "Win" },
        { "delete", "Del" }, { "insert", "Insert" }, { "home", "Home" }, { "end", "End" },
        { "pageup", "Page_Up" }, { "pagedown", "Page_Down" }, { "up", "Arrow_Up" }, { "down", "Arrow_Down" },
        { "left", "Arrow_Left" }, { "right", "Arrow_Right" }, { "numlockclear", "Num_Lock" }, { "printscreen", "Print_Screen" },
        { "-", "Minus" }, { "keypad -", "Minus" }, { "=", "Plus" }, { "+", "Plus" }, { "keypad +", "Plus_Tall" },
        { "keypad *", "Asterisk" }, { "*", "Asterisk" }, { "/", "Slash" }, { "keypad /", "Slash" }, { "?", "Question" },
        { ";", "Semicolon" }, { "'", "Quote" }, { "`", "Tilda" }, { "~", "Tilda" }, { "[", "Bracket_Left" },
        { "]", "Bracket_Right" }, { ",", "Mark_Left" }, { "<", "Mark_Left" }, { ".", "Mark_Right" }, { ">", "Mark_Right" },
    };
    std::string l = lower(keyName);
    if (l.rfind("keypad ", 0) == 0 && l.size() == 8 && std::isdigit(static_cast<unsigned char>(l[7]))) l = l.substr(7);
    std::string stem;
    if (auto it = named.find(l); it != named.end()) stem = it->second;
    else if (l.size() == 1 && (std::isalnum(static_cast<unsigned char>(l[0])))) stem = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(l[0]))));
    else if (l.size() >= 2 && l.size() <= 3 && l[0] == 'f' && std::all_of(l.begin() + 1, l.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
        const int n = std::stoi(l.substr(1));
        if (n >= 1 && n <= 12) stem = "F" + std::to_string(n);
    }
    if (stem.empty()) return { {}, keyName };
    return { std::string(light ? "keyboard_light/" : "keyboard_dark/") + stem + (light ? "_Key_Light.png" : "_Key_Dark.png"), keyName };
}

ButtonPrompts::Glyph ButtonPrompts::gestureGlyph(const std::string& gesture) {
    static const char* known[] = {
        "Double_Rotate", "Double_Tap", "Finger_Front", "Finger_Side", "Full_Circle", "Half_Circle", "Hold",
        "Quarter_Circle", "Scroll_Down", "Scroll_Left", "Scroll_Right", "Scroll_Up", "Swipe_Bottom",
        "Swipe_Bottom_Left", "Swipe_Bottom_Right", "Swipe_Left", "Swipe_Right", "Swipe_Top_Left",
        "Swipe_Top_Right", "Swipe_Up", "Tap", "Zoom_In", "Zoom_Out",
    };
    const std::string l = lower(gesture);
    for (const char* k : known)
        if (lower(k) == l) return { std::string("others/gestures/Gesture_") + k + ".png", gesture };
    return { {}, gesture };
}

ButtonPrompts::Glyph ButtonPrompts::glyph(PromptStyle style, const InputSource& s) const {
    const PromptStyle pad = isPadStyle(style) ? style : PromptStyle::Xbox;
    switch (s.kind) {
    case SourceKind::Key: {
        // The layout's name for the physical key (AZERTY: the Q position is A).
        const char* name = nullptr;
        if (SDL_WasInit(SDL_INIT_VIDEO)) name = SDL_GetKeyName(SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(s.code), SDL_KMOD_NONE, false));
        if (!name || !*name) name = SDL_GetScancodeName(static_cast<SDL_Scancode>(s.code));
        return keyGlyph(name ? name : "?", m_lightKeys);
    }
    case SourceKind::MouseButton: return { mouseFile(s.code, m_lightKeys), "Mouse " + std::to_string(s.code) };
    case SourceKind::MouseWheel: return { mouseFile(SDL_BUTTON_MIDDLE, m_lightKeys), "Wheel" };
    case SourceKind::MouseMotion: return { mouseFile(0, m_lightKeys), "Mouse" };
    case SourceKind::GamepadButton: {
        std::string file = padButtonFile(pad, s.code);
        const char* label = SDL_GetGamepadStringForButton(static_cast<SDL_GamepadButton>(s.code));
        return { file, label ? label : "Button" };
    }
    case SourceKind::GamepadAxis: {
        const char* label = SDL_GetGamepadStringForAxis(static_cast<SDL_GamepadAxis>(s.code));
        return { padAxisFile(pad, s.code), label ? label : "Stick" };
    }
    case SourceKind::Gyro: return { pad == PromptStyle::SteamDeck ? "steam_deck/SteamDeck_Gyro.png" : pad == PromptStyle::SteamController ? "others/steam/Steam_Gyro.png" : "", "Gyro" };
    case SourceKind::JoyButton: return { {}, "Button " + std::to_string(s.code + 1) };
    case SourceKind::JoyAxis: return { {}, "Axis " + std::to_string(s.code + 1) };
    case SourceKind::JoyHat: return { {}, "Hat" };
    default: return { {}, "?" };
    }
}

std::vector<ButtonPrompts::Glyph> ButtonPrompts::actionGlyphs(PromptStyle style, const InputMap& map, const std::string& action) const {
    std::vector<Glyph> out;
    if (style == PromptStyle::Touch) {
        auto it = m_touch.find(action);
        out.push_back(gestureGlyph(it == m_touch.end() ? "tap" : it->second));
        return out;
    }
    const std::vector<size_t> idx = map.bindingsFor(action);
    int best = 0;
    for (size_t i : idx) best = std::max(best, rank(style, map.bindings()[i].source.kind));
    if (best == 0) {
        // RmlUi answers the keyboard itself, so ui.* has no key bindings.
        if (style == PromptStyle::Keyboard && action.rfind("ui.", 0) == 0) {
            static const std::map<std::string, std::vector<std::string>> implicit = {
                { "ui.accept", { "Return" } }, { "ui.back", { "Escape" } }, { "ui.up", { "Up" } }, { "ui.down", { "Down" } },
                { "ui.left", { "Left" } }, { "ui.right", { "Right" } }, { "ui.next", { "Tab" } }, { "ui.prev", { "Left Shift", "Tab" } },
            };
            if (auto it = implicit.find(action); it != implicit.end())
                for (const std::string& k : it->second) out.push_back(keyGlyph(k, m_lightKeys));
        }
        return out;
    }
    const ActionDef* def = map.action(action);
    const bool axis = def && def->type != ActionType::Button;
    std::vector<size_t> order = idx;
    if (axis) {
        // Keys on an axis read up, left, down, right (W A S D), whatever
        // order they were bound in.
        auto place = [&](size_t i) {
            const Binding& b = map.bindings()[i];
            return b.component == 1 ? (b.scale >= 0.0f ? 0 : 2) : (b.scale < 0.0f ? 1 : 3);
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return place(a) < place(b); });
    }
    for (size_t i : order) {
        const Binding& b = map.bindings()[i];
        if (rank(style, b.source.kind) != best) continue;
        if (out.empty() || !axis)
            for (const InputSource& m : b.modifiers) out.push_back(glyph(style, m));
        Glyph g = glyph(style, b.source);
        // An axis driven by several keys (W A S D) lists each once; a
        // stick is one picture however many bindings use it.
        if (std::find(out.begin(), out.end(), g) == out.end()) out.push_back(g);
        if (!axis || out.size() >= 4) break;
    }
    return out;
}

int ButtonPrompts::padButtonFromName(const std::string& name) {
    static const std::map<std::string, int> names = {
        { "a", SDL_GAMEPAD_BUTTON_SOUTH }, { "south", SDL_GAMEPAD_BUTTON_SOUTH }, { "cross", SDL_GAMEPAD_BUTTON_SOUTH },
        { "b", SDL_GAMEPAD_BUTTON_EAST }, { "east", SDL_GAMEPAD_BUTTON_EAST }, { "circle", SDL_GAMEPAD_BUTTON_EAST },
        { "x", SDL_GAMEPAD_BUTTON_WEST }, { "west", SDL_GAMEPAD_BUTTON_WEST }, { "square", SDL_GAMEPAD_BUTTON_WEST },
        { "y", SDL_GAMEPAD_BUTTON_NORTH }, { "north", SDL_GAMEPAD_BUTTON_NORTH }, { "triangle", SDL_GAMEPAD_BUTTON_NORTH },
        { "back", SDL_GAMEPAD_BUTTON_BACK }, { "view", SDL_GAMEPAD_BUTTON_BACK }, { "select", SDL_GAMEPAD_BUTTON_BACK },
        { "start", SDL_GAMEPAD_BUTTON_START }, { "menu", SDL_GAMEPAD_BUTTON_START }, { "options", SDL_GAMEPAD_BUTTON_START },
        { "guide", SDL_GAMEPAD_BUTTON_GUIDE }, { "home", SDL_GAMEPAD_BUTTON_GUIDE },
        { "misc", SDL_GAMEPAD_BUTTON_MISC1 }, { "share", SDL_GAMEPAD_BUTTON_MISC1 }, { "touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD },
        { "ls", SDL_GAMEPAD_BUTTON_LEFT_STICK }, { "l3", SDL_GAMEPAD_BUTTON_LEFT_STICK }, { "left_stick_click", SDL_GAMEPAD_BUTTON_LEFT_STICK },
        { "rs", SDL_GAMEPAD_BUTTON_RIGHT_STICK }, { "r3", SDL_GAMEPAD_BUTTON_RIGHT_STICK }, { "right_stick_click", SDL_GAMEPAD_BUTTON_RIGHT_STICK },
        { "lb", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "l1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER }, { "left_shoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER },
        { "rb", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER }, { "r1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER }, { "right_shoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER },
        { "dpad_up", SDL_GAMEPAD_BUTTON_DPAD_UP }, { "dpad_down", SDL_GAMEPAD_BUTTON_DPAD_DOWN },
        { "dpad_left", SDL_GAMEPAD_BUTTON_DPAD_LEFT }, { "dpad_right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT },
        { "r4", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1 }, { "l4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1 },
        { "r5", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2 }, { "l5", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2 },
    };
    auto it = names.find(lower(name));
    return it == names.end() ? -1 : it->second;
}

int ButtonPrompts::padAxisFromName(const std::string& name) {
    static const std::map<std::string, int> names = {
        { "left_stick", SDL_GAMEPAD_AXIS_LEFTX }, { "right_stick", SDL_GAMEPAD_AXIS_RIGHTX },
        { "lt", SDL_GAMEPAD_AXIS_LEFT_TRIGGER }, { "l2", SDL_GAMEPAD_AXIS_LEFT_TRIGGER }, { "left_trigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER },
        { "rt", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER }, { "r2", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER }, { "right_trigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER },
    };
    auto it = names.find(lower(name));
    return it == names.end() ? -1 : it->second;
}

std::vector<ButtonPrompts::Glyph> ButtonPrompts::namedGlyphs(PromptStyle style, const std::string& name) const {
    const std::string l = lower(name);
    const PromptStyle pad = isPadStyle(style) ? style : PromptStyle::Xbox;
    if (l.rfind("key:", 0) == 0) {
        const std::string key = name.substr(4);
        const SDL_Scancode sc = SDL_GetScancodeFromName(key.c_str());
        if (sc != SDL_SCANCODE_UNKNOWN) return { glyph(PromptStyle::Keyboard, { SourceKind::Key, 0, static_cast<int32_t>(sc), 0 }) };
        return { keyGlyph(key, m_lightKeys) };
    }
    if (l.rfind("mouse:", 0) == 0) {
        const std::string b = l.substr(6);
        const int button = b == "left" ? SDL_BUTTON_LEFT : b == "right" ? SDL_BUTTON_RIGHT : b == "middle" ? SDL_BUTTON_MIDDLE : 0;
        return { { mouseFile(button, m_lightKeys), "Mouse" } };
    }
    if (l.rfind("touch:", 0) == 0) return { gestureGlyph(name.substr(6)) };
    const std::string padName = l.rfind("pad:", 0) == 0 ? l.substr(4) : l;
    if (const int b = padButtonFromName(padName); b >= 0) return { glyph(pad, { SourceKind::GamepadButton, 0, b, 0 }) };
    if (const int a = padAxisFromName(padName); a >= 0) return { glyph(pad, { SourceKind::GamepadAxis, 0, a, 0 }) };
    return {};
}

std::string ButtonPrompts::rml(const std::vector<Glyph>& glyphs, const std::string& label) const {
    std::string out = "<span class=\"kke-prompt\" style=\"white-space: nowrap;\">";
    for (const Glyph& g : glyphs) {
        // Sized inline in em so a prompt looks right in any document; a
        // document scales them with `img { font-size: ... }`.
        if (!g.file.empty()) out += "<img src=\"" + m_root + "/" + g.file + "\" style=\"height: 1.6em; width: 1.6em; vertical-align: middle;\"/>";
        else out += "<span class=\"kke-prompt-key\" style=\"border: 1dp #ffffffaa; border-radius: 4dp; padding: 0 0.3em; margin: 0 0.15em;\">" + escapeRmlText(g.text) + "</span>";
    }
    if (!label.empty()) out += "<span class=\"kke-prompt-label\"> " + escapeRmlText(label) + "</span>";
    return out + "</span>";
}

namespace {
// A button's label: at most three words and 22 characters.
bool isShortLabel(const std::string& label) {
    if (label.size() > 22) return false;
    int words = 0;
    bool inWord = false;
    for (char c : label) {
        const bool space = c == ' ';
        if (!space && !inWord) ++words;
        inWord = !space;
    }
    return words <= 3;
}
} // namespace

int ButtonPrompts::touchColour(const InputMap& map, const std::string& action) {
    const std::vector<ActionDef>& all = map.actions();
    for (size_t i = 0; i < all.size(); ++i)
        if (all[i].id == action) return static_cast<int>(i);
    return 0;
}

bool ButtonPrompts::isTouchButton(const InputMap& map, const std::string& action) const {
    const ActionDef* def = map.action(action);
    if (!def || def->type != ActionType::Button) return false;
    // A mouse click means "here, on the world" (select, order to the
    // crosshair): on a touch screen that's a tap on the scene, not a button.
    for (size_t i : map.bindingsFor(action))
        if (map.bindings()[i].source.kind == SourceKind::MouseButton) return false;
    auto it = m_touch.find(action);
    return it == m_touch.end() || lower(it->second) == "tap";
}

std::string ButtonPrompts::touchButton(const std::string& action, const std::string& label, int player, int colour) {
    // Five colours, like a pad's face buttons. The colour is the action's
    // place among the map's actions (touchColour), so one action keeps its
    // colour everywhere and actions defined together, which a hint lists
    // together, differ.
    static const char* fills[] = { "#2f6fd6f0", "#2e9e5bf0", "#d9772bf0", "#8a4fd0f0", "#c9405af0" };
    static const char* edges[] = { "#173a73", "#16532f", "#6f3a12", "#45276b", "#6a1c2b" };
    const size_t k = static_cast<size_t>(colour < 0 ? -colour : colour) % 5;
    std::string out = "<span class=\"kke-touch-button\" data-kke-action=\"" + escapeRmlText(action) + "\" data-kke-player=\"" +
                      std::to_string(player) + "\" style=\"display: inline-block; white-space: nowrap; vertical-align: middle; " +
                      "padding: 0.35em 0.9em; margin: 0.2em 0.25em; border-radius: 1.1em; border-width: 2dp 2dp 4dp 2dp; " +
                      "border-color: #ffffffd0 #ffffffd0 " + edges[k] + " #ffffffd0; background-color: " + fills[k] +
                      "; color: #ffffff; font-weight: bold; pointer-events: auto;\">";
    return out + escapeRmlText(label.empty() ? action : label) + "</span>";
}

std::string ButtonPrompts::format(PromptStyle style, const InputMap& map, const std::string& text, int player) const {
    std::string out, plain;
    auto flush = [&] { out += escapeRmlText(plain); plain.clear(); };
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '{' && i + 1 < text.size() && text[i + 1] == '{') { plain += '{'; ++i; continue; }
        if (text[i] == '{') {
            const size_t end = text.find('}', i);
            if (end != std::string::npos) {
                const std::string name = text.substr(i + 1, end - i - 1);
                const bool action = map.action(name) != nullptr;
                flush();
                if (style == PromptStyle::Touch && action && isTouchButton(map, name)) {
                    // The words after the placeholder are the button's label,
                    // up to punctuation or the next placeholder. A long run of
                    // words is a sentence, not a label: the button then takes
                    // the action's own short label, or else the sentence's
                    // first word, and the rest stays text beside it.
                    static constexpr std::string_view kStops = "{,;|.:()/!?\n";
                    size_t stop = end + 1;
                    while (stop < text.size() && text[stop] == ' ') ++stop;
                    size_t labelEnd = stop;
                    while (labelEnd < text.size() && kStops.find(text[labelEnd]) == std::string_view::npos &&
                           text.compare(labelEnd, 3, " \xC2\xB7") != 0 && text.compare(labelEnd, 2, "\xC2\xB7") != 0 &&
                           text.compare(labelEnd, 2, "  ") != 0 && text.compare(labelEnd, 3, "\xE2\x80\x94") != 0)
                        ++labelEnd;
                    std::string label = text.substr(stop, labelEnd - stop);
                    while (!label.empty() && label.back() == ' ') label.pop_back();
                    if (label.empty() || !isShortLabel(label)) {
                        const ActionDef* def = map.action(name);
                        if (def && !def->label.empty() && isShortLabel(def->label)) {
                            label = def->label;
                            labelEnd = end + 1;
                        } else if (!label.empty()) {
                            label = label.substr(0, label.find(' '));
                            labelEnd = stop + label.size();
                        } else {
                            label = name;
                            labelEnd = end + 1;
                        }
                    }
                    out += touchButton(name, label, player, touchColour(map, name));
                    i = labelEnd - 1;
                    continue;
                }
                std::vector<Glyph> g = action ? actionGlyphs(style, map, name) : namedGlyphs(style, name);
                // An action with nothing on this device shows nothing; an
                // unknown name shows as typed, so the typo is visible.
                if (!g.empty()) out += rml(g);
                else if (!action) out += rml({ { {}, name } });
                i = end;
                continue;
            }
        }
        plain += text[i];
    }
    flush();
    return out;
}

} // namespace kke
