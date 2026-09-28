// The HUD (RmlUi, ui/flying_hud.rml): each player's panel in a corner of
// their view (speed, height, throttle, stall warning, the ring or score,
// the last trick, which controller they fly with), the standings, the
// countdown and results in the middle, and the pause menu, where a
// player picks another controller.

#include "FlyingModule.h"

#include "kke/Log.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace flying {

namespace {

std::string clock(float seconds) {
    char buf[32];
    const int m = static_cast<int>(seconds) / 60;
    std::snprintf(buf, sizeof(buf), "%d:%05.2f", m, static_cast<double>(seconds - static_cast<float>(m) * 60.0f));
    return buf;
}

std::string hexColour(const glm::vec3& c) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

std::string percent(float v) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v * 100.0f));
    return buf;
}

std::string ordinal(int n) {
    const char* suffix = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

std::string points(int n) {
    std::string digits = std::to_string(std::abs(n)), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return (n < 0 ? "-" : "") + out;
}

} // namespace

void FlyingModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("flying");
    if (!c) return;
    if (auto p = c.RegisterStruct<PlayerHud>()) {
        p.RegisterMember("name", &PlayerHud::name);
        p.RegisterMember("speed", &PlayerHud::speed);
        p.RegisterMember("altitude", &PlayerHud::altitude);
        p.RegisterMember("throttle", &PlayerHud::throttle);
        p.RegisterMember("status", &PlayerHud::status);
        p.RegisterMember("big", &PlayerHud::big);
        p.RegisterMember("sub", &PlayerHud::sub);
        p.RegisterMember("device", &PlayerHud::device);
        p.RegisterMember("accent", &PlayerHud::accent);
        p.RegisterMember("x", &PlayerHud::x);
        p.RegisterMember("y", &PlayerHud::y);
        p.RegisterMember("w", &PlayerHud::w);
        p.RegisterMember("trick", &PlayerHud::trick);
        p.RegisterMember("arrow", &PlayerHud::arrow);
        p.RegisterMember("stall", &PlayerHud::stall);
        p.RegisterMember("down", &PlayerHud::down);
    }
    c.RegisterArray<std::vector<PlayerHud>>();
    if (auto r = c.RegisterStruct<RowHud>()) {
        r.RegisterMember("place", &RowHud::place);
        r.RegisterMember("name", &RowHud::name);
        r.RegisterMember("what", &RowHud::what);
        r.RegisterMember("accent", &RowHud::accent);
        r.RegisterMember("you", &RowHud::you);
    }
    c.RegisterArray<std::vector<RowHud>>();
    if (auto r = c.RegisterStruct<PauseRow>()) {
        r.RegisterMember("label", &PauseRow::label);
        r.RegisterMember("value", &PauseRow::value);
        r.RegisterMember("focused", &PauseRow::focused);
        r.RegisterMember("arrows", &PauseRow::arrows);
        r.RegisterMember("taken", &PauseRow::taken);
    }
    c.RegisterArray<std::vector<PauseRow>>();
    c.Bind("players", &m_hud.players);
    c.Bind("standings", &m_hud.standings);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("flash", &m_hud.flash);
    c.Bind("clock", &m_hud.clock);
    c.Bind("art_status", &m_hud.artStatus);
    c.Bind("flying", &m_hud.flying);
    c.Bind("results", &m_hud.results);
    c.Bind("paused", &m_hud.paused);
    c.Bind("pause_title", &m_hud.pauseTitle);
    c.Bind("pause_hint", &m_hud.pauseHint);
    c.Bind("pause", &m_hud.pause);
    c.Bind("devices", &m_hud.devices);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/flying_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void FlyingModule::updateHud(float) {
    if (!m_hudModel) return;
    auto set = [this](auto& field, const auto& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const bool flying = m_phase != Phase::Lobby;
    set(m_hud.flying, flying, "flying");
    set(m_hud.artStatus, m_art.status, "art_status");
    char buf[96];

    // A panel per player here, in the top left corner of their view.
    std::vector<const Pilot*> players;
    for (const Pilot& p : m_pilots)
        if (p.seat >= 0 && !p.remote) players.push_back(&p);
    std::sort(players.begin(), players.end(), [](const Pilot* a, const Pilot* b) { return a->player < b->player; });
    const std::vector<kke::ViewRect> rects = kke::splitScreen(std::max(1, static_cast<int>(players.size())), true);
    const int n = static_cast<int>(m_rings.size());
    std::vector<PlayerHud> next;
    for (size_t i = 0; flying && i < players.size(); ++i) {
        const Pilot& p = *players[i];
        PlayerHud h;
        h.name = p.name;
        std::snprintf(buf, sizeof(buf), "%d km/h", static_cast<int>(std::lround(p.plane.airspeed * 3.6f)));
        h.speed = buf;
        const float height = p.plane.position.y - m_island.surface(p.plane.position.x, p.plane.position.z);
        std::snprintf(buf, sizeof(buf), "%d m", static_cast<int>(std::max(0.0f, height)));
        h.altitude = buf;
        h.throttlePct = static_cast<int>(std::lround(p.controls.throttle * 100.0f));
        h.throttle = std::to_string(h.throttlePct) + "%";
        h.stall = p.plane.stalled && !p.plane.onGround && !down(p);
        h.down = down(p);
        h.status = h.down                                   ? "Crashed! Back in a moment"
                 : h.stall                                  ? "STALL: push the nose down"
                 : p.plane.onGround && p.controls.brake     ? "On the ground, brakes on"
                 : p.plane.onGround && p.plane.airspeed < 5 ? "On the ground: throttle up to take off"
                 : p.plane.onGround                         ? "Rolling: pull up at " + std::to_string(static_cast<int>(m_flight.stallSpeed * 3.6f * 1.2f)) + " km/h"
                 : height < 30.0f && p.plane.velocity.y < -8.0f ? "PULL UP"
                                                            : std::string();
        if (m_mode == Mode::Race && n > 0) {
            h.big = p.finished ? clock(p.finishTime) : ordinal(place(p));
            h.sub = p.finished ? "Finished " + ordinal(place(p))
                               : "Lap " + std::to_string(std::min(p.lap + 1, m_laps)) + "/" + std::to_string(m_laps) + "  ·  ring " +
                                     std::to_string(p.nextRing + 1) + "/" + std::to_string(n);
            // Which way the next ring is, from the nose (0 = straight ahead).
            if (!p.finished) {
                const Ring& r = m_rings[static_cast<size_t>(p.nextRing % n)];
                const glm::vec3 to = glm::inverse(p.plane.rotation) * (r.center - p.plane.position);
                const float angle = glm::degrees(std::atan2(to.x, -to.z));
                const float up = glm::degrees(std::atan2(to.y, std::sqrt(to.x * to.x + to.z * to.z)));
                std::snprintf(buf, sizeof(buf), "rotate(%.0fdeg)", static_cast<double>(std::abs(up) > 60.0f && std::abs(angle) < 30.0f ? (up > 0 ? 0.0f : 180.0f) : angle));
                h.arrow = buf;
            }
        } else if (m_mode == Mode::Stunts) {
            h.big = points(score(p));
            h.sub = ordinal(place(p)) + "  ·  " + clock(std::max(0.0f, m_stuntTime - m_clock)) + " left";
        } else {
            h.big = "Free flight";
            h.sub = "The runway, the hills, the sea: go anywhere";
        }
        if (h.arrow.empty()) h.arrow = "none";
        h.trick = p.trickTime > 0.0f ? p.trick : std::string();
        h.device = deviceName(p.seat);
        h.accent = hexColour(p.tint);
        const kke::ViewRect& v = rects[std::min(i, rects.size() - 1)];
        h.x = percent(v.x);
        h.y = percent(v.y);
        h.w = percent(v.w);
        next.push_back(std::move(h));
    }
    bool dirty = next.size() != m_hud.players.size();
    for (size_t i = 0; !dirty && i < next.size(); ++i) {
        const PlayerHud &a = next[i], &b = m_hud.players[i];
        dirty = a.name != b.name || a.speed != b.speed || a.altitude != b.altitude || a.throttle != b.throttle || a.status != b.status || a.big != b.big ||
                a.sub != b.sub || a.device != b.device || a.accent != b.accent || a.x != b.x || a.y != b.y || a.w != b.w || a.trick != b.trick ||
                a.arrow != b.arrow || a.stall != b.stall || a.down != b.down;
    }
    if (dirty) {
        m_hud.players = std::move(next);
        m_hudModel.DirtyVariable("players");
    }

    // The standings: everyone, leader first.
    std::vector<RowHud> rows;
    if (flying && m_mode != Mode::Free) {
        std::vector<const Pilot*> order;
        for (const Pilot& p : m_pilots) order.push_back(&p);
        std::stable_sort(order.begin(), order.end(), [this](const Pilot* a, const Pilot* b) { return progress(*a) > progress(*b); });
        for (size_t i = 0; i < order.size(); ++i) {
            const Pilot& p = *order[i];
            RowHud r;
            r.place = std::to_string(i + 1);
            r.name = p.name + (p.cpu ? " (CPU)" : "");
            r.accent = hexColour(p.tint);
            r.you = p.seat >= 0 && !p.remote;
            const bool finished = p.remote ? p.net.finished : p.finished;
            if (m_mode == Mode::Stunts) r.what = points(score(p));
            else if (finished) r.what = clock(p.remote ? p.net.finishTime : p.finishTime);
            else r.what = "lap " + std::to_string(std::min((p.remote ? p.net.lap : p.lap) + 1, m_laps));
            if (down(p)) r.what = "crashed";
            rows.push_back(std::move(r));
        }
    }
    dirty = rows.size() != m_hud.standings.size();
    for (size_t i = 0; !dirty && i < rows.size(); ++i)
        dirty = rows[i].name != m_hud.standings[i].name || rows[i].what != m_hud.standings[i].what || rows[i].accent != m_hud.standings[i].accent;
    if (dirty) {
        m_hud.standings = std::move(rows);
        m_hudModel.DirtyVariable("standings");
    }

    // The middle of the screen.
    const int p1 = players.empty() ? 0 : std::max(0, players[0]->player);
    const bool stick = !players.empty() && onFlightStick(*players[0]);
    auto prompt = [this, p1, stick](std::string text) {
        if (stick) // a flight stick: its own buttons
            for (const char* a : { "smoke", "camera", "brake", "pause", "again" }) {
                const std::string from = std::string("{fly.") + a + "}", to = std::string("{stick.") + a + "}";
                for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) text.replace(at, from.size(), to);
            }
        return m_input->promptText(text, p1);
    };
    std::string banner, sub;
    const bool results = m_phase == Phase::Results;
    if (m_phase == Phase::Countdown) {
        banner = std::to_string(std::max(1, static_cast<int>(std::ceil(m_countdown))));
        sub = m_mode == Mode::Race     ? "Fly through the rings in order, " + std::to_string(m_laps) + (m_laps == 1 ? " lap" : " laps")
            : m_mode == Mode::Stunts ? "Loops, rolls, inverted, knife edge and low passes score points: " + std::to_string(static_cast<int>(m_stuntTime)) + " s"
                                     : "Take off from the runway and fly anywhere";
    } else if (m_phase == Phase::Flying && m_clock < 1.0f) {
        banner = "GO";
    } else if (results) {
        const Pilot* best = nullptr;
        for (const Pilot& p : m_pilots)
            if (!best || progress(p) > progress(*best)) best = &p;
        banner = best ? best->name + " wins" : "";
        sub = prompt("{fly.again} fly again  ·  {fly.menu} start menu");
    }
    set(m_hud.banner, flying ? banner : std::string(), "banner");
    set(m_hud.sub, flying ? sub : std::string(), "sub");
    set(m_hud.results, results, "results");
    set(m_hud.flash, flying && m_flashTime > 0.0f ? m_flash : std::string(), "flash");
    set(m_hud.clock, flying && m_mode == Mode::Race && m_phase == Phase::Flying ? clock(m_clock) : std::string(), "clock");
    std::string hint;
    if (flying && !results) {
        const bool keys = !players.empty() && onKeyboard(*players[0]);
        hint = keys ? prompt("{fly.pitch} {fly.roll} fly (or the mouse)  ·  {fly.yaw} rudder  ·  {fly.throttle.up} {fly.throttle.down} throttle  ·  "
                             "{fly.smoke} smoke  ·  {fly.camera} camera  ·  {fly.pause} pause")
             : stick ? prompt("Stick: fly  ·  twist: rudder  ·  lever: throttle  ·  {fly.smoke} smoke  ·  {fly.camera} camera  ·  {fly.brake} brakes  ·  "
                              "{fly.pause} pause")
                     : prompt("{fly.pitch} fly  ·  {fly.yaw} rudder  ·  {fly.throttle.up} {fly.throttle.down} throttle  ·  {fly.smoke} smoke  ·  "
                              "{fly.camera} camera  ·  {fly.pause} pause, change controller");
    }
    set(m_hud.hint, hint, "hint");

    // The pause menu.
    const bool paused = m_pauseSeat >= 0;
    set(m_hud.paused, paused, "paused");
    std::vector<PauseRow> pause;
    std::vector<std::string> devices;
    if (paused) {
        const std::vector<DeviceChoice> choices = deviceChoices();
        const std::string who = m_lobby ? m_lobby->lobby().seatName(m_pauseSeat) : std::string("Player 1");
        set(m_hud.pauseTitle, "Paused: " + who, "pause_title");
        std::string device = "none plugged in";
        bool taken = false;
        if (m_pauseDevice >= 0 && m_pauseDevice < static_cast<int>(choices.size())) {
            const DeviceChoice& d = choices[static_cast<size_t>(m_pauseDevice)];
            device = d.label + (d.holder == m_pauseSeat ? " (yours now)" : "");
            taken = d.holder >= 0 && d.holder != m_pauseSeat;
        }
        const char* labels[] = { "Resume", "Controls", "Restart", "Start menu", "Quit" };
        for (int i = 0; i < 5; ++i) {
            PauseRow r;
            r.label = labels[i];
            r.focused = i == m_pauseRow;
            if (i == 1) {
                r.value = device;
                r.arrows = true;
                r.taken = taken;
            }
            if (i == 2 && netClient()) r.value = "the host restarts";
            pause.push_back(std::move(r));
        }
        for (const DeviceChoice& d : choices)
            devices.push_back(d.label + ": " + (d.holder < 0 ? std::string("free") : m_lobby->lobby().seatName(d.holder)));
        set(m_hud.pauseHint,
            m_input->promptText(m_net && m_net->connected() ? "{pause.accept} choose  ·  {pause.back} back  ·  online, the flight goes on"
                                                            : "{pause.accept} choose  ·  {pause.back} back  ·  Controls: left and right pick a free one",
                                std::max(0, p1)),
            "pause_hint");
    }
    dirty = pause.size() != m_hud.pause.size();
    for (size_t i = 0; !dirty && i < pause.size(); ++i)
        dirty = pause[i].label != m_hud.pause[i].label || pause[i].value != m_hud.pause[i].value || pause[i].focused != m_hud.pause[i].focused ||
                pause[i].taken != m_hud.pause[i].taken;
    if (dirty) {
        m_hud.pause = std::move(pause);
        m_hudModel.DirtyVariable("pause");
    }
    if (devices != m_hud.devices) {
        m_hud.devices = std::move(devices);
        m_hudModel.DirtyVariable("devices");
    }
}

} // namespace flying
