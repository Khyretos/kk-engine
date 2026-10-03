// The race HUD (RmlUi, ui/climb_hud.rml): each player's clock, stamina bar
// and what each hand is doing, the countdown and the result in the middle,
// and a line of controls at the bottom.

#include "ClimbRaceModule.h"

#include "kke/Log.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <algorithm>

namespace climb_race {

namespace {

std::string clock(float seconds) {
    char buf[32];
    const int m = static_cast<int>(seconds) / 60;
    std::snprintf(buf, sizeof(buf), "%d:%05.2f", m, static_cast<double>(seconds - static_cast<float>(m) * 60.0f));
    return buf;
}

std::string handText(const kke::Climber& c, int h) {
    if (!c.climbing()) return "";
    if (c.handMoving(h)) {
        switch (c.handMove(h)) {
        case kke::Climber::Move::Precise: return "reaching";
        case kke::Climber::Move::Quick: return "snatching";
        case kke::Climber::Move::Lunge: return "lunging";
        case kke::Climber::Move::None: break;
        }
    }
    if (c.charge(h) > 0.05f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "power %d%%", static_cast<int>(c.charge(h) * 100.0f));
        return buf;
    }
    const int hold = c.handHold(h);
    return hold < 0 ? "free" : kke::holdKindName(c.wall().holds()[static_cast<size_t>(hold)].kind);
}

std::string hexColour(const glm::vec3& c) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

} // namespace

std::string ClimbRaceModule::clockText(float seconds) { return clock(seconds); }

void ClimbRaceModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("climb");
    if (!c) return;
    if (auto p = c.RegisterStruct<PlayerHud>()) {
        p.RegisterMember("name", &PlayerHud::name);
        p.RegisterMember("time", &PlayerHud::time);
        p.RegisterMember("stamina", &PlayerHud::stamina);
        p.RegisterMember("stamina_color", &PlayerHud::staminaColor);
        p.RegisterMember("left", &PlayerHud::left);
        p.RegisterMember("right", &PlayerHud::right);
        p.RegisterMember("height", &PlayerHud::height);
        p.RegisterMember("status", &PlayerHud::status);
        p.RegisterMember("x", &PlayerHud::x);
        p.RegisterMember("y", &PlayerHud::y);
        p.RegisterMember("accent", &PlayerHud::accent);
        p.RegisterMember("low", &PlayerHud::low);
    }
    c.RegisterArray<std::vector<PlayerHud>>();
    if (auto r = c.RegisterStruct<RivalHud>()) {
        r.RegisterMember("name", &RivalHud::name);
        r.RegisterMember("height", &RivalHud::height);
        r.RegisterMember("accent", &RivalHud::accent);
        r.RegisterMember("status", &RivalHud::status);
    }
    c.RegisterArray<std::vector<RivalHud>>();
    if (auto r = c.RegisterStruct<ResultHud>()) {
        r.RegisterMember("place", &ResultHud::place);
        r.RegisterMember("name", &ResultHud::name);
        r.RegisterMember("result", &ResultHud::result);
        r.RegisterMember("note", &ResultHud::note);
        r.RegisterMember("accent", &ResultHud::accent);
    }
    c.RegisterArray<std::vector<ResultHud>>();
    c.Bind("results", &m_hud.results);
    c.Bind("players", &m_hud.players);
    c.Bind("rivals", &m_hud.rivals);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("racing", &m_hud.racing);
    c.Bind("howto", &m_hud.howto);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/climb_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void ClimbRaceModule::updateHud(float) {
    if (!m_hudModel) return;
    const kke::RigidWorld& w = m_rigid->world();
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const bool racing = m_phase != Phase::Lobby;
    if (m_hud.racing != racing) {
        m_hud.racing = racing;
        m_hudModel.DirtyVariable("racing");
    }
    char buf[64];
    // A panel per player, in the top left corner of their view.
    std::vector<const Racer*> players;
    for (const Racer& r : m_racers)
        if (r.seat >= 0) players.push_back(&r);
    std::sort(players.begin(), players.end(), [](const Racer* a, const Racer* b) { return a->player < b->player; });
    const std::vector<kke::ViewRect> rects = kke::splitScreen(std::max(1, static_cast<int>(players.size())), true);
    std::vector<PlayerHud> next;
    for (size_t i = 0; i < players.size(); ++i) {
        const Racer& r = *players[i];
        const kke::Climber& c = *r.climber;
        PlayerHud h;
        h.name = r.name;
        h.time = clock(r.time);
        const float s = c.staminaFraction();
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(s * 100.0f)));
        h.stamina = buf;
        h.staminaColor = s > 0.5f ? "#6fe39a" : s > 0.25f ? "#ffcf5c" : "#ff6b5c";
        h.low = s < 0.25f && c.climbing();
        h.left = handText(c, 0);
        h.right = handText(c, 1);
        const float y = w.characterPosition(r.id).y;
        std::snprintf(buf, sizeof(buf), "%.1f / %.0f m", static_cast<double>(std::max(0.0f, y)), static_cast<double>(c.wall().summitY()));
        h.height = buf;
        const bool resting = c.climbing() && c.drainRate() < 0.0f;
        h.status = r.out           ? std::string("out: watching")
                 : r.hitFlash > 0.0f ? std::string("hit by a rock!")
                 : r.finished ? (r.medal >= 0 ? std::string("topped out: ") + medalName(r.medal) : std::string("topped out"))
                 : c.state() == kke::Climber::State::Mantle ? std::string("mantling")
                 : resting ? "shaking out"
                 : c.climbing() ? (c.drainRate() > 8.0f ? "pumped" : "climbing")
                 : r.loco->state() == kke::Locomotion::State::Air ? "falling"
                 : y > 1.0f ? "resting on a ledge"
                 : "on the ground";
        const kke::ViewRect& v = rects[std::min(i, rects.size() - 1)];
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v.x * 100.0f));
        h.x = buf;
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v.y * 100.0f));
        h.y = buf;
        h.accent = hexColour(r.tint);
        next.push_back(std::move(h));
    }
    bool dirty = next.size() != m_hud.players.size();
    for (size_t i = 0; !dirty && i < next.size(); ++i) {
        const PlayerHud &a = next[i], &b = m_hud.players[i];
        dirty = a.name != b.name || a.time != b.time || a.stamina != b.stamina || a.staminaColor != b.staminaColor || a.low != b.low ||
                a.left != b.left || a.right != b.right || a.height != b.height || a.status != b.status || a.x != b.x || a.y != b.y ||
                a.accent != b.accent;
    }
    if (dirty) {
        m_hud.players = std::move(next);
        m_hudModel.DirtyVariable("players");
    }

    // Everyone else, highest first.
    std::vector<const Racer*> order;
    for (const Racer& r : m_racers)
        if (r.seat < 0) order.push_back(&r);
    std::stable_sort(order.begin(), order.end(),
                     [&w](const Racer* a, const Racer* b) { return w.characterPosition(a->id).y > w.characterPosition(b->id).y; });
    std::vector<RivalHud> rivals;
    for (const Racer* r : order) {
        RivalHud h;
        h.name = r->name;
        std::snprintf(buf, sizeof(buf), "%.0f m", static_cast<double>(std::max(0.0f, w.characterPosition(r->id).y)));
        h.height = buf;
        h.accent = hexColour(r->tint);
        h.status = r->finished ? "top" : r->out ? "out" : "";
        rivals.push_back(std::move(h));
    }
    dirty = rivals.size() != m_hud.rivals.size();
    for (size_t i = 0; !dirty && i < rivals.size(); ++i)
        dirty = rivals[i].name != m_hud.rivals[i].name || rivals[i].height != m_hud.rivals[i].height || rivals[i].status != m_hud.rivals[i].status ||
                rivals[i].accent != m_hud.rivals[i].accent;
    if (dirty) {
        m_hud.rivals = std::move(rivals);
        m_hudModel.DirtyVariable("rivals");
    }

    // The finish screen: who topped out in what time, then everyone else
    // by how high they got (out of Elimination last).
    std::vector<ResultHud> results;
    if (m_phase == Phase::Finished) {
        std::vector<const Racer*> order;
        for (const Racer& r : m_racers) order.push_back(&r);
        std::stable_sort(order.begin(), order.end(), [&w](const Racer* a, const Racer* b) {
            if (a->finished != b->finished) return a->finished;
            if (a->finished) return a->time < b->time;
            if (a->out != b->out) return !a->out;
            return w.characterPosition(a->id).y > w.characterPosition(b->id).y;
        });
        for (size_t i = 0; i < order.size(); ++i) {
            const Racer& r = *order[i];
            ResultHud h;
            h.place = std::to_string(i + 1) + ".";
            h.name = r.name;
            h.accent = hexColour(r.tint);
            if (r.finished) {
                h.result = clock(r.time);
            } else {
                std::snprintf(buf, sizeof(buf), "%.1f m", static_cast<double>(std::max(0.0f, w.characterPosition(r.id).y)));
                h.result = buf;
            }
            std::string note = r.out ? "out" : r.ghost ? "your best run" : "";
            if (r.medal >= 0) note = std::string(medalName(r.medal)) + " medal";
            if (r.newBest) note += note.empty() ? "a new best!" : ", a new best!";
            if (r.falls > 0 && !r.ghost) note += (note.empty() ? "" : ", ") + std::to_string(r.falls) + (r.falls == 1 ? " fall" : " falls");
            h.note = note;
            results.push_back(std::move(h));
        }
    }
    dirty = results.size() != m_hud.results.size();
    for (size_t i = 0; !dirty && i < results.size(); ++i)
        dirty = results[i].name != m_hud.results[i].name || results[i].result != m_hud.results[i].result || results[i].note != m_hud.results[i].note;
    if (dirty) {
        m_hud.results = std::move(results);
        m_hudModel.DirtyVariable("results");
    }

    std::string banner, sub;
    const Racer* you = players.empty() ? &m_racers[0] : players[0];
    if (m_phase == Phase::Countdown) {
        banner = m_countdown > 2.0f ? "3" : m_countdown > 1.0f ? "2" : "1";
        sub = m_mode == Mode::Rockfall      ? "Race to the summit. Watch out for falling rocks!"
            : m_mode == Mode::Elimination ? "Race to the summit. Every 30 seconds the lowest climber is out!"
            : m_mode == Mode::TimeTrial && m_ghost ? "Beat your ghost: " + m_ghost->name + ", " + clock(m_ghost->time())
            : m_mode == Mode::TimeTrial && !netClient() && !netHost() ? "No ghost yet: set a time and it races you next time"
                                          : "Race to the summit";
    } else if (m_phase == Phase::Racing && you->time < 0.8f && !you->finished) {
        banner = "GO";
    } else if (m_phase == Phase::Racing && m_flashTime > 0.0f) {
        banner = m_flash;
    } else if (m_phase == Phase::Racing && m_mode == Mode::Elimination && !isDone(*you)) {
        std::snprintf(buf, sizeof(buf), "The lowest climber is out in %d s", static_cast<int>(std::ceil(m_elimTimer)));
        sub = buf;
    } else if (m_phase == Phase::Finished && you->out) {
        banner = m_winner.empty() ? std::string("Out!") : m_winner + " wins";
        sub = (players.size() > 1 ? you->name : std::string("You")) + " went out at " + clock(you->time) +
              "  ·  {race.again} race again  ·  {race.new} next mountain";
    } else if (m_phase == Phase::Finished) {
        banner = m_winner == you->name && players.size() <= 1 ? "You win" : m_winner + " wins";
        sub = (players.size() > 1 ? you->name + " " : std::string("Your time ")) + clock(you->time);
        if (you->finished && you->medal >= 0) sub += std::string("  ·  ") + medalName(you->medal) + " medal";
        if (you->finished && you->newBest) {
            sub += "  ·  a new best!";
        } else if (const std::string rec = recordText(m_mountain); !rec.empty()) {
            sub += "  ·  " + rec;
        }
        if (!m_opened.empty()) sub += "  ·  " + m_opened + " is open!";
        sub += "  ·  {race.again} race again  ·  {race.new} next mountain";
    }
    // Button prompts: the buttons of the device player 1 is using.
    const int p1 = players.empty() ? 0 : std::max(0, players[0]->player);
    auto prompt = [this, p1](const std::string& text) { return m_input->promptText(text, p1); };
    set(m_hud.banner, racing ? banner : std::string(), "banner");
    set(m_hud.sub, racing ? prompt(sub) : std::string(), "sub");
    const kke::Climber& c = *you->climber;
    // What to press next: on an edge, pull up; a hand came off, grab again;
    // tired, rest; otherwise the moves.
    std::string hint;
    if (!racing) {
    } else if (m_phase == Phase::Finished) {
        hint = prompt("{race.again} race again  ·  {race.new} next mountain");
    } else if (m_phase == Phase::Racing && you->finished) {
        hint = prompt("You topped out! Watch the others come up  ·  {race.again} race again now");
    } else if (m_phase == Phase::Racing && you->out) {
        hint = prompt("You're out: watch who's last on the rock  ·  {race.again} race again now");
    } else if (c.climbing()) {
        const auto onEdge = [&c](int h) {
            return c.handHold(h) >= 0 && c.wall().holds()[static_cast<size_t>(c.handHold(h))].kind == kke::ClimbHold::Kind::Edge;
        };
        const bool freeHand = (c.handHold(0) < 0 && !c.handMoving(0)) || (c.handHold(1) < 0 && !c.handMoving(1));
        hint = onEdge(0) && onEdge(1)       ? prompt("{jump} pull yourself up onto the edge")
             : freeHand                     ? prompt("A hand let go: {move} aim and {reach.left} {reach.right} grab again, quick!")
             : c.staminaFraction() < 0.3f   ? prompt("Tired! Rest on two green holds or a ledge  ·  {letgo} let go")
                                            : prompt("{move} aim  ·  {reach.left} {reach.right} reach  ·  hold and let go {grab.left} {grab.right} to lunge  ·  "
                                                     "{help} how to play");
    } else {
        hint = prompt("{move} walk to the rock  ·  {reach.left} {reach.right} grab it  ·  {help} how to play");
    }
    set(m_hud.hint, hint, "hint");
}

} // namespace climb_race
