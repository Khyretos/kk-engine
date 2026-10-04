#include "kke/modules/LobbyModule.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
#include "kke/modules/NetModule.h"
#endif

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <typeindex>

namespace kke {

namespace {

enum Bits : unsigned { kUp = 1, kDown = 2, kLeft = 4, kRight = 8, kConfirm = 16, kBack = 32, kStart = 64 };
constexpr unsigned kDirections = kUp | kDown | kLeft | kRight;

std::string hex(const glm::vec3& c) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

// The menu: the title at the top, four player cards along the bottom,
// toasts in the top right (those show in the game too). Driven by
// controllers and the keyboard; a tap or a click on a row or a key works
// too (onEvent: the document itself takes no pointer events).
const char* kLobbyRml = R"(
<rml>
<head>
    <title>Lobby</title>
    <style>
        body { font-family: Noto Sans; color: #ffffff; pointer-events: none; width: 100%; height: 100%; }
        div { display: block; }
        #shade { position: absolute; left: 0; right: 0; bottom: 0; height: 58%;
                 decorator: vertical-gradient(#05070f00 #05070fe0); }
        #title { position: absolute; top: 5%; left: 4%; right: 4%; text-align: center; }
        #title .big { font-size: 54dp; font-weight: bold; letter-spacing: 3dp; color: #ffffff;
                      font-effect: shadow(0 3dp #000000aa); }
        #title .sub { font-size: 17dp; color: #eef1f8; margin-top: 4dp; font-effect: outline(2dp #000000b0); }
        #title .sub img { font-size: 15dp; }
        #cards { position: absolute; left: 3%; right: 3%; bottom: 5%; height: 44%; }
        .card { position: absolute; bottom: 0; width: 22.5%; padding: 12dp 14dp 10dp 14dp; border-radius: 12dp;
                background-color: #0b0f1ce6; border-top: 4dp #56a8ff; }
        .card.empty { background-color: #0b0f1c80; border-top-color: #3a4260; height: 76dp; }
        .card .who { font-size: 12dp; letter-spacing: 2dp; color: #aab3cc; }
        .card .name { font-size: 24dp; font-weight: bold; color: #ffffff; }
        .card .device { font-size: 12dp; color: #8b95b0; margin-bottom: 6dp; }
        .card .prompt { font-size: 18dp; color: #ffcf5c; margin-top: 10dp; text-align: center; }
        .card .unplugged { font-size: 13dp; color: #ff6b5c; }
        .row { display: block; padding: 3dp 8dp; margin-top: 2dp; border-radius: 6dp; font-size: 14dp; color: #cfd6e6; }
        .row.focused { background-color: #2a3a66; color: #ffffff; }
        .row .label { display: inline-block; width: 46%; }
        .row .value { display: inline-block; width: 54%; text-align: right; }
        .row .arrow { color: #56a8ff; }
        .row .swatch { display: inline-block; width: 12dp; height: 12dp; border-radius: 6dp; margin-right: 6dp; vertical-align: -1dp; }
        .row.section { margin-top: 8dp; }
        .row.start { margin-top: 8dp; text-align: center; font-weight: bold; background-color: #1f3f2a; color: #b8f5c9; }
        .row.start.focused { background-color: #2f8a4c; color: #ffffff; }
        #hint { position: absolute; left: 0; bottom: 1.2%; width: 100%; text-align: center; font-size: 13dp; color: #aab3cc; }
        #hint img, .toast img { font-size: 14dp; } /* prompt glyphs are 1.6em */
        .prompt img { font-size: 16dp; }
        .row .value.empty { color: #7d869e; }
        #typing { position: absolute; left: 22%; right: 22%; top: 20%; padding: 16dp 20dp; border-radius: 12dp;
                  background-color: #0b0f1cf2; border-top: 4dp #56a8ff; text-align: center; }
        #typing .label { font-size: 14dp; letter-spacing: 2dp; color: #aab3cc; }
        #typing .text { font-size: 26dp; color: #ffffff; margin: 8dp 0 12dp 0; padding: 6dp; border-radius: 6dp;
                        background-color: #1a2138; min-height: 34dp; }
        #typing .keys { display: block; margin-top: 4dp; }
        #typing .key { display: inline-block; width: 38dp; padding: 6dp 0; margin: 2dp; border-radius: 6dp;
                       background-color: #1f2740; font-size: 17dp; color: #cfd6e6; }
        #typing .key.wide { width: 120dp; }
        #typing .key.focused { background-color: #56a8ff; color: #0b0f1c; font-weight: bold; }
        #typing .hint { font-size: 13dp; color: #aab3cc; margin-top: 10dp; }
        #typing .hint img { font-size: 14dp; }
        #online { position: absolute; right: 3%; top: 21%; width: 30%; }
        #online .head { font-size: 12dp; letter-spacing: 2dp; color: #eef1f8; margin-bottom: 4dp; font-effect: outline(2dp #000000b0); }
        .player { display: block; margin-bottom: 6dp; padding: 6dp 12dp; border-radius: 8dp; background-color: #0b0f1cd0;
                  border-left: 4dp #56a8ff; }
        .player .name { font-size: 17dp; font-weight: bold; color: #ffffff; }
        .player .tag { font-size: 12dp; color: #ffcf5c; margin-left: 6dp; }
        .player .look { font-size: 13dp; color: #cfd6e6; }
        #toasts { position: absolute; top: 18dp; right: 18dp; width: 360dp; }
        .toast { display: block; margin-bottom: 8dp; padding: 10dp 14dp; border-radius: 8dp; background-color: #0b0f1ce6;
                 border-left: 4dp #ffcf5c; font-size: 15dp; color: #ffffff; }
    </style>
</head>
<body data-model="kke_lobby">
    <div data-if="open">
        <div id="shade"></div>
        <div id="title">
            <div class="big">{{title}}</div>
            <div class="sub" data-rml="subtitle"></div>
        </div>
        <div id="cards">
            <div data-for="seat, s : seats" class="card" data-class-empty="!seat.joined" data-attr-data-seat="s"
                 data-style-left="(s * 25.8) + '%'" data-style-border-top-color="seat.accent">
                <div class="who">PLAYER {{s + 1}}</div>
                <div data-if="seat.joined">
                    <div class="name">{{seat.name}}</div>
                    <div class="device">{{seat.device}}</div>
                    <div class="unplugged" data-if="seat.unplugged">controller unplugged</div>
                    <div class="prompt" data-if="seat.waiting" data-rml="seat.prompt"></div>
                    <div data-for="row, r : seat.rows" class="row" data-class-focused="row.focused" data-class-start="row.start"
                         data-class-section="row.action" data-attr-data-seat="s" data-attr-data-row="r">
                        <span data-if="row.start">{{row.label}}</span>
                        <span data-if="!row.start" class="label">{{row.label}}</span><span data-if="!row.start" class="value"><span data-if="row.has_swatch" class="swatch" data-style-background-color="row.swatch"></span><span class="arrow" data-if="row.arrows">&lt; </span><span data-class-empty="row.empty">{{row.value}}</span><span class="arrow" data-if="row.arrows"> &gt;</span></span>
                    </div>
                </div>
                <div class="prompt" data-if="!seat.joined" data-rml="seat.prompt"></div>
            </div>
        </div>
        <div id="online" data-if="online.size > 0">
            <div class="head">ONLINE IN THIS GAME</div>
            <div data-for="p : online" class="player" data-style-border-left-color="p.accent">
                <span class="name">{{p.name}}</span><span class="tag">{{p.tag}}</span>
                <div class="look">{{p.look}}</div>
            </div>
        </div>
        <div id="hint" data-rml="hint"></div>
        <div id="typing" data-if="typing">
            <div class="label">{{typing_label}}</div>
            <div class="text">{{typing_text}}_</div>
            <div class="keys" data-for="r, kr : keys">
                <span data-for="k, kc : r.keys" class="key" data-class-focused="k.focused" data-class-wide="k.wide"
                      data-attr-data-key-row="kr" data-attr-data-key-col="kc">{{k.label}}</span>
            </div>
            <div class="hint" data-rml="typing_hint"></div>
        </div>
    </div>
    <div id="toasts">
        <div class="toast" data-for="t : toasts" data-rml="t"></div>
    </div>
</body>
</rml>
)";

} // namespace

LobbyModule::LobbyModule(std::string path) : m_path(std::move(path)) {}
LobbyModule::~LobbyModule() = default;

std::vector<ModuleDependency> LobbyModule::dependencies() const {
    return { { std::type_index(typeid(InputModule)), true, "controllers and the keyboard join and pick with it" },
             { std::type_index(typeid(UiModule)), false, "shows the menu and the toasts" } };
}

void LobbyModule::init(Application& app) {
    m_app = &app;
    m_input = app.getModule<InputModule>();
    // The lobby has the screen: no touch sticks over the cards.
    if (m_input) m_input->addTouchHider([this] { return m_lobby.isOpen() && !m_suspended; });
    buildUi();
}

bool LobbyModule::tapAt(float x, float y) {
    const UiModule* ui = m_app ? m_app->getModule<UiModule>() : nullptr;
    if (!ui || !m_doc) return false;
    const glm::vec2 p = ui->toContext(glm::vec2(x, y));
    auto inside = [&](Rml::Element* e) {
        const Rml::Vector2f at = e->GetAbsoluteOffset(Rml::BoxArea::Border), size = e->GetBox().GetSize(Rml::BoxArea::Border);
        return p.x >= at.x && p.y >= at.y && p.x < at.x + size.x && p.y < at.y + size.y ? (p.x - at.x) / std::max(1.0f, size.x) : -1.0f;
    };
    Rml::ElementList list;
    if (m_lobby.editing()) {
        m_doc->GetElementsByClassName(list, "key");
        for (Rml::Element* e : list)
            if (inside(e) >= 0.0f) {
                m_lobby.tapKey(e->GetAttribute<int>("data-key-row", -1), e->GetAttribute<int>("data-key-col", -1));
                return true;
            }
        return false;
    }
    m_doc->GetElementsByClassName(list, "row");
    for (Rml::Element* e : list) {
        const float fx = inside(e);
        if (fx < 0.0f) continue;
        // "Colour   < Red >": the left half of the value steps back, the rest on.
        m_lobby.tapRow(e->GetAttribute<int>("data-seat", -1), e->GetAttribute<int>("data-row", -1), fx > 0.46f && fx < 0.75f ? -1 : 1);
        return true;
    }
    // Player 1's card before their first press: the tap is it (the screen
    // and the keyboard are theirs, as Enter would make them).
    list.clear();
    m_doc->GetElementsByClassName(list, "card");
    for (Rml::Element* e : list)
        if (inside(e) >= 0.0f && e->GetAttribute<int>("data-seat", -1) == 0 && m_lobby.seat(0).device == Lobby::Device::Any) {
            Lobby::Press p;
            p.device = Lobby::Device::KeyboardMouse;
            p.confirm = true;
            m_lobby.handle(p, m_pads);
            return true;
        }
    return false;
}

void LobbyModule::setTitle(std::string title, std::string subtitle) {
    m_title = std::move(title);
    m_subtitle = std::move(subtitle);
    m_shownRevision = 0;
}

void LobbyModule::open() {
    m_lobby.setOpen(true);
}

void LobbyModule::close() {
    m_lobby.setOpen(false);
}

void LobbyModule::setSuspended(bool suspended) {
    if (suspended == m_suspended) return;
    m_suspended = suspended;
    m_tapped.clear();
    if (!suspended) m_deafFrames = 2;
    if (m_doc) m_doc->SetProperty("visibility", suspended ? "hidden" : "visible");
}

void LobbyModule::applyInput() {
    m_applied = m_lobby.joinedSeats();
    m_input->setPlayers(static_cast<int>(m_applied.size()));
    assignDevices();
}

int LobbyModule::playerOf(int seat) const {
    const auto it = std::find(m_applied.begin(), m_applied.end(), seat);
    return it == m_applied.end() ? -1 : static_cast<int>(it - m_applied.begin());
}

void LobbyModule::assignDevices() {
    for (size_t p = 0; p < m_applied.size() && static_cast<int>(p) < m_input->players(); ++p) {
        std::vector<uint32_t> refs = m_lobby.devicesFor(m_applied[p], m_pads, m_keyboardMice);
        m_input->assignDevices(static_cast<int>(p), std::move(refs));
    }
}

bool LobbyModule::load() {
    nlohmann::json j;
    std::string error;
    bool exists = false;
    if (!datafile::loadPath(m_path, j, &error, &exists)) {
        if (exists) log::get(name())->warn("could not read {} (using defaults): {}", m_path, error);
        return false;
    }
    m_lobby.load(j);
    return true;
}

bool LobbyModule::save() const {
    std::string error;
    if (datafile::saveFile(datafile::saveTarget(m_path), m_lobby.save(), &error)) return true;
    log::get(name())->warn("could not save {}: {}", m_path, error);
    return false;
}

Lobby::Press LobbyModule::pressFrom(unsigned now, Held& held, unsigned tapped, float dt) const {
    // A tap shorter than a frame is only in the events.
    unsigned edges = (now & ~held.bits) | tapped;
    // Directions repeat while held: after 0.4 s, every 0.14 s.
    if ((now & kDirections) && (now & kDirections) == (held.bits & kDirections)) {
        held.repeat -= dt;
        if (held.repeat <= 0.0f) {
            edges |= now & kDirections;
            held.repeat = 0.14f;
        }
    } else if (edges & kDirections) {
        held.repeat = 0.4f;
    }
    held.bits = now;
    Lobby::Press p;
    p.up = edges & kUp;
    p.down = edges & kDown;
    p.left = edges & kLeft;
    p.right = edges & kRight;
    p.confirm = edges & kConfirm;
    p.back = edges & kBack;
    p.start = edges & kStart;
    return p;
}

bool LobbyModule::seatable(const InputDevices::Device& d) const {
    return d.connected && (d.kind == InputDevices::Kind::Gamepad || (m_flightSticks && d.kind == InputDevices::Kind::Joystick));
}

void LobbyModule::noteJoinDevice(const InputDevices::Device& d) {
    if (d.kind == InputDevices::Kind::Joystick) {
        m_lobby.joinButton = "the trigger";
        return;
    }
    m_joinStyle = InputModule::promptStyleFor(d);
    m_lobby.joinButton = glyph(m_joinStyle, "a");
}

void LobbyModule::readDevices(float dt) {
    const InputDevices& devices = m_input->devices();
    std::vector<uint32_t> pads;
    m_keyboardMice.clear();
    for (const InputDevices::Device& d : devices.devices()) {
        if (!d.connected) continue;
        if (seatable(d)) pads.push_back(d.ref);
        else if (d.kind == InputDevices::Kind::Keyboard || d.kind == InputDevices::Kind::Mouse) m_keyboardMice.push_back(d.ref);
    }
    // Controllers that came or went.
    for (uint32_t p : pads)
        if (std::find(m_pads.begin(), m_pads.end(), p) == m_pads.end()) {
            if (const InputDevices::Device* d = devices.find(p)) noteJoinDevice(*d);
            m_lobby.padConnected(p);
        }
    for (uint32_t p : m_pads)
        if (std::find(pads.begin(), pads.end(), p) == pads.end()) {
            m_lobby.padDisconnected(p);
            m_held.erase(p);
        }
    m_pads = std::move(pads);

    // Each controller on its own; the keyboard (and mice) as one. Held
    // buttons are still tracked while deaf, so letting go isn't a press.
    const bool deaf = m_suspended || m_deafFrames > 0;
    if (m_deafFrames > 0) --m_deafFrames;
    const bool open = m_lobby.isOpen();
    for (uint32_t ref : m_pads) {
        const InputDevices::Device* dev = devices.find(ref);
        const bool stick = dev && dev->kind == InputDevices::Kind::Joystick;
        unsigned now = 0;
        if (stick) {
            // A flight stick: the trigger joins and confirms, the next
            // button goes back, the hat (or the stick itself) moves.
            auto button = [&](int b) { return devices.value({ SourceKind::JoyButton, ref, b, 0 }, nullptr) > 0.5f; };
            auto axis = [&](int a) { return devices.value({ SourceKind::JoyAxis, ref, a, 0 }, nullptr); };
            auto hat = [&](int dir) { return devices.value({ SourceKind::JoyHat, ref, dir, 0 }, nullptr) > 0.5f; };
            if (open) {
                const float x = axis(0), y = axis(1);
                if (hat(0) || y < -0.55f) now |= kUp;
                if (hat(2) || y > 0.55f) now |= kDown;
                if (hat(3) || x < -0.55f) now |= kLeft;
                if (hat(1) || x > 0.55f) now |= kRight;
                if (button(1)) now |= kBack;
            }
            if (button(0)) now |= kConfirm;
        } else {
            auto button = [&](SDL_GamepadButton b) { return devices.value({ SourceKind::GamepadButton, ref, static_cast<int32_t>(b), 0 }, nullptr) > 0.5f; };
            auto axis = [&](SDL_GamepadAxis a) { return devices.value({ SourceKind::GamepadAxis, ref, static_cast<int32_t>(a), 0 }, nullptr); };
            if (open) {
                const float x = axis(SDL_GAMEPAD_AXIS_LEFTX), y = axis(SDL_GAMEPAD_AXIS_LEFTY);
                if (button(SDL_GAMEPAD_BUTTON_DPAD_UP) || y < -0.55f) now |= kUp;
                if (button(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || y > 0.55f) now |= kDown;
                if (button(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || x < -0.55f) now |= kLeft;
                if (button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || x > 0.55f) now |= kRight;
                if (button(SDL_GAMEPAD_BUTTON_EAST)) now |= kBack;
                if (button(SDL_GAMEPAD_BUTTON_START)) now |= kStart;
            }
            if (button(SDL_GAMEPAD_BUTTON_SOUTH)) now |= kConfirm;
        }
        if (!open) m_tapped[ref] &= kConfirm;
        Lobby::Press p = pressFrom(now, m_held[ref], m_tapped[ref], dt);
        p.device = Lobby::Device::Pad;
        p.pad = ref;
        if (p.any() && !deaf) {
            if (m_lobby.seatOfPad(ref) < 0 && dev) noteJoinDevice(*dev);
            m_lobby.handle(p, m_pads);
        }
    }
    {
        auto key = [&](SDL_Scancode sc) { return devices.value(InputModule::key(sc), nullptr) > 0.5f; };
        unsigned now = 0;
        if (open) {
            if (key(SDL_SCANCODE_UP) || key(SDL_SCANCODE_W)) now |= kUp;
            if (key(SDL_SCANCODE_DOWN) || key(SDL_SCANCODE_S)) now |= kDown;
            if (key(SDL_SCANCODE_LEFT) || key(SDL_SCANCODE_A)) now |= kLeft;
            if (key(SDL_SCANCODE_RIGHT) || key(SDL_SCANCODE_D)) now |= kRight;
            if (key(SDL_SCANCODE_ESCAPE) || key(SDL_SCANCODE_BACKSPACE)) now |= kBack;
            if (key(SDL_SCANCODE_SPACE)) now |= kConfirm;
        }
        if (key(SDL_SCANCODE_RETURN) || key(SDL_SCANCODE_KP_ENTER)) now |= kConfirm;
        if (!open) m_tapped[0] &= kConfirm;
        Lobby::Press p = pressFrom(now, m_held[0], m_tapped[0], dt);
        p.device = Lobby::Device::KeyboardMouse;
        // While typing, the keyboard types (onEvent), it doesn't steer.
        if (p.any() && !deaf && !m_lobby.editing()) m_lobby.handle(p, m_pads);
    }
    m_tapped.clear();
}

void LobbyModule::onEvent(const SDL_Event& e) {
    if (m_suspended) return;
    // A finger (SDL's mouse from it) or the mouse on a row or a key.
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && m_lobby.isOpen() && tapAt(e.button.x, e.button.y)) return;
    if (m_lobby.editing() && m_lobby.isOpen()) {
        // Typing into a text row: the keyboard types (controllers still
        // drive the on-screen keyboard below).
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            m_lobby.typeText(e.text.text);
            return;
        }
        if (e.type == SDL_EVENT_KEY_DOWN) {
            switch (e.key.scancode) {
            case SDL_SCANCODE_BACKSPACE: m_lobby.backspace(); break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: if (!e.key.repeat) m_lobby.finishEditing(true); break;
            case SDL_SCANCODE_ESCAPE: if (!e.key.repeat) m_lobby.finishEditing(false); break;
            case SDL_SCANCODE_V:
                if (e.key.mod & SDL_KMOD_CTRL) // paste an address someone sent
                    if (char* clip = SDL_GetClipboardText()) {
                        m_lobby.typeText(clip);
                        SDL_free(clip);
                    }
                break;
            default: break;
            }
            m_held[0].bits = ~0u; // keys down while typing (Enter, Esc) aren't presses after it
            return;
        }
        if (e.type == SDL_EVENT_KEY_UP) return;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        // The same keys as readDevices(); Space only confirms in the menu.
        unsigned bit = 0;
        switch (e.key.scancode) {
        case SDL_SCANCODE_UP: case SDL_SCANCODE_W: bit = kUp; break;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: bit = kDown; break;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: bit = kLeft; break;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: bit = kRight; break;
        case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_BACKSPACE: bit = kBack; break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: bit = kConfirm; break;
        case SDL_SCANCODE_SPACE: bit = m_lobby.isOpen() ? static_cast<unsigned>(kConfirm) : 0u; break;
        default: break;
        }
        m_tapped[0] |= bit;
    } else if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
        unsigned bit = 0;
        switch (e.gbutton.button) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: bit = kUp; break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: bit = kDown; break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: bit = kLeft; break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: bit = kRight; break;
        case SDL_GAMEPAD_BUTTON_SOUTH: bit = kConfirm; break;
        case SDL_GAMEPAD_BUTTON_EAST: bit = kBack; break;
        case SDL_GAMEPAD_BUTTON_START: bit = kStart; break;
        default: break;
        }
        for (const InputDevices::Device& d : m_input->devices().devices())
            if (d.connected && d.kind == InputDevices::Kind::Gamepad && d.sdlId == e.gbutton.which) m_tapped[d.ref] |= bit;
    } else if (e.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN && m_flightSticks && e.jbutton.button <= 1) {
        // A flight stick's quick trigger pull (gamepads send their own events above).
        const unsigned bit = e.jbutton.button == 0 ? kConfirm : kBack;
        for (const InputDevices::Device& d : m_input->devices().devices())
            if (d.connected && d.kind == InputDevices::Kind::Joystick && d.sdlId == e.jbutton.which) m_tapped[d.ref] |= bit;
    }
}

void LobbyModule::update(const UpdateContext& ctx) {
    if (m_firstFrame) {
        // Controllers already there aren't news. KKE_LOBBY_JOIN=<n>
        // (developer builds): the first n of them join as players 2..,
        // for screenshots and tests of a full lobby without hands on pads.
        for (const InputDevices::Device& d : m_input->devices().devices())
            if (d.connected && d.kind == InputDevices::Kind::Gamepad) m_pads.push_back(d.ref);
        // Flight sticks after the gamepads: "press A" names a pad's button.
        for (const InputDevices::Device& d : m_input->devices().devices())
            if (d.connected && d.kind == InputDevices::Kind::Joystick && seatable(d)) m_pads.push_back(d.ref);
        for (uint32_t p : m_pads) m_lobby.padConnected(p, true);
        if (!m_pads.empty())
            if (const InputDevices::Device* d = m_input->devices().find(m_pads.front())) noteJoinDevice(*d);
        if (m_pads.empty()) m_lobby.joinButton = glyph(m_joinStyle, "a");
        if (const char* v = dev::env("KKE_LOBBY_JOIN"); v && *v) {
            if (m_lobby.seat(0).device == Lobby::Device::Any && m_lobby.isOpen()) {
                Lobby::Press keys; // player 1: the keyboard
                keys.device = Lobby::Device::KeyboardMouse;
                keys.confirm = true;
                m_lobby.handle(keys, m_pads);
            }
            const int n = std::atoi(v);
            for (int i = 0; i < n && i < static_cast<int>(m_pads.size()); ++i) m_lobby.join(Lobby::Device::Pad, m_pads[static_cast<size_t>(i)]);
        }
        m_firstFrame = false;
    }
    readDevices(ctx.dt);
    m_lobby.update(ctx.dt);
    updateOnline();
    if (m_lobby.editing() && !m_lobby.isOpen()) m_lobby.finishEditing(false);
    if (m_lobby.editing() != m_textInput) {
        // Text events (with the layout and any IME) only while typing.
        m_textInput = m_lobby.editing();
        if (m_textInput) SDL_StartTextInput(m_app->window().handle());
        else SDL_StopTextInput(m_app->window().handle());
    }
    if (!m_applied.empty()) assignDevices();
    refreshUi();
}

// Everyone on the other machines of a network game (NetModule), so the
// host sees who joined and what they picked, and a joiner sees the host's
// players: their characters carry their looks (Lobby::lookText).
void LobbyModule::updateOnline() {
    std::vector<Lobby::OnlinePlayer> online;
#if KKE_ENABLE_NET && KKE_ENABLE_JOLT
    if (const NetModule* net = m_app->getModule<NetModule>(); net && net->role() != NetModule::Role::Offline) {
        for (const net::RemotePlayer& r : net->remotePlayers()) {
            Lobby::OnlinePlayer p;
            p.id = r.id;
            p.name = r.name;
            p.look = m_lobby.lookFromText(r.character);
            p.host = r.id == 0;
            const std::string extra = Lobby::characterExtra(r.character);
            p.cpu = extra == "cpu" || extra.rfind("cpu,", 0) == 0 || r.character == "cpu";
            online.push_back(std::move(p));
        }
        std::sort(online.begin(), online.end(), [](const Lobby::OnlinePlayer& a, const Lobby::OnlinePlayer& b) { return a.id < b.id; });
    }
#endif
    m_lobby.setOnlinePlayers(std::move(online));
}

void LobbyModule::shutdown() {
    // The document goes with UiModule's context.
    m_doc = nullptr;
    m_model = {};
}

PromptStyle LobbyModule::seatStyle(int seat) const {
    const Lobby::Seat& s = m_lobby.seat(seat);
    if (s.device == Lobby::Device::KeyboardMouse) return PromptStyle::Keyboard;
    if (s.device == Lobby::Device::Pad)
        if (const InputDevices::Device* d = m_input->devices().find(s.pad)) return InputModule::promptStyleFor(*d);
    return m_input->promptStyle(0);
}

std::string LobbyModule::glyph(PromptStyle style, const std::string& name) const {
    const ButtonPrompts& p = m_input->prompts();
    return p.rml(p.namedGlyphs(style, name));
}

std::string LobbyModule::promptText(PromptStyle style, const std::string& text) const {
    return m_input->prompts().format(style, m_input->map(0), text);
}

void LobbyModule::buildUi() {
    auto* ui = m_app->getModule<UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("kke_lobby");
    if (!c) return;
    if (auto r = c.RegisterStruct<RowView>()) {
        r.RegisterMember("label", &RowView::label);
        r.RegisterMember("value", &RowView::value);
        r.RegisterMember("swatch", &RowView::swatch);
        r.RegisterMember("focused", &RowView::focused);
        r.RegisterMember("action", &RowView::action);
        r.RegisterMember("start", &RowView::start);
        r.RegisterMember("arrows", &RowView::arrows);
        r.RegisterMember("has_swatch", &RowView::hasSwatch);
        r.RegisterMember("empty", &RowView::empty);
    }
    c.RegisterArray<std::vector<RowView>>();
    if (auto s = c.RegisterStruct<SeatView>()) {
        s.RegisterMember("joined", &SeatView::joined);
        s.RegisterMember("you", &SeatView::you);
        s.RegisterMember("unplugged", &SeatView::unplugged);
        s.RegisterMember("waiting", &SeatView::waiting);
        s.RegisterMember("name", &SeatView::name);
        s.RegisterMember("device", &SeatView::device);
        s.RegisterMember("accent", &SeatView::accent);
        s.RegisterMember("prompt", &SeatView::prompt);
        s.RegisterMember("rows", &SeatView::rows);
    }
    c.RegisterArray<std::vector<SeatView>>();
    if (auto o = c.RegisterStruct<OnlineView>()) {
        o.RegisterMember("name", &OnlineView::name);
        o.RegisterMember("tag", &OnlineView::tag);
        o.RegisterMember("look", &OnlineView::look);
        o.RegisterMember("accent", &OnlineView::accent);
    }
    c.RegisterArray<std::vector<OnlineView>>();
    c.RegisterArray<std::vector<std::string>>();
    if (auto k = c.RegisterStruct<KeyView>()) {
        k.RegisterMember("label", &KeyView::label);
        k.RegisterMember("focused", &KeyView::focused);
        k.RegisterMember("wide", &KeyView::wide);
    }
    c.RegisterArray<std::vector<KeyView>>();
    if (auto k = c.RegisterStruct<KeyRowView>()) k.RegisterMember("keys", &KeyRowView::keys);
    c.RegisterArray<std::vector<KeyRowView>>();
    c.Bind("typing", &m_view.typing);
    c.Bind("typing_label", &m_view.typingLabel);
    c.Bind("typing_text", &m_view.typingText);
    c.Bind("typing_hint", &m_view.typingHint);
    c.Bind("keys", &m_view.keys);
    c.Bind("open", &m_view.open);
    c.Bind("title", &m_view.title);
    c.Bind("subtitle", &m_view.subtitle);
    c.Bind("hint", &m_view.hint);
    c.Bind("seats", &m_view.seats);
    c.Bind("online", &m_view.online);
    c.Bind("toasts", &m_view.toasts);
    m_model = c.GetModelHandle();
    m_doc = ctx->LoadDocumentFromMemory(kLobbyRml, "kke_lobby.rml");
    if (!m_doc) {
        log::get(name())->warn("the lobby document did not load");
        return;
    }
    m_doc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void LobbyModule::refreshUi() {
    if (!m_model) return;
    if (m_lobby.revision() == m_shownRevision && m_input->promptSerial() == m_shownPromptSerial) return;
    m_shownRevision = m_lobby.revision();
    m_shownPromptSerial = m_input->promptSerial();
    const Lobby& l = m_lobby;
    const std::string join = glyph(m_joinStyle, "a");
    const std::string enter = glyph(PromptStyle::Keyboard, "key:Return");
    View v;
    v.open = l.isOpen();
    v.title = m_title;
    v.subtitle = promptText(m_joinStyle, m_subtitle);
    const Lobby::Seat& one = l.seat(0);
    const PromptStyle style = seatStyle(0);
    const InputDevices::Device* oneDevice = one.device == Lobby::Device::Pad ? m_input->devices().find(one.pad) : nullptr;
    // A finger plays player 1's card on a touch screen (tapAt).
    const bool touch = m_input->promptStyle(0) == PromptStyle::Touch;
    if (one.device == Lobby::Device::Any) v.hint = touch ? std::string("Player 1: tap your card") : "Player 1: press " + join + " or " + enter;
    else if (touch && one.device == Lobby::Device::KeyboardMouse)
        v.hint = "Tap a row: a value changes (left half: back)   ·   the Start row begins";
    else if (oneDevice && oneDevice->kind == InputDevices::Kind::Joystick)
        v.hint = "Hat or stick: choose and change   ·   trigger: next   ·   button 2: leave   ·   player 1: the Start row begins";
    else if (isPadStyle(style))
        v.hint = promptText(style, "{dpad_up}{dpad_down} choose   ·   {dpad_left}{dpad_right} change   ·   {a} next   ·   {b} leave   ·   "
                                   "player 1: {start} or the Start row begins");
    else
        v.hint = promptText(style, "{key:Up}{key:Down} choose   ·   {key:Left}{key:Right} change   ·   {key:Return} next   ·   {key:Escape} leave   ·   "
                                   "player 1: the Start row begins");
    // The colour of a look field with swatches, for the card's top edge.
    int swatchField = -1;
    for (size_t f = 0; f < l.lookFields().size(); ++f)
        if (!l.lookFields()[f].swatches.empty()) {
            swatchField = static_cast<int>(f);
            break;
        }
    for (int i = 0; i < Lobby::kMaxSeats; ++i) {
        const Lobby::Seat& s = l.seat(i);
        SeatView sv;
        sv.joined = s.joined;
        sv.you = i == 0;
        sv.name = l.seatName(i);
        sv.unplugged = s.joined && s.device == Lobby::Device::Pad && !s.padPresent;
        sv.device = s.device == Lobby::Device::Pad ? "Controller" : s.device == Lobby::Device::KeyboardMouse ? "Keyboard and mouse" : "Any controller or the keyboard";
        if (i == 0 && touch && s.device == Lobby::Device::KeyboardMouse) sv.device = "Touch screen";
        if (s.device == Lobby::Device::Pad)
            if (const InputDevices::Device* d = m_input->devices().find(s.pad); d && d->kind == InputDevices::Kind::Joystick) sv.device = "Flight stick: " + d->label();
        sv.accent = "#3a4260";
        if (s.joined && swatchField >= 0) {
            const Lobby::LookField& f = l.lookFields()[static_cast<size_t>(swatchField)];
            const size_t c = static_cast<size_t>(s.look[static_cast<size_t>(swatchField)]);
            if (c < f.swatches.size()) sv.accent = hex(f.swatches[c]);
        }
        if (!s.joined) {
            const bool keyboardFree = l.seatOfKeyboard() < 0 && one.device == Lobby::Device::Pad;
            sv.prompt = "Press " + join + (keyboardFree ? " or " + enter : std::string()) + " to join";
        } else if (i == 0 && s.device == Lobby::Device::Any) {
            sv.waiting = true;
            sv.prompt = touch ? std::string("Tap here to play") : "Press " + join + " or " + enter;
        }
        if (s.joined && !sv.waiting) {
            const std::vector<Lobby::Row> rows = l.rows(i);
            for (size_t r = 0; r < rows.size(); ++r) {
                RowView rv;
                rv.focused = static_cast<int>(r) == std::clamp(s.row, 0, static_cast<int>(rows.size()) - 1);
                const Lobby::Row& row = rows[r];
                if (row.kind == Lobby::Row::Kind::Look) {
                    const Lobby::LookField& f = l.lookFields()[static_cast<size_t>(row.index)];
                    const size_t c = static_cast<size_t>(s.look[static_cast<size_t>(row.index)]);
                    rv.label = f.label;
                    rv.value = c < f.choices.size() ? f.choices[c] : "";
                    if (c < f.swatches.size()) {
                        rv.swatch = hex(f.swatches[c]);
                        rv.hasSwatch = true;
                    }
                } else if (row.kind == Lobby::Row::Kind::Option) {
                    const Lobby::Option& o = l.options()[static_cast<size_t>(row.index)];
                    rv.label = o.label;
                    rv.action = o.choices.empty();
                    rv.value = o.choices.empty() ? "" : o.choices[static_cast<size_t>(o.value)];
                    if (l.isTextOption(o.id)) {
                        rv.action = false;
                        rv.value = l.text(o.id);
                        rv.empty = rv.value.empty();
                        if (rv.empty) rv.value = l.placeholder(o.id);
                    }
                } else {
                    rv.start = true;
                    rv.label = "Start";
                }
                rv.arrows = rv.focused && !rv.action && !rv.start && !(row.kind == Lobby::Row::Kind::Option && l.isTextOption(l.options()[static_cast<size_t>(row.index)].id));
                sv.rows.push_back(std::move(rv));
            }
        }
        v.seats.push_back(std::move(sv));
    }
    if (l.editing()) {
        v.typing = true;
        for (const Lobby::Option& o : l.options())
            if (o.id == l.editingId()) v.typingLabel = o.label;
        v.typingText = l.text(l.editingId());
        const auto& keys = Lobby::keyboardKeys();
        for (size_t r = 0; r < keys.size(); ++r) {
            KeyRowView row;
            for (size_t k = 0; k < keys[r].size(); ++k)
                row.keys.push_back({ keys[r][k], static_cast<int>(r) == l.keyRow() && static_cast<int>(k) == l.keyCol(), keys[r][k].size() > 1 });
            v.keys.push_back(std::move(row));
        }
        v.typingHint = isPadStyle(style) && one.device == Lobby::Device::Pad
                           ? promptText(style, "{a} type   ·   {b} delete   ·   {start} done")
                           : promptText(PromptStyle::Keyboard, "type it   ·   {key:Return} done   ·   {key:Escape} put it back");
    }
    // The other machines' players, with what they picked (the name is the card's).
    for (const Lobby::OnlinePlayer& p : l.onlinePlayers()) {
        OnlineView ov;
        ov.name = p.name;
        ov.tag = p.host ? "HOST" : p.cpu ? "CPU" : "";
        ov.accent = "#56a8ff";
        for (size_t f = 0; f < l.lookFields().size() && f < p.look.size(); ++f) {
            const Lobby::LookField& field = l.lookFields()[f];
            if (field.id == "name") continue;
            const size_t c = static_cast<size_t>(p.look[f]);
            if (static_cast<int>(f) == swatchField && c < field.swatches.size()) ov.accent = hex(field.swatches[c]);
            const std::string choice = l.lookChoice(p.look, static_cast<int>(f));
            if (choice.empty()) continue;
            if (!ov.look.empty()) ov.look += "  ·  ";
            ov.look += field.label + ": " + choice;
        }
        v.online.push_back(std::move(ov));
    }
    for (const Lobby::Toast& t : l.toasts()) v.toasts.push_back(t.text);
    m_view = std::move(v);
    m_model.DirtyAllVariables();
}

} // namespace kke
