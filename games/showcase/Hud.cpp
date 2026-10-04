// kke_demo's RmlUi HUD (ACTION_PLAN.md 1.4, issue #12) and its rows in the
// shared pause menu. The HUD says what the character is doing and, near a
// station, what to try there (live numbers where there are some). Esc or a
// controller's Start or Select opens the pause menu (kke::GameShellModule,
// whose look started as this game's own; KKE_MAIN_MENU=pause opens it at
// start, for screenshots): the game stops (unless it's online), the mouse
// is free, and the menu works with a controller too.

#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/DevTools.h"
#include "kke/modules/GameShellModule.h"
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
    c.Bind("menu_hint", &m_hud.menuHint);
    c.Bind("station_live", &m_hud.stationLive);
    c.Bind("panels", &m_hud.panels);
    c.Bind("online", &m_hud.online);
    c.Bind("players", &m_hud.players);
    c.Bind("prompt", &m_hud.prompt);
    c.Bind("toast", &m_hud.toast);
    c.Bind("ammo", &m_hud.ammo);
    c.Bind("crosshair", &m_hud.crosshair);
    c.Bind("reloading", &m_hud.reloading);
    m_hudModel = c.GetModelHandle();

    // Next to the executable (CMake copies ui/ there).
    const char* base = SDL_GetBasePath();
    const std::string root = std::string(base ? base : "") + "ui/";
    m_hudDoc = ctx->LoadDocument(root + "showcase_hud.rml");
    if (!m_hudDoc) {
        kke::log::get(name())->warn("HUD: could not load {}showcase_hud.rml", root);
        return;
    }
    m_hudDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

// This game's rows in the shared pause menu: back to the start, how many
// players share the screen, and the engine's panels.
void ShowcaseModule::buildPauseRows() {
    m_shell = m_app->getModule<kke::GameShellModule>();
    if (!m_shell) return;
    // Esc, Start or Select close the bag or the spawn menu first (onEvent).
    m_shell->blockPause = [this] { return m_invOpen || m_spawnOpen || m_mapOpen; };
    m_shell->selectIsTheGames = [] { return true; }; // View opens the bag; Start pauses
    m_shell->addPauseItem("World map", [this] {
        m_shell->closeMenu();
        openMap(true);
    });
    m_shell->addPauseItem("Reset the world", [this] {
        resetWorld();
        m_shell->closeMenu();
    });
    m_shell->pauseRows().choice("Players on this screen", &m_playersChoice, { "1", "2", "3", "4" },
                                [this] { setLocalPlayers(m_playersChoice + 1); });
    if (kke::dev::kEnabled)
        m_shell->pauseRows().toggle("Engine panels (F1)", &m_showPanels, [this] {
            for (kke::Module* p : m_panels) p->setUiVisible(m_showPanels);
        });
}

// Before the game's update, paused or not: follow the shared menu (it
// freed the mouse when it opened).
void ShowcaseModule::frameStart(const kke::UpdateContext&) {
    const bool open = m_shell && m_shell->menuOpen();
    if (open && !m_menuOpen) m_captured = false;
    m_menuOpen = open;
    m_playersChoice = static_cast<int>(m_locals.size());
}

namespace {
std::string metres(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f m/s", static_cast<double>(v));
    return buf;
}
bool within(const glm::vec3& p, const glm::vec3& c, float hx, float hz) { return std::abs(p.x - c.x) < hx && std::abs(p.z - c.z) < hz; }
} // namespace

void ShowcaseModule::updateHud() {
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
        text = "Shoot {fire} at the glass, the plank and the stone wall.";
    } else if (within(p, kSupply, 3.0f, 3.0f)) {
        station = "SUPPLY TABLE";
        text = "Pick things up {pickup}, then open your bag {inv.open} to equip them.";
    } else if (within(p, kCratePile, 3.5f, 3.5f)) {
        station = "CRATES";
        text = "Pick one up {pickup}, push them {interact} or shoot them over {fire}. More from the spawn menu {spawn.menu}.";
    } else if (p.x > kTrickX - 3.0f && p.x < 30.0f && p.z > -4.0f && p.z < 25.0f) {
        station = "TRICK COURSE";
        text = p.z > 10.0f ? "Sprint {sprint} beside the wall and jump {jump} to run along it. Jump again to kick off."
                           : "Jump {jump} to hang from a pillar. Jump with left or right leaps to the next; at the thin wall, jump leaps up.";
    } else if (std::abs(p.x - kLaneX) < 3.0f && p.z > 5.0f && p.z < 29.0f) {
        station = "PARKOUR LANE";
        text = "Run at it: vault the fences, climb the blocks, sprint for the high ledge.";
    } else if (within(p, kLowRoof, 2.5f, 2.5f)) {
        station = "LOW ROOF";
        text = "Crouch {crouch} to get under it.";
    } else if (within(p, kPlatform, 7.0f, 3.0f)) {
        station = "MOVING PLATFORM";
        text = "Stand on it: it carries you.";
    } else if (parkourStation(p, station, text)) {
        // A section of the parkour park (Parkour.cpp).
    } else if (const int zone = zoneAt(p); zone > 0) {
        station = kZones[zone].name;
        text = kZones[zone].text;
        if (zone == 2) { // the firing range
            text = armed() ? "Fire {fire}, aim {aim}. Hit the plates, break the glass and the walls, shoot a red barrel."
                           : "Pick up a gun from the bench {pickup}: it goes straight into your hand.";
            live = "Plates down: " + std::to_string(platesDown()) + " of " + std::to_string(plateCount());
        }
        if (zone == 5) {
            if (axeInHand()) text = "Swing the axe {fire} at a tree: four hits fell it. Swing at a log to split it into firewood.";
            live = "Trees felled: " + std::to_string(m_felled);
        }
    }

    if (m_ride != Ride::None) {
        // Riding: what it is, how to drive or fly it, the numbers.
        move = m_ride == Ride::Plane ? (m_plane.wreck >= 0.0f ? "Crashed" : m_plane.s.onGround ? "Taxiing" : "Flying") : "Driving";
        speedText.clear();
        if (m_ride == Ride::Car) {
            station = std::string(m_cars[static_cast<size_t>(m_rideCar)].name);
            for (char& ch : station) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            text = "Steer {move}, gas {drive.gas}, brake or reverse {drive.brake}, handbrake {jump}. Back on its wheels {reset}, get out {pickup}.";
        } else {
            station = "STUNT PLANE";
            text = m_plane.s.onGround ? "Throttle up {drive.gas} and roll: it lifts off by itself. Steer {move}, brakes {jump}, get out {pickup} once stopped."
                                      : "Stick {move} (forward: nose down), throttle {drive.gas} / {drive.brake}. Land on the runway or a flat field.";
        }
        live = rideHud();
    }
    auto set = [this](std::string& field, std::string value, const char* name) {
        if (field == value) return;
        field = std::move(value);
        m_hudModel.DirtyVariable(name);
    };
    set(m_hud.move, move, "move");
    set(m_hud.speed, speedText, "speed");
    set(m_hud.station, station, "station");
    // Station text is prompt text: {action} shows that button on the device in use.
    const kke::InputModule* input = m_app->getModule<kke::InputModule>();
    set(m_hud.stationText, input ? input->promptText(text) : text, "station_text");
    set(m_hud.stationLive, live, "station_live");
    // "Esc menu" on the keyboard, "(Start) menu" on a controller.
    const bool keys = !input || input->promptStyle() == kke::PromptStyle::Keyboard;
    set(m_hud.menuHint, keys ? "<span class=\"keycap\">Esc</span> menu" : input->promptText("{shell.pause} menu"), "menu_hint");
    // What pickup would do here.
    std::string prompt;
    if (!m_invOpen && !m_spawnOpen && m_held.body == kke::RigidWorld::kNoBody && st == State::Ground) {
        const int item = itemInReach();
        if (item >= 0)
            if (const kke::ItemDef* def = m_items.find(m_worldItems[static_cast<size_t>(item)].id)) {
                const int n = m_worldItems[static_cast<size_t>(item)].count;
                prompt = "{pickup} Pick up " + (n > 1 ? std::to_string(n) + " x " : std::string()) + def->name;
            }
        if (prompt.empty() && m_ride == Ride::None) {
            if (const int car = carInReach(); car >= 0) prompt = std::string("{pickup} Drive the ") + m_cars[static_cast<size_t>(car)].name;
            else if (planeInReach()) prompt = "{pickup} Fly the plane";
            else if (const int f = flowerInReach(); f >= 0) prompt = "{pickup} Pick the " + flowerName(f);
        }
    }
    set(m_hud.prompt, prompt.empty() || !input ? prompt : input->promptText(prompt), "prompt");
    // The gun: rounds in it and in the bag, or reloading; the crosshair.
    std::string ammo;
    if (const GunDef* gun = gunInHand()) {
        const auto loaded = m_loaded.find(gun->id);
        ammo = std::to_string(loaded == m_loaded.end() ? 0 : loaded->second) + " / " + std::to_string(m_inv.count(gun->ammo));
    } else if (grenadeInHand()) {
        ammo = "Grenades " + std::to_string(m_inv.count("grenade"));
    }
    set(m_hud.ammo, ammo, "ammo");
    const bool crosshair = armed() && m_ride == Ride::None && !m_invOpen && !m_spawnOpen && !m_mapOpen;
    if (m_hud.crosshair != crosshair) {
        m_hud.crosshair = crosshair;
        m_hudModel.DirtyVariable("crosshair");
    }
    if (const bool reloading = m_reloadLeft > 0.0f; m_hud.reloading != reloading) {
        m_hud.reloading = reloading;
        m_hudModel.DirtyVariable("reloading");
    }
    if (m_hud.trick != trick) {
        m_hud.trick = trick;
        m_hudModel.DirtyVariable("trick");
    }
}

} // namespace kke_showcase
