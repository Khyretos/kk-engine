// The HUD (RmlUi, ui/horde_hud.rml): the king's health and stamina, the
// wave, goblins left and kills, a banner between waves, the controls.

#include "HordeModule.h"

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

namespace horde {

namespace {

std::string percent(float fraction) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(std::clamp(fraction, 0.0f, 1.0f) * 100.0f)));
    return buf;
}

} // namespace

void HordeModule::buildHud() {
    auto* ui = m_app->getModule<kke::UiModule>();
    if (!ui || !ui->context()) return;
    Rml::Context* ctx = ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("horde");
    if (!c) return;
    c.Bind("health", &m_hud.health);
    c.Bind("stamina", &m_hud.stamina);
    c.Bind("low", &m_hud.low);
    c.Bind("wave", &m_hud.wave);
    c.Bind("left", &m_hud.left);
    c.Bind("kills", &m_hud.kills);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    m_hudModel = c.GetModelHandle();

    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "ui/horde_hud.rml";
    m_hudDoc = ctx->LoadDocument(path);
    if (!m_hudDoc) {
        kke::log::get(name())->error("HUD: could not load {}", path);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void HordeModule::updateHud() {
    if (!m_hudModel) return;
    auto set = [this](std::string& field, const std::string& value, const char* var) {
        if (field == value) return;
        field = value;
        m_hudModel.DirtyVariable(var);
    };
    const kke::Combatant& c = m_combat.get(m_hero.id);
    set(m_hud.health, percent(c.healthFraction()), "health");
    set(m_hud.stamina, percent(c.staminaFraction()), "stamina");
    const bool low = c.healthFraction() < 0.25f;
    if (low != m_hud.low) {
        m_hud.low = low;
        m_hudModel.DirtyVariable("low");
    }
    int alive = 0;
    for (const auto& g : m_goblins) alive += g->dead ? 0 : 1;
    set(m_hud.wave, "Wave " + std::to_string(m_wave), "wave");
    set(m_hud.left, std::to_string(alive + m_toSpawn) + " goblins left", "left");
    set(m_hud.kills, std::to_string(m_kills) + " slain", "kills");
    std::string banner, sub;
    switch (m_phase) {
    case Phase::Intro:
        banner = "Wave " + std::to_string(m_wave);
        sub = std::to_string(m_waveSize) + " goblins are coming";
        break;
    case Phase::Fighting:
        if (m_morale < 0.15f && alive > 0) banner = "They're breaking!";
        break;
    case Phase::Cleared:
        banner = "Wave " + std::to_string(m_wave) + " beaten";
        sub = "Catch your breath";
        break;
    case Phase::Overrun:
        banner = "Overrun";
        sub = "Wave " + std::to_string(m_wave) + ", " + std::to_string(m_kills) + " goblins slain. R (Start) to try again";
        break;
    }
    set(m_hud.banner, banner, "banner");
    set(m_hud.sub, sub, "sub");
    set(m_hud.hint,
        m_captured ? "WASD move  ·  left mouse slash  ·  right mouse great swing  ·  Shift block (just in time: parry)  ·  Space roll  ·  Esc frees the mouse"
                   : "Click to take the mouse  ·  WASD move  ·  left mouse slash  ·  right mouse great swing  ·  Shift block  ·  Space roll",
        "hint");
}

} // namespace horde
