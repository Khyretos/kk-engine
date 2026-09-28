// The HUD (RmlUi, ui/tennis_hud.rml): the scoreboard top left (who
// serves, sets and games, the game's points), the umpire's call in the
// middle ("Out", "15-30", "Game Juno"), and the controls at the bottom.

#include "TennisModule.h"

#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

namespace tennis {

void TennisModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("tennis");
    if (!c) return;
    if (auto p = c.RegisterStruct<TeamRow>()) {
        p.RegisterMember("name", &TeamRow::name);
        p.RegisterMember("sets", &TeamRow::sets);
        p.RegisterMember("points", &TeamRow::points);
        p.RegisterMember("serving", &TeamRow::serving);
    }
    c.Bind("t0", &m_hud.t[0]);
    c.Bind("t1", &m_hud.t[1]);
    c.Bind("call", &m_hud.call);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("banner", &m_hud.banner);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/tennis_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void TennisModule::updateHud() {
    if (!m_hudModel) return;
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const Match* m = m_matches.empty() || m_inMenu ? nullptr : m_matches.front().get();
    for (int t = 0; t < 2; ++t) {
        TeamRow next;
        if (m) {
            for (int idx : m->players)
                if (m_players[static_cast<size_t>(idx)].team == t) next.name += (next.name.empty() ? "" : " & ") + m_players[static_cast<size_t>(idx)].name;
            next.sets = m->score.setsText(t);
            next.points = m->score.over() ? (m->score.winner() == t ? "WIN" : "") : m->score.pointText(t);
            next.serving = !m->score.over() && m->score.servingTeam() == t;
        }
        TeamRow& cur = m_hud.t[t];
        if (cur.name != next.name || cur.sets != next.sets || cur.points != next.points || cur.serving != next.serving) {
            cur = next;
            m_hudModel.DirtyVariable(t == 0 ? "t0" : "t1");
        }
    }
    std::string call, sub, hint, banner;
    if (m) {
        switch (m->phase) {
        case Match::Phase::PointOver:
        case Match::Phase::MatchOver:
            call = m->call;
            sub = m->sub;
            break;
        case Match::Phase::Serve:
            sub = m->phaseTime < 1.2f ? m->sub : "";
            break;
        default: break;
        }
        // Controls for the players at this screen.
        bool human = false, humanServes = false;
        for (int idx : m->players) {
            const Player& p = m_players[static_cast<size_t>(idx)];
            if (p.cpu || p.remote) continue;
            human = true;
            if (m->phase == Match::Phase::Serve && idx == serverIndex(*m)) humanServes = true;
        }
        if (humanServes) hint = m_input->promptText("{tennis.topspin} toss, then again at the top to serve  {move} aim");
        else if (human && m->phase != Match::Phase::MatchOver)
            hint = m_input->promptText("{move} run  {tennis.topspin} topspin  {tennis.flat} flat  {tennis.slice} slice  {tennis.lob} lob  "
                                       "(press early, hold to hit harder, the stick aims)  {tennis.menu} menu");
        if (m->phase == Match::Phase::MatchOver && human) hint = m_input->promptText("{tennis.menu} back to the menu");
        if (m->score.inTiebreak()) banner = "Tiebreak";
        else if (m->rally.secondServeNow() && m->phase == Match::Phase::Serve) banner = "Second serve";
    }
    set(m_hud.call, call, "call");
    set(m_hud.sub, sub, "sub");
    set(m_hud.hint, hint, "hint");
    set(m_hud.banner, banner, "banner");
}

} // namespace tennis
