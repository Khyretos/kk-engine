// The fight HUD (RmlUi, ui/duel_hud.rml): both corners' health and stamina
// bars and round wins at the top, the round banner in the middle, the
// controls at the bottom.

#include "DuelModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace duel {

namespace {

std::string percent(float fraction) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(std::clamp(fraction, 0.0f, 1.0f) * 100.0f)));
    return buf;
}

} // namespace

void DuelModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("duel");
    if (!c) return;
    if (auto p = c.RegisterStruct<Corner>()) {
        p.RegisterMember("name", &Corner::name);
        p.RegisterMember("health", &Corner::health);
        p.RegisterMember("stamina", &Corner::stamina);
        p.RegisterMember("wins", &Corner::wins);
        p.RegisterMember("note", &Corner::note);
        p.RegisterMember("low", &Corner::low);
    }
    c.Bind("blue", &m_hud.c[0]);
    c.Bind("red", &m_hud.c[1]);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("round", &m_hud.round);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/duel_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void DuelModule::updateHud() {
    if (!m_hudModel) return;
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    for (int i = 0; i < 2; ++i) {
        const Fighter& f = m_fighters[i];
        const kke::Combatant& c = m_combat.get(f.id);
        Corner next;
        next.name = f.name;
        next.health = percent(c.healthFraction());
        next.stamina = percent(c.staminaFraction());
        next.wins = f.wins > 0 ? (f.wins == 1 ? "1 round" : "2 rounds") : "";
        next.low = c.healthFraction() < 0.25f;
        using S = kke::Combatant::State;
        next.note = c.state() == S::Knockdown ? "down!"
                  : c.state() == S::Stunned ? (c.stateLength() > 0.9f ? "guard broken" : "stunned")
                  : c.blocking() ? "blocking"
                  : c.staminaFraction() < 0.2f ? "winded"
                  : "";
        Corner& h = m_hud.c[i];
        if (next.name != h.name || next.health != h.health || next.stamina != h.stamina || next.wins != h.wins || next.low != h.low ||
            next.note != h.note) {
            h = next;
            m_hudModel.DirtyVariable(i == 0 ? "blue" : "red");
        }
    }
    std::string banner, sub;
    switch (m_phase) {
    case Phase::Intro:
        banner = m_phaseTime < 1.2f ? "Round " + std::to_string(m_round) : "Fight!";
        break;
    case Phase::Fight: break;
    case Phase::RoundOver:
        banner = "K.O.";
        sub = m_roundWinner + " takes round " + std::to_string(m_round);
        break;
    case Phase::MatchOver:
        banner = m_roundWinner + " wins";
        sub = "R (Start) for a rematch";
        break;
    }
    set(m_hud.banner, banner, "banner");
    set(m_hud.sub, sub, "sub");
    set(m_hud.round, "Round " + std::to_string(m_round), "round");
    const std::string hint = m_twoPlayers
                                 ? "P1: WASD, J jab, K uppercut, L knee, Shift block, Space dodge  ·  P2: arrows, 1 2 3, 0 block, Enter dodge  ·  F2 back to the bot"
                                 : "WASD move  ·  J / left mouse jab  ·  K / right mouse uppercut  ·  L knee  ·  Shift block (just in time: parry)  ·  "
                                   "Space dodge  ·  F2 two players";
    set(m_hud.hint, hint, "hint");
}

} // namespace duel
