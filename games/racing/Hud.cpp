// The race HUD (RmlUi, ui/racing_hud.rml): each player's place, lap and
// times in the top left of their view, speed, gear, revs and damage in
// the bottom right; the running order down the right; the lights, the
// winner and the results in the middle; the controls along the bottom.

#include "RacingModule.h"

#include "kke/Log.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>


#include <algorithm>
#include <cmath>
#include <cstdio>

namespace racing {

namespace {

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
    const int t = n % 100;
    const char* suffix = (t >= 11 && t <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

std::string points(float p) { return fmt::format("{:.0f}", p); }

} // namespace

void RacingModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("race");
    if (!c) return;
    if (auto p = c.RegisterStruct<PlayerHud>()) {
        p.RegisterMember("name", &PlayerHud::name);
        p.RegisterMember("place", &PlayerHud::place);
        p.RegisterMember("lap", &PlayerHud::lap);
        p.RegisterMember("speed", &PlayerHud::speed);
        p.RegisterMember("gear", &PlayerHud::gear);
        p.RegisterMember("rpm", &PlayerHud::rpm);
        p.RegisterMember("rpm_color", &PlayerHud::rpmColor);
        p.RegisterMember("health", &PlayerHud::health);
        p.RegisterMember("health_color", &PlayerHud::healthColor);
        p.RegisterMember("status", &PlayerHud::status);
        p.RegisterMember("x", &PlayerHud::x);
        p.RegisterMember("y", &PlayerHud::y);
        p.RegisterMember("r", &PlayerHud::r);
        p.RegisterMember("b", &PlayerHud::b);
        p.RegisterMember("accent", &PlayerHud::accent);
        p.RegisterMember("drift", &PlayerHud::drift);
        p.RegisterMember("combo", &PlayerHud::combo);
        p.RegisterMember("note", &PlayerHud::note);
        p.RegisterMember("best", &PlayerHud::best);
        p.RegisterMember("low", &PlayerHud::low);
        p.RegisterMember("drifting", &PlayerHud::drifting);
        p.RegisterMember("drag", &PlayerHud::drag);
        p.RegisterMember("redline", &PlayerHud::redline);
    }
    c.RegisterArray<std::vector<PlayerHud>>();
    if (auto s = c.RegisterStruct<StandingHud>()) {
        s.RegisterMember("place", &StandingHud::place);
        s.RegisterMember("name", &StandingHud::name);
        s.RegisterMember("gap", &StandingHud::gap);
        s.RegisterMember("accent", &StandingHud::accent);
        s.RegisterMember("you", &StandingHud::you);
        s.RegisterMember("out", &StandingHud::out);
    }
    c.RegisterArray<std::vector<StandingHud>>();
    if (auto r = c.RegisterStruct<ResultHud>()) {
        r.RegisterMember("place", &ResultHud::place);
        r.RegisterMember("name", &ResultHud::name);
        r.RegisterMember("result", &ResultHud::result);
        r.RegisterMember("note", &ResultHud::note);
        r.RegisterMember("accent", &ResultHud::accent);
    }
    c.RegisterArray<std::vector<ResultHud>>();
    c.Bind("players", &m_hud.players);
    c.Bind("standings", &m_hud.standings);
    c.Bind("results", &m_hud.results);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("notice", &m_hud.notice);
    c.Bind("lights", &m_hud.lights);
    c.Bind("racing", &m_hud.racing);
    c.Bind("howto", &m_hud.howto);
    c.Bind("drag", &m_hud.drag);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/racing_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void RacingModule::showHowTo(bool on) {
    m_howto = on;
    m_howtoAge = 0.0f;
    m_hud.howto = on;
    if (m_hudModel) m_hudModel.DirtyVariable("howto");
}

void RacingModule::updateHud(float) {
    if (!m_hudModel) return;
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    auto flag = [this](bool& field, bool value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const bool racing = m_phase != Phase::Lobby;
    const Event ev = event();
    flag(m_hud.racing, racing, "racing");
    flag(m_hud.drag, ev == Event::Drag, "drag");
    set(m_hud.notice, m_garage->hasPack() ? std::string() : "POLYGON Street Racer pack not found: block cars and a bare track (assets/synty or KKE_ASSETS_DIR)",
        "notice");

    // A panel per player, in the corners of their view.
    std::vector<const Car*> players;
    for (const Car& c : m_cars)
        if (c.seat >= 0 && !c.remote) players.push_back(&c);
    std::sort(players.begin(), players.end(), [](const Car* a, const Car* b) { return a->player < b->player; });
    const std::vector<kke::ViewRect> rects = kke::splitScreen(std::max(1, static_cast<int>(players.size())), players.size() > 2);
    const int cars = static_cast<int>(m_cars.size());
    const int running = static_cast<int>(std::count_if(m_cars.begin(), m_cars.end(), [](const Car& c) { return !c.totalled; }));
    std::vector<PlayerHud> next;
    for (size_t i = 0; i < players.size() && racing; ++i) {
        const Car& c = *players[i];
        const CarType& type = carTypes()[static_cast<size_t>(c.type)];
        PlayerHud h;
        h.name = c.name;
        h.place = ev == Event::Drag && c.falseStart ? std::string("RED LIGHT") : ordinal(c.place) + " / " + std::to_string(cars);
        if (ev == Event::Drag) {
            h.lap = c.finished ? clockText(c.finishTime) : m_phase == Phase::Racing ? clockText(m_raceClock) : "";
            h.best = c.reaction >= 0.0f ? fmt::format("reaction {:.3f} s", c.reaction) : "";
        } else if (ev == Event::Derby) {
            h.lap = fmt::format("{} of {} running", running, cars);
            h.best = fmt::format("{:.0f} pts{}", c.derbyPoints, c.wrecked ? fmt::format(", {} wrecked", c.wrecked) : std::string());
        } else if (ev == Event::Rally) {
            const bool started = c.where.s >= m_track->startS() && m_phase == Phase::Racing;
            h.lap = c.finished ? clockText(c.finishTime) : started ? clockText(m_raceClock - c.lapStart) : std::string("To the start line");
            h.best = fmt::format("{:.1f} km to go", std::max(0.0f, m_track->finishS() - c.where.s) / 1000.0f);
        } else {
            h.lap = c.finished ? std::string("Finished") : fmt::format("Lap {} / {}", std::clamp(c.lap + 1, 1, m_laps), m_laps);
            h.best = c.bestLap > 0.0f ? "best " + clockText(c.bestLap) : m_phase == Phase::Racing ? clockText(m_raceClock - c.lapStart) : "";
        }
        h.speed = std::to_string(static_cast<int>(std::lround(std::fabs(carSpeed(c)) * 3.6f)));
        h.gear = c.state.gear < 0 ? "R" : c.state.gear == 0 ? "N" : std::to_string(c.state.gear);
        const float rpm = std::clamp(c.state.rpm / type.maxRpm, 0.0f, 1.0f);
        h.rpm = percent(rpm);
        h.redline = rpm > 0.9f;
        h.rpmColor = rpm > 0.9f ? "#ff6b5c" : rpm > 0.75f ? "#ffcf5c" : "#6fe39a";
        h.health = percent(c.health / 100.0f);
        h.healthColor = c.health > 60.0f ? "#6fe39a" : c.health > 30.0f ? "#ffcf5c" : "#ff6b5c";
        h.low = c.health < 30.0f && !c.totalled && m_damage > 0;
        h.status = c.totalled                                 ? std::string("Totalled")
                 : m_damage == 0                              ? std::string("Damage off")
                 : m_track->hasPits() && c.health < 50.0f     ? std::string("Pit! Inside the front straight")
                 : c.wrongWay > 1.0f                          ? std::string("Wrong way")
                                                              : std::string();
        h.drag = ev == Event::Drag;
        if (ev == Event::Drift) {
            h.drift = points(c.driftScore);
            h.drifting = c.driftChain > 0.0f;
            h.combo = c.driftChain > 0.0f ? fmt::format("+{:.0f}  x{:.0f}", c.driftChain, c.driftCombo) : "";
        }
        h.note = c.noteTime > 0.0f ? c.note : "";
        const kke::ViewRect& v = rects[std::min(i, rects.size() - 1)];
        h.x = percent(v.x);
        h.y = percent(v.y);
        h.r = percent(1.0f - v.x - v.w);
        h.b = percent(1.0f - v.y - v.h);
        h.accent = hexColour(c.color);
        next.push_back(std::move(h));
    }
    bool dirty = next.size() != m_hud.players.size();
    for (size_t i = 0; !dirty && i < next.size(); ++i) {
        const PlayerHud &a = next[i], &b = m_hud.players[i];
        dirty = a.name != b.name || a.place != b.place || a.lap != b.lap || a.speed != b.speed || a.gear != b.gear || a.rpm != b.rpm ||
                a.health != b.health || a.status != b.status || a.x != b.x || a.y != b.y || a.r != b.r || a.b != b.b || a.drift != b.drift ||
                a.combo != b.combo || a.note != b.note || a.best != b.best || a.low != b.low || a.redline != b.redline || a.accent != b.accent;
    }
    if (dirty) {
        m_hud.players = std::move(next);
        m_hudModel.DirtyVariable("players");
    }

    // The running order: the top eight, and every player below that.
    std::vector<const Car*> order;
    for (const Car& c : m_cars) order.push_back(&c);
    std::sort(order.begin(), order.end(), [](const Car* a, const Car* b) { return a->place < b->place; });
    std::vector<StandingHud> standings;
    const Car* leader = order.empty() ? nullptr : order[0];
    for (const Car* c : order) {
        const bool you = c->seat >= 0 && !c->remote;
        if (!racing || (c->place > 8 && !you)) continue;
        StandingHud s;
        s.place = std::to_string(c->place);
        s.name = c->name;
        s.accent = hexColour(c->color);
        s.you = you;
        s.out = c->totalled;
        if (c->totalled) s.gap = "out";
        else if (ev == Event::Drift) s.gap = points(c->driftScore + c->driftChain);
        else if (ev == Event::Derby) s.gap = fmt::format("{:.0f} pts", c->derbyPoints);
        else if (ev == Event::Rally) s.gap = c->finished ? clockText(c->finishTime) : c->where.s < m_track->startS() ? "waiting" : "on stage";
        else if (c->finished) s.gap = ev == Event::Drag ? clockText(c->finishTime) : "done";
        else if (c != leader && leader) s.gap = fmt::format("-{:.0f} m", std::max(0.0f, leader->progress - c->progress));
        standings.push_back(std::move(s));
    }
    dirty = standings.size() != m_hud.standings.size();
    for (size_t i = 0; !dirty && i < standings.size(); ++i)
        dirty = standings[i].name != m_hud.standings[i].name || standings[i].place != m_hud.standings[i].place ||
                standings[i].gap != m_hud.standings[i].gap || standings[i].out != m_hud.standings[i].out;
    if (dirty) {
        m_hud.standings = std::move(standings);
        m_hudModel.DirtyVariable("standings");
    }

    // The results: everyone in finishing order.
    std::vector<ResultHud> results;
    if (m_phase == Phase::Finished) {
        for (const Car* c : order) {
            ResultHud r;
            r.place = std::to_string(c->place) + ".";
            r.name = c->name;
            r.accent = hexColour(c->color);
            if (ev == Event::Drift) {
                r.result = points(c->driftScore) + " pts";
                r.note = c->driftBest > 0.0f ? "best drift " + points(c->driftBest) : "";
            } else if (ev == Event::Drag) {
                r.result = c->finished ? clockText(c->finishTime) : "DNF";
                r.note = c->falseStart ? std::string("false start")
                                       : c->finished ? fmt::format("reaction {:.3f} s, {:.0f} km/h", c->reaction, c->trapSpeed * 3.6f) : std::string();
            } else if (ev == Event::Derby) {
                r.result = c->totalled ? "out at " + clockText(c->outAt) : std::string("survived");
                r.note = fmt::format("{:.0f} points", c->derbyPoints) + (c->wrecked ? fmt::format(", wrecked {}", c->wrecked) : std::string());
            } else {
                r.result = c->finished ? clockText(c->finishTime) : c->totalled ? "totalled" : "DNF";
                r.note = c->bestLap > 0.0f ? "best lap " + clockText(c->bestLap) : "";
                if (c->pitStops > 0) r.note += (r.note.empty() ? "" : ", ") + std::to_string(c->pitStops) + (c->pitStops == 1 ? " pit stop" : " pit stops");
            }
            if (c->hits > 0 && m_damage > 0) r.note += (r.note.empty() ? "" : ", ") + std::to_string(c->hits) + (c->hits == 1 ? " hit" : " hits");
            results.push_back(std::move(r));
            if (results.size() >= 12) break;
        }
    }
    dirty = results.size() != m_hud.results.size();
    for (size_t i = 0; !dirty && i < results.size(); ++i)
        dirty = results[i].name != m_hud.results[i].name || results[i].result != m_hud.results[i].result || results[i].note != m_hud.results[i].note;
    if (dirty) {
        m_hud.results = std::move(results);
        m_hudModel.DirtyVariable("results");
    }

    // The middle: the lights, GO, the winner.
    std::string banner, sub, lights;
    const Car* you = players.empty() ? (m_cars.empty() ? nullptr : &m_cars[0]) : players[0];
    const int p1 = players.empty() ? 0 : std::max(0, players[0]->player);
    auto prompt = [this, p1](const std::string& text) { return m_input->promptText(text, p1); };
    if (m_phase == Phase::Countdown) {
        if (ev == Event::Drag) {
            lights = m_countdown > 1.5f ? "" : m_countdown > 1.0f ? "amber1" : m_countdown > 0.5f ? "amber2" : "amber3";
            if (you && you->falseStart) lights = "red";
            sub = m_countdown > 1.5f ? std::string("Rev it: hold {brake} and {throttle} for a burnout. Go on green!") : "";
        } else {
            banner = m_countdown > 2.0f ? "3" : m_countdown > 1.0f ? "2" : "1";
            sub = ev == Event::Drift   ? std::string("Slide through the corners: the longer and faster, the more points. Don't hit anything!")
                : ev == Event::Derby   ? std::string("Wreck them all: the last car running wins. Back into them: the boot can take it, the engine can't.")
                : ev == Event::Rally   ? std::string("One at a time against the clock. Loose gravel: brake early and slide it round.")
                                       : fmt::format("{} laps. Drive carefully: hits hurt your car.", m_laps);
            if (m_netHold) sub = "Waiting for everyone to reach the grid...";
        }
    } else if (m_phase == Phase::Racing && m_raceClock < 1.0f) {
        if (ev == Event::Drag) lights = you && you->falseStart ? "red" : "green";
        else banner = "GO";
    } else if (m_phase == Phase::Finished) {
        const bool youWon = you && m_winner == you->name && players.size() <= 1;
        banner = m_winner.empty() ? std::string("Race over") : youWon ? std::string("You win!") : m_winner + " wins";
        if (you) {
            sub = (players.size() > 1 ? you->name + ": " : std::string()) + ordinal(you->place);
            if (ev == Event::Drift) sub += ", " + points(you->driftScore) + " points";
            else if (ev == Event::Derby) sub += fmt::format(", {:.0f} points", you->derbyPoints) + (you->totalled ? ", wrecked" : ", still running");
            else if (you->finished) sub += ", " + clockText(you->finishTime);
            else if (you->totalled) sub += ", totalled";
        }
    }
    set(m_hud.banner, racing ? banner : std::string(), "banner");
    set(m_hud.sub, racing ? prompt(sub) : std::string(), "sub");
    set(m_hud.lights, racing ? lights : std::string(), "lights");

    std::string hint;
    if (!racing) {
    } else if (m_phase == Phase::Finished) {
        hint = prompt("{race.again} race again  ·  {race.new} next track");
    } else if (you && you->totalled) {
        hint = prompt("Totalled! Watch the rest  ·  {camera} camera  ·  {race.again} race again");
    } else if (ev == Event::Drag) {
        hint = prompt("{throttle} go on green  ·  {shift.up} gear up at the top of the revs  ·  {help} how to play");
    } else if (ev == Event::Drift) {
        hint = prompt("{steer} steer  ·  {throttle} gas  ·  {handbrake} handbrake to start a slide  ·  {camera} camera  ·  {help} how to play");
    } else {
        hint = prompt("{steer} steer  ·  {throttle} gas  ·  {brake} brake  ·  {look.back} look back  ·  {reset.car} back on track  ·  {help} how to play");
    }
    set(m_hud.hint, hint, "hint");
}

} // namespace racing
