// kke_demo's RmlUi HUD and pause menu (ACTION_PLAN.md 1.4, issue #12).
// The HUD says what the character is doing and, near a station, what to
// try there (live numbers where there are some). Esc or a controller's
// Start opens the pause menu (KKE_MENU=1: open at start, for screenshots): the game stops (unless it's online), the
// mouse is free, and the menu works with a controller too.

#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#endif

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace kke_showcase {

using namespace layout;

void ShowcaseModule::buildHud() {
    m_ui = m_app->getModule<kke::UiModule>();
    if (!m_ui || !m_ui->context()) return;
    Rml::Context* ctx = m_ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("demo");
    if (!c) return;
    c.Bind("move", &m_hud.move);
    c.Bind("speed", &m_hud.speed);
    c.Bind("trick", &m_hud.trick);
    c.Bind("station", &m_hud.station);
    c.Bind("station_text", &m_hud.stationText);
    c.Bind("station_live", &m_hud.stationLive);
    c.Bind("panels", &m_hud.panels);
    c.Bind("online", &m_hud.online);
    c.Bind("players", &m_hud.players);
    c.BindEventCallback("resume", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { setMenuOpen(false); });
    c.BindEventCallback("restart", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        resetCourse();
        m_loco->teleport(m_spawn);
        setMenuOpen(false);
    });
    c.BindEventCallback("set_players", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty()) return;
        setLocalPlayers(args[0].Get<int>());
        m_hud.players = static_cast<int>(m_locals.size()) + 1;
        h.DirtyVariable("players");
    });
    c.BindEventCallback("toggle_panels", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList&) {
        m_showPanels = !m_showPanels;
        for (kke::Module* p : m_panels) p->setUiVisible(m_showPanels);
        m_hud.panels = m_showPanels;
        h.DirtyVariable("panels");
    });
    c.BindEventCallback("quit", [](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    });
    m_hudModel = c.GetModelHandle();

    // Next to the executable (CMake copies ui/ there).
    const char* base = SDL_GetBasePath();
    const std::string root = std::string(base ? base : "") + "ui/";
    m_hudDoc = ctx->LoadDocument(root + "showcase_hud.rml");
    m_pauseDoc = ctx->LoadDocument(root + "showcase_pause.rml");
    if (!m_hudDoc || !m_pauseDoc) {
        kke::log::get(name())->warn("HUD: could not load {}showcase_hud.rml / showcase_pause.rml", root);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    if (const char* menu = std::getenv("KKE_MENU"); menu && *menu == '1') m_menuAt = 1.5f; // once the game is running
}

void ShowcaseModule::setMenuOpen(bool open) {
    if (open == m_menuOpen) return;
    m_menuOpen = open;
#if KKE_ENABLE_NET
    m_hud.online = m_net && m_net->connected();
#endif
    if (m_hudModel) {
        m_hud.players = static_cast<int>(m_locals.size()) + 1;
        m_hud.panels = m_showPanels;
        m_hudModel.DirtyVariable("online");
        m_hudModel.DirtyVariable("players");
        m_hudModel.DirtyVariable("panels");
    }
    if (open) {
        setCaptured(false);
        // Online the others play on: only an offline game stops.
        m_pausedByMenu = !m_hud.online && !m_app->isPaused();
        if (m_pausedByMenu) m_app->setPaused(true);
        if (m_pauseDoc) m_pauseDoc->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        if (m_pauseDoc)
            if (Rml::Element* first = m_pauseDoc->GetElementById("resume")) first->Focus(true);
    } else {
        if (m_pausedByMenu) m_app->setPaused(false);
        m_pausedByMenu = false;
        if (m_pauseDoc) m_pauseDoc->Hide();
    }
}

// Before the game's update, paused or not: the menu's own buttons.
void ShowcaseModule::frameStart(const kke::UpdateContext&) {
    if (!m_input) return;
    kke::InputMap& in = m_input->map(0);
    if (in.pressed("menu")) setMenuOpen(!m_menuOpen);
    else if (m_menuOpen && in.pressed("ui.back")) setMenuOpen(false);
}

namespace {
std::string metres(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f m/s", static_cast<double>(v));
    return buf;
}
bool within(const glm::vec3& p, const glm::vec3& c, float hx, float hz) { return std::abs(p.x - c.x) < hx && std::abs(p.z - c.z) < hz; }
} // namespace

void ShowcaseModule::updateHud(float dt) {
    if (m_menuAt > 0.0f && (m_menuAt -= dt) <= 0.0f) setMenuOpen(true);
    if (!m_hudModel || !m_loco) return;
    using State = kke::Locomotion::State;
    const State st = m_loco->state();
    const float speed = m_loco->groundSpeed();
    std::string move;
    switch (st) {
    case State::Ground:
        move = m_crouch ? "Crouching" : speed > 4.8f ? "Sprinting" : speed > 2.2f ? "Running" : speed > 0.2f ? "Walking" : "Standing";
        break;
    case State::Air: move = "In the air"; break;
    case State::Vault: move = "Vaulting"; break;
    case State::Climb: move = "Climbing"; break;
    case State::Hang: move = m_loco->shimmySpeed() != 0.0f ? "Shimmying" : "Hanging"; break;
    case State::Leap: move = "Leaping"; break;
    case State::WallRun: move = "Wall running"; break;
    }
    const bool trick = st == State::Vault || st == State::Climb || st == State::Hang || st == State::Leap || st == State::WallRun;
    std::string speedText = st == State::Hang ? "" : metres(speed);

    // The station you're at, and what to do there.
    const glm::vec3 p = m_rigid->world().characterPosition(m_player);
    std::string station, text, live;
    if (within(p, kLavaCenter, 5.0f, 5.0f)) {
        station = "LAVA";
        text = "Lava pours on a block and melts it. Push a crate in.";
        if (m_lava) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "%s block, %.0f%% left", m_lava->blockName(), static_cast<double>(m_lava->blockLeft() * 100.0f));
            live = buf;
            live[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(live[0])));
        }
    } else if (within(p, kPoolCenter, 7.0f, 7.0f)) {
        station = "POOL";
        text = "Vault the wall and wade in. Crates, planks and a raft float; steel sinks.";
    } else if (within(p, kYard, 7.0f, 6.0f)) {
        station = "BREAKING YARD";
        text = "Shoot (left click, RT) at the glass, the plank and the stone wall.";
    } else if (within(p, kCratePile, 3.5f, 3.5f)) {
        station = "CRATES";
        text = "Push them (E, right click) or shoot them over.";
    } else if (p.x > kTrickX - 3.0f && p.z > -4.0f && p.z < 25.0f) {
        station = "TRICK COURSE";
        text = p.z > 10.0f ? "Sprint beside the wall and jump to run along it. Jump again to kick off."
                           : "Jump to hang from a pillar. Jump with left or right leaps to the next; at the thin wall, jump leaps up.";
    } else if (std::abs(p.x - kLaneX) < 3.0f && p.z > 5.0f && p.z < 29.0f) {
        station = "PARKOUR LANE";
        text = "Run at it: vault the fences, climb the blocks, sprint for the high ledge.";
    } else if (within(p, kLowRoof, 2.5f, 2.5f)) {
        station = "LOW ROOF";
        text = "Crouch (C) to get under it.";
    } else if (within(p, kPlatform, 7.0f, 3.0f)) {
        station = "MOVING PLATFORM";
        text = "Stand on it: it carries you.";
    }

    auto set = [this](std::string& field, std::string value, const char* name) {
        if (field == value) return;
        field = std::move(value);
        m_hudModel.DirtyVariable(name);
    };
    set(m_hud.move, move, "move");
    set(m_hud.speed, speedText, "speed");
    set(m_hud.station, station, "station");
    set(m_hud.stationText, text, "station_text");
    set(m_hud.stationLive, live, "station_live");
    if (m_hud.trick != trick) {
        m_hud.trick = trick;
        m_hudModel.DirtyVariable("trick");
    }
}

} // namespace kke_showcase
