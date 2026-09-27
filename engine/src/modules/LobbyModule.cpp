#include "kke/modules/LobbyModule.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"

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
// toasts in the top right (those show in the game too). Nothing here
// takes the mouse: it's driven by controllers and the keyboard.
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
            <div data-for="seat : seats" class="card" data-class-empty="!seat.joined"
                 data-style-left="(it_index * 25.8) + '%'" data-style-border-top-color="seat.accent">
                <div class="who">PLAYER {{it_index + 1}}</div>
                <div data-if="seat.joined">
                    <div class="name">{{seat.name}}</div>
                    <div class="device">{{seat.device}}</div>
                    <div class="unplugged" data-if="seat.unplugged">controller unplugged</div>
                    <div class="prompt" data-if="seat.waiting" data-rml="seat.prompt"></div>
                    <div data-for="row : seat.rows" class="row" data-class-focused="row.focused" data-class-start="row.start"
                         data-class-section="row.action">
                        <span data-if="row.start">{{row.label}}</span>
                        <span data-if="!row.start" class="label">{{row.label}}</span><span data-if="!row.start" class="value"><span data-if="row.has_swatch" class="swatch" data-style-background-color="row.swatch"></span><span class="arrow" data-if="row.arrows">&lt; </span>{{row.value}}<span class="arrow" data-if="row.arrows"> &gt;</span></span>
                    </div>
                </div>
                <div class="prompt" data-if="!seat.joined" data-rml="seat.prompt"></div>
            </div>
        </div>
        <div id="hint" data-rml="hint"></div>
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
    buildUi();
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

void LobbyModule::readDevices(float dt) {
    const InputDevices& devices = m_input->devices();
    std::vector<uint32_t> pads;
    m_keyboardMice.clear();
    for (const InputDevices::Device& d : devices.devices()) {
        if (!d.connected) continue;
        if (d.kind == InputDevices::Kind::Gamepad) pads.push_back(d.ref);
        else if (d.kind == InputDevices::Kind::Keyboard || d.kind == InputDevices::Kind::Mouse) m_keyboardMice.push_back(d.ref);
    }
    // Controllers that came or went.
    for (uint32_t p : pads)
        if (std::find(m_pads.begin(), m_pads.end(), p) == m_pads.end()) {
            if (const InputDevices::Device* d = devices.find(p)) m_joinStyle = InputModule::promptStyleFor(*d);
            m_lobby.joinButton = glyph(m_joinStyle, "a");
            m_lobby.padConnected(p);
        }
    for (uint32_t p : m_pads)
        if (std::find(pads.begin(), pads.end(), p) == pads.end()) {
            m_lobby.padDisconnected(p);
            m_held.erase(p);
        }
    m_pads = std::move(pads);

    // Each controller on its own; the keyboard (and mice) as one.
    const bool open = m_lobby.isOpen();
    for (uint32_t ref : m_pads) {
        auto button = [&](SDL_GamepadButton b) { return devices.value({ SourceKind::GamepadButton, ref, static_cast<int32_t>(b), 0 }, nullptr) > 0.5f; };
        auto axis = [&](SDL_GamepadAxis a) { return devices.value({ SourceKind::GamepadAxis, ref, static_cast<int32_t>(a), 0 }, nullptr); };
        unsigned now = 0;
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
        if (!open) m_tapped[ref] &= kConfirm;
        Lobby::Press p = pressFrom(now, m_held[ref], m_tapped[ref], dt);
        p.device = Lobby::Device::Pad;
        p.pad = ref;
        if (p.any()) {
            if (m_lobby.seatOfPad(ref) < 0)
                if (const InputDevices::Device* d = devices.find(ref)) m_joinStyle = InputModule::promptStyleFor(*d);
            m_lobby.joinButton = glyph(m_joinStyle, "a");
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
        if (p.any()) m_lobby.handle(p, m_pads);
    }
    m_tapped.clear();
}

void LobbyModule::onEvent(const SDL_Event& e) {
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
    }
}

void LobbyModule::update(const UpdateContext& ctx) {
    if (m_firstFrame) {
        // Controllers already there aren't news. KKE_LOBBY_JOIN=<n>
        // (developer builds): the first n of them join as players 2..,
        // for screenshots and tests of a full lobby without hands on pads.
        for (const InputDevices::Device& d : m_input->devices().devices())
            if (d.connected && d.kind == InputDevices::Kind::Gamepad) m_pads.push_back(d.ref);
        for (uint32_t p : m_pads) m_lobby.padConnected(p, true);
        if (!m_pads.empty())
            if (const InputDevices::Device* d = m_input->devices().find(m_pads.front())) m_joinStyle = InputModule::promptStyleFor(*d);
        m_lobby.joinButton = glyph(m_joinStyle, "a");
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
    if (!m_applied.empty()) assignDevices();
    refreshUi();
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
    c.RegisterArray<std::vector<std::string>>();
    c.Bind("open", &m_view.open);
    c.Bind("title", &m_view.title);
    c.Bind("subtitle", &m_view.subtitle);
    c.Bind("hint", &m_view.hint);
    c.Bind("seats", &m_view.seats);
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
    if (one.device == Lobby::Device::Any) v.hint = "Player 1: press " + join + " or " + enter;
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
            sv.prompt = "Press " + join + " or " + enter;
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
                } else {
                    rv.start = true;
                    rv.label = "Start";
                }
                rv.arrows = rv.focused && !rv.action && !rv.start;
                sv.rows.push_back(std::move(rv));
            }
        }
        v.seats.push_back(std::move(sv));
    }
    for (const Lobby::Toast& t : l.toasts()) v.toasts.push_back(t.text);
    m_view = std::move(v);
    m_model.DirtyAllVariables();
}

} // namespace kke
