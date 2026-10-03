// The HUD (RmlUi, ui/party_hud.rml): each player's panel in their view,
// the clock and the minigame's line, the round card before each round,
// the results after it, and the podium's standings.

#include "PartyModule.h"

#include "kke/Log.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/VoiceModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace party {

namespace {

std::string hexColour(const glm::vec3& c) {
    char buf[16];
    auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", b(c.r), b(c.g), b(c.b));
    return buf;
}

std::string ordinal(int place) {
    const int n = place + 1;
    const char* suffix = (n % 100 >= 11 && n % 100 <= 13) ? "th" : n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    return std::to_string(n) + suffix;
}

} // namespace

void PartyModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("party");
    if (!c) return;
    if (auto p = c.RegisterStruct<PlayerHud>()) {
        p.RegisterMember("name", &PlayerHud::name);
        p.RegisterMember("accent", &PlayerHud::accent);
        p.RegisterMember("status", &PlayerHud::status);
        p.RegisterMember("points", &PlayerHud::points);
        p.RegisterMember("x", &PlayerHud::x);
        p.RegisterMember("y", &PlayerHud::y);
        p.RegisterMember("w", &PlayerHud::w);
        p.RegisterMember("out", &PlayerHud::out);
        p.RegisterMember("won", &PlayerHud::won);
        p.RegisterMember("talking", &PlayerHud::talking);
        p.RegisterMember("stamina", &PlayerHud::stamina);
        p.RegisterMember("tired", &PlayerHud::tired);
        p.RegisterMember("tank", &PlayerHud::tank);
    }
    c.RegisterArray<std::vector<PlayerHud>>();
    if (auto r = c.RegisterStruct<RowHud>()) {
        r.RegisterMember("place", &RowHud::place);
        r.RegisterMember("name", &RowHud::name);
        r.RegisterMember("accent", &RowHud::accent);
        r.RegisterMember("got", &RowHud::got);
        r.RegisterMember("total", &RowHud::total);
        r.RegisterMember("note", &RowHud::note);
    }
    c.RegisterArray<std::vector<RowHud>>();
    if (auto d = c.RegisterStruct<DotHud>()) d.RegisterMember("colour", &DotHud::colour);
    c.RegisterArray<std::vector<DotHud>>();
    if (auto v = c.RegisterStruct<ChoiceHud>()) {
        v.RegisterMember("title", &ChoiceHud::title);
        v.RegisterMember("goal", &ChoiceHud::goal);
        v.RegisterMember("count", &ChoiceHud::count);
        v.RegisterMember("pointing", &ChoiceHud::pointing);
        v.RegisterMember("dots", &ChoiceHud::dots);
        v.RegisterMember("here", &ChoiceHud::here);
        v.RegisterMember("won", &ChoiceHud::won);
    }
    c.RegisterArray<std::vector<ChoiceHud>>();
    c.Bind("choices", &m_hud.choices);
    c.Bind("vote", &m_hud.vote);
    c.Bind("voteClock", &m_hud.voteClock);
    c.Bind("players", &m_hud.players);
    c.Bind("rows", &m_hud.rows);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("clock", &m_hud.clock);
    c.Bind("status", &m_hud.status);
    c.Bind("round", &m_hud.round);
    c.Bind("title", &m_hud.title);
    c.Bind("goal", &m_hud.goal);
    c.Bind("controls", &m_hud.controls);
    c.Bind("talk", &m_hud.talk);
    c.Bind("show", &m_hud.show);
    c.Bind("card", &m_hud.card);
    c.Bind("table", &m_hud.table);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/party_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void PartyModule::updateHud(float) {
    if (!m_hudModel) return;
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    auto setBool = [this](bool& field, bool value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const bool show = m_phase != Phase::Lobby;
    setBool(m_hud.show, show, "show");
    char buf[96];

    // This screen's players, in their views.
    std::vector<const Bean*> players;
    for (const Bean& b : m_beans)
        if (b.seat >= 0 && !b.remote) players.push_back(&b);
    std::sort(players.begin(), players.end(), [](const Bean* a, const Bean* b) { return a->player < b->player; });
    const bool split = m_game && m_phase != Phase::Podium && players.size() > 1;
    const std::vector<kke::ViewRect> rects = kke::splitScreen(std::max(1, static_cast<int>(players.size())), true);
    std::vector<PlayerHud> next;
    for (size_t i = 0; i < players.size(); ++i) {
        const Bean& b = *players[i];
        PlayerHud h;
        h.name = b.name;
        h.accent = hexColour(beanColour(b.look.colour));
        const int pts = static_cast<size_t>(b.index) < m_show.points.size() ? m_show.points[static_cast<size_t>(b.index)] : 0;
        h.points = std::to_string(pts) + (pts == 1 ? " point" : " points");
        std::string st = m_game && m_phase == Phase::Play ? m_game->beanStatus(*this, b) : std::string();
        if (b.out) st = "Out! Watching the others";
        else if (b.finished) st = b.result.finishOrder >= 0 ? "Finished " + ordinal(b.result.finishOrder) + "!" : "Finished!";
        h.status = st;
        h.out = b.out;
        h.won = b.finished;
        // Stamina, in steps of 5% (so the panel isn't rebuilt every frame).
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(b.stamina * 20.0f)) * 5);
        h.stamina = buf;
        h.tank = m_phase == Phase::Play || m_phase == Phase::Countdown || m_phase == Phase::Intro;
        h.tired = b.stamina < 0.3f;
        h.talking = m_voice && m_net && b.netId >= 0 && m_voice->talking() && b.player == 0;
        // Split screen: the panel in the corner of their view; one view (or
        // a shared one): side by side along the top.
        const kke::ViewRect v = split ? rects[std::min(i, rects.size() - 1)]
                                      : kke::ViewRect{ static_cast<float>(i) / static_cast<float>(players.size()), 0.0f, 1.0f / static_cast<float>(players.size()), 1.0f };
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v.x * 100.0f));
        h.x = buf;
        // A panel that would sit under the clock (top middle) goes below it.
        const bool underClock = v.y < 0.01f && v.x + 0.14f > 0.42f && v.x < 0.58f;
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v.y * 100.0f + (underClock ? 13.0f : 0.0f)));
        h.y = buf;
        std::snprintf(buf, sizeof(buf), "%.2f%%", static_cast<double>(v.w * 100.0f));
        h.w = buf;
        next.push_back(std::move(h));
    }
    bool dirty = next.size() != m_hud.players.size();
    for (size_t i = 0; !dirty && i < next.size(); ++i) {
        const PlayerHud &a = next[i], &b = m_hud.players[i];
        dirty = a.name != b.name || a.status != b.status || a.points != b.points || a.x != b.x || a.y != b.y || a.w != b.w || a.accent != b.accent ||
                a.out != b.out || a.won != b.won || a.talking != b.talking ||
                a.stamina != b.stamina || a.tired != b.tired || a.tank != b.tank;
    }
    if (dirty) {
        m_hud.players = std::move(next);
        m_hudModel.DirtyVariable("players");
    }

    // The clock and the minigame's line.
    std::string clock;
    if (m_game && m_phase == Phase::Play) {
        const int left = static_cast<int>(std::ceil(std::max(0.0f, m_game->timeLimit() - m_playTime)));
        std::snprintf(buf, sizeof(buf), "%d:%02d", left / 60, left % 60);
        clock = buf;
    }
    set(m_hud.clock, clock, "clock");
    set(m_hud.status, m_phase == Phase::Play ? m_status : std::string(), "status");
    std::snprintf(buf, sizeof(buf), "ROUND %d OF %d", std::min(m_show.round + 1, m_show.rounds), m_show.rounds);
    if (m_oneGame) std::snprintf(buf, sizeof(buf), "ONE GAME");
    set(m_hud.round, show && m_phase != Phase::Podium ? std::string(buf) : std::string(), "round");

    // Button prompts: the buttons of the device player 1 is using.
    const int p1 = players.empty() ? 0 : std::max(0, players[0]->player);
    auto prompt = [this, p1](const std::string& text) { return m_input->promptText(text, p1); };

    // The round card.
    const bool card = m_phase == Phase::Intro && m_game;
    setBool(m_hud.card, card, "card");
    set(m_hud.title, card ? std::string(m_game->title()) : std::string(), "title");
    set(m_hud.goal, card ? std::string(m_game->goal()) : std::string(), "goal");
    set(m_hud.controls, card ? prompt(m_game->controls()) : std::string(), "controls");

    // The middle: the countdown, GO, a flash, the end of the round.
    std::string banner, sub;
    if (m_phase == Phase::Countdown) {
        banner = std::to_string(std::max(1, static_cast<int>(std::ceil(3.0f - m_phaseTime))));
        sub = m_game ? m_game->goal() : "";
    } else if (m_phase == Phase::Play && m_playTime < 0.9f) {
        banner = "GO!";
    } else if (m_flashTime > 0.0f) {
        banner = m_flash;
    } else if (m_phase == Phase::RoundOver) {
        banner = "Round over!";
    } else if (m_phase == Phase::Podium) {
        const std::vector<int> order = m_show.standings();
        if (!order.empty()) banner = m_beans[static_cast<size_t>(order.front())].name + " wins the party!";
    }
    set(m_hud.banner, show ? banner : std::string(), "banner");
    set(m_hud.sub, show ? sub : std::string(), "sub");

    // The table: this round's points (Results) or the final standings (Podium).
    std::vector<RowHud> rows;
    const bool table = (m_phase == Phase::Results || m_phase == Phase::Podium) && !m_show.points.empty();
    if (table) {
        const std::vector<int> order = m_show.standings();
        const std::vector<int> places = m_show.standingPlaces();
        for (int i : order) {
            const Bean& b = m_beans[static_cast<size_t>(i)];
            RowHud r;
            r.place = ordinal(places[static_cast<size_t>(i)]);
            r.name = b.name;
            r.accent = hexColour(beanColour(b.look.colour));
            const int got = static_cast<size_t>(i) < m_roundPoints.size() ? m_roundPoints[static_cast<size_t>(i)] : 0;
            r.got = m_phase == Phase::Results ? "+" + std::to_string(got) : std::string();
            r.total = std::to_string(m_show.points[static_cast<size_t>(i)]);
            r.note = m_phase == Phase::Results ? (b.result.finished ? "finished" : b.result.out ? "out" : "") : std::string();
            rows.push_back(std::move(r));
        }
    }
    dirty = rows.size() != m_hud.rows.size();
    for (size_t i = 0; !dirty && i < rows.size(); ++i)
        dirty = rows[i].name != m_hud.rows[i].name || rows[i].got != m_hud.rows[i].got || rows[i].total != m_hud.rows[i].total ||
                rows[i].place != m_hud.rows[i].place || rows[i].note != m_hud.rows[i].note;
    if (dirty) {
        m_hud.rows = std::move(rows);
        m_hudModel.DirtyVariable("rows");
    }
    setBool(m_hud.table, table, "table");

    // The vote: the choices, who voted for which, where this screen's players point.
    const bool vote = m_phase == Phase::Vote && !m_vote.games.empty();
    std::vector<ChoiceHud> choices;
    if (vote) {
        for (size_t c = 0; c < m_vote.games.size(); ++c) {
            ChoiceHud h;
            h.title = m_vote.games[c];
            for (const auto& g : m_games)
                if (g->id() == m_vote.games[c]) {
                    h.title = g->title();
                    h.goal = g->goal();
                }
            int count = 0;
            for (const Bean& b : m_beans) {
                const size_t i = static_cast<size_t>(b.index);
                if (i < m_vote.votes.size() && m_vote.votes[i] == static_cast<int>(c)) {
                    ++count;
                    h.dots.push_back({ hexColour(beanColour(b.look.colour)) });
                }
            }
            h.count = count == 1 ? "1 vote" : std::to_string(count) + " votes";
            for (const Bean* b : players) {
                const size_t i = static_cast<size_t>(b->index);
                if (i < m_vote.cursor.size() && m_vote.cursor[i] == static_cast<int>(c) && m_vote.winner < 0) {
                    h.here = true;
                    if (players.size() > 1) h.pointing += (h.pointing.empty() ? "" : ", ") + b->name;
                }
            }
            h.won = m_vote.winner == static_cast<int>(c);
            choices.push_back(std::move(h));
        }
    }
    dirty = choices.size() != m_hud.choices.size();
    for (size_t i = 0; !dirty && i < choices.size(); ++i) {
        const ChoiceHud &a = choices[i], &b = m_hud.choices[i];
        dirty = a.title != b.title || a.count != b.count || a.pointing != b.pointing || !std::equal(a.dots.begin(), a.dots.end(), b.dots.begin(), b.dots.end(), [](const DotHud& x, const DotHud& y) { return x.colour == y.colour; }) || a.here != b.here || a.won != b.won;
    }
    if (dirty) {
        m_hud.choices = std::move(choices);
        m_hudModel.DirtyVariable("choices");
    }
    setBool(m_hud.vote, vote, "vote");
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::ceil(m_vote.left)));
    set(m_hud.voteClock, vote && m_vote.winner < 0 ? std::string(buf) : std::string(), "voteClock");

    // What to press.
    std::string hint;
    const bool anyOut = !players.empty() && std::any_of(players.begin(), players.end(), [](const Bean* b) { return b->out; });
    switch (m_phase) {
    case Phase::Intro: hint = prompt(netClient() ? "The host starts the round" : "{jump} ready  ·  {panel.toggle} menu"); break;
    case Phase::Countdown: hint = prompt(m_game ? m_game->controls() : std::string()); break;
    case Phase::Play:
        hint = prompt(m_game ? m_game->controls() : std::string());
        if (anyOut) hint += prompt("  ·  out: {jump} watch someone else");
        break;
    case Phase::Results:
        hint = prompt(netClient()                ? "The host starts the next round"
                      : m_oneGame && !online() ? "{jump} play it again  ·  {panel.toggle} menu"
                                               : "{jump} next round  ·  {panel.toggle} menu");
        break;
    case Phase::Vote: hint = prompt(m_vote.winner >= 0 ? std::string() : "{move} pick a game  ·  {jump} vote for it"); break;
    case Phase::Podium: hint = prompt(netClient() ? "The host picks what's next" : "{jump} back to the menu  ·  a new party"); break;
    default: break;
    }
    set(m_hud.hint, show ? hint : std::string(), "hint");

    // Voice: who's talking, where from, and their mutes are kke::VoiceHudModule's
    // (markers over heads, the nearby list); this line stays empty.
    set(m_hud.talk, std::string(), "talk");
}

} // namespace party
