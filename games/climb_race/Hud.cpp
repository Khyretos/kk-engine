// The race HUD (RmlUi, ui/climb_hud.rml): each player's clock, stamina bar
// and what each hand is doing, the countdown and the result in the middle,
// and a line of controls at the bottom.

#include "ClimbRaceModule.h"

#include "kke/Log.h"
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

} // namespace

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
        p.RegisterMember("low", &PlayerHud::low);
    }
    c.Bind("p1", &m_hud.p[0]);
    c.Bind("p2", &m_hud.p[1]);
    c.Bind("banner", &m_hud.banner);
    c.Bind("sub", &m_hud.sub);
    c.Bind("hint", &m_hud.hint);
    c.Bind("split", &m_hud.split);
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
    for (size_t i = 0; i < m_racers.size() && i < 2; ++i) {
        const Racer& r = m_racers[i];
        const kke::Climber& c = *r.climber;
        PlayerHud& h = m_hud.p[i];
        PlayerHud next = h;
        next.name = r.name;
        next.time = clock(r.time);
        const float s = c.staminaFraction();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::round(s * 100.0f)));
        next.stamina = buf;
        next.staminaColor = s > 0.5f ? "#6fe39a" : s > 0.25f ? "#ffcf5c" : "#ff6b5c";
        next.low = s < 0.25f && c.climbing();
        next.left = handText(c, 0);
        next.right = handText(c, 1);
        const float y = w.characterPosition(r.id).y;
        std::snprintf(buf, sizeof(buf), "%.1f / %.0f m", static_cast<double>(std::max(0.0f, y)), static_cast<double>(c.wall().summitY()));
        next.height = buf;
        const bool resting = c.climbing() && c.drainRate() < 0.0f;
        next.status = r.finished ? "topped out"
                    : c.state() == kke::Climber::State::Mantle ? "mantling"
                    : resting ? "shaking out"
                    : c.climbing() ? (c.drainRate() > 8.0f ? "pumped" : "climbing")
                    : r.loco->state() == kke::Locomotion::State::Air ? "falling"
                    : y > 1.0f ? "resting on a ledge"
                    : "on the ground";
        if (next.name != h.name || next.time != h.time || next.stamina != h.stamina || next.staminaColor != h.staminaColor || next.low != h.low ||
            next.left != h.left || next.right != h.right || next.height != h.height || next.status != h.status) {
            h = next;
            m_hudModel.DirtyVariable(i == 0 ? "p1" : "p2");
        }
    }

    std::string banner, sub;
    if (m_phase == Phase::Countdown) {
        banner = m_countdown > 2.0f ? "3" : m_countdown > 1.0f ? "2" : "1";
        sub = "Race to the summit";
    } else if (m_phase == Phase::Racing && m_racers[0].time < 0.8f && !m_racers[0].finished) {
        banner = "GO";
    } else if (m_phase == Phase::Finished) {
        const Racer& you = m_racers[0];
        banner = m_winner == you.name ? (m_split ? "Player 1 wins" : "You win") : m_winner + " wins";
        sub = "Your time " + clock(you.time);
        if (m_best > 0.0f) sub += "  ·  best " + clock(m_best);
        sub += "  ·  {race.again} race again  ·  {race.new} new mountain";
    }
    // Button prompts: pictures of the buttons on the device in use.
    auto prompt = [this](const std::string& text) { return m_input ? m_input->promptText(text, 0) : text; };
    set(m_hud.banner, banner, "banner");
    set(m_hud.sub, prompt(sub), "sub");
    const kke::Climber& c = *m_racers[0].climber;
    std::string hint;
    if (c.climbing()) {
        const bool edge = c.handHold(0) >= 0 && c.handHold(1) >= 0 &&
                          c.wall().holds()[static_cast<size_t>(c.handHold(0))].kind == kke::ClimbHold::Kind::Edge &&
                          c.wall().holds()[static_cast<size_t>(c.handHold(1))].kind == kke::ClimbHold::Kind::Edge;
        const bool freeHand = (c.handHold(0) < 0 && !c.handMoving(0)) || (c.handHold(1) < 0 && !c.handMoving(1));
        hint = edge       ? "{jump} pull yourself up onto the edge"
             : freeHand   ? "A hand let go: {move} aim and {reach.left} {reach.right} grab again, quick!"
             : c.staminaFraction() < 0.3f ? "Tired! Rest on two green holds or a ledge  ·  {letgo} let go"
                                          : "{move} aim  ·  {reach.left} {reach.right} reach  ·  hold and let go {grab.left} {grab.right} to lunge  ·  {help} how to play";
    } else {
        hint = "{move} walk to the rock  ·  {reach.left} {reach.right} grab it  ·  {help} how to play";
    }
    set(m_hud.hint, prompt(hint), "hint");
}

} // namespace climb_race
