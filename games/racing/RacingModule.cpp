// Racing: the module's frame (see RacingModule.h). The physics tick
// drives every car (fixedUpdate: read what Jolt did, decide the pedals,
// hand them back); the frame does everything else: the race's clock and
// order, damage and effects, the cameras and the HUD. The rest is split by
// topic: Race.cpp (tracks, grid, laps), Driving.cpp (players, CPU drivers,
// cameras), Damage.cpp (dents, sparks, smoke, pits, drift), Lobby.cpp,
// Net.cpp and Hud.cpp.

#include "RacingModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/Viewports.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>


#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace racing {

namespace {

float envFloat(const char* name, float fallback) {
    const char* v = kke::dev::env(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}
bool envOn(const char* name) {
    const char* v = kke::dev::env(name);
    return v && *v == '1';
}

} // namespace

RacingModule::RacingModule() = default;
RacingModule::~RacingModule() = default;

std::vector<kke::ModuleDependency> RacingModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the cars (Jolt vehicles), the track's walls, flying debris" },
             { std::type_index(typeid(kke::InputModule)), true, "steering, pedals, gears: rebindable" },
             { std::type_index(typeid(kke::ModelModule)), true, "the cars (Synty's Street Racer pack, or block cars) and the track's props" },
             { std::type_index(typeid(kke::UiModule)), false, "the HUD: place, lap, speed, gear, damage, drift points" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join, pick a car, set up the race" },
             { std::type_index(typeid(kke::NetModule)), false, "online races: Host / Join in the start menu" },
             { std::type_index(typeid(kke::AudioModule)), false, "crashes and scrapes, the lights" },
#if KKE_ENABLE_FEMFX
             { std::type_index(typeid(kke::PhysicsModule)), false, "bodies that crumple for real (FEMFX): without it, dents by hand" },
#endif
    };
}

void RacingModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_net = app.getModule<kke::NetModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    m_audio = app.getModule<kke::AudioModule>();
#if KKE_ENABLE_FEMFX
    m_femfx = app.getModule<kke::PhysicsModule>();
#endif
    m_crumple = !(std::getenv("KKE_RACE_FEMFX") && *std::getenv("KKE_RACE_FEMFX") == '0');
    m_crumpleShove = envFloat("KKE_RACE_SHOVE", m_crumpleShove);

    m_autopilot = envOn("KKE_RACE_AUTOPILOT");
    m_crashTest = envOn("KKE_RACE_CRASH");
    // Benchmark scenarios by name, as the flying demo's KKE_FLY_BENCH.
    const char* bench = kke::dev::env("KKE_RACE_BENCH");
    m_pileup = envOn("KKE_RACE_PILEUP") || (bench && std::string(bench) == "pileup");
    m_quitAfter = envFloat("KKE_RACE_QUIT", -1.0f);
    m_defaultCars = std::clamp(static_cast<int>(envFloat("KKE_RACE_CARS", 12.0f)), 1, 24);
    m_forceLaps = static_cast<int>(envFloat("KKE_RACE_LAPS", -1.0f));
    m_forceDamage = static_cast<int>(envFloat("KKE_RACE_DAMAGE", -1.0f));
    m_cameraMode = std::clamp(static_cast<int>(envFloat("KKE_RACE_CAMERA", 0.0f)), 0, 4);
    if (m_pileup) {
        // The worst case: a full field, brutal damage, nobody at the wheel.
        m_autopilot = true;
        if (!kke::dev::env("KKE_RACE_CARS")) m_defaultCars = 24;
        if (m_forceDamage < 0) m_forceDamage = 2;
    }
    // How to play before the first race, unless nobody's there to read it.
    const char* intro = kke::dev::env("KKE_RACE_INTRO");
    m_howtoFirst = intro && *intro ? *intro == '1' : !(m_autopilot || m_quitAfter > 0.0f);

    // Controls, for each of four players. A controller: the left stick
    // steers, the triggers are the pedals, A the handbrake, B / X change
    // gear on the drag strip. The keyboard: WASD or the arrows, Space the
    // handbrake, E / Q gears.
    using IM = kke::InputModule;
    for (int p = 0; p < 4; ++p) {
        m_input->setPlayers(p + 1);
        kke::InputMap& in = m_input->map(p);
        in.defineAction({ "steer", "Steer", "Driving", "game", kke::ActionType::Axis1D });
        in.defineAction({ "throttle", "Accelerate", "Driving", "game", kke::ActionType::Axis1D });
        in.defineAction({ "brake", "Brake / reverse", "Driving", "game", kke::ActionType::Axis1D });
        in.defineAction({ "handbrake", "Handbrake (slide)", "Driving", "game" });
        in.defineAction({ "shift.up", "Gear up (drag race)", "Driving", "game" });
        in.defineAction({ "shift.down", "Gear down (drag race)", "Driving", "game" });
        in.defineAction({ "look.back", "Look back", "Driving", "game" });
        in.defineAction({ "reset.car", "Back on the track", "Driving", "game" });
        in.defineAction({ "camera", "Camera", "Driving", "game" });
        in.defineAction({ "race.again", "Race again", "Race", "game" });
        in.defineAction({ "race.new", "Next track", "Race", "game" });
        in.defineAction({ "menu", "Back to the menu (players, cars, track)", "Race", "game" });
        in.defineAction({ "help", "How to play", "Race", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        auto analog = [&](const char* action, SDL_GamepadAxis axis, int8_t half, float deadzone) {
            kke::Binding b = IM::bind(action, IM::padAxis(axis, half), kke::Trigger::Continuous);
            b.deadzone = deadzone;
            in.addBinding(b);
        };
        auto key = [&](const char* action, SDL_Scancode sc, float scale) {
            kke::Binding b = IM::bind(action, IM::key(sc), kke::Trigger::Continuous);
            b.scale = scale;
            in.addBinding(b);
        };
        analog("steer", SDL_GAMEPAD_AXIS_LEFTX, 0, 0.1f);
        analog("throttle", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1, 0.04f);
        analog("brake", SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1, 0.04f);
        key("steer", SDL_SCANCODE_A, -1.0f);
        key("steer", SDL_SCANCODE_LEFT, -1.0f);
        key("steer", SDL_SCANCODE_D, 1.0f);
        key("steer", SDL_SCANCODE_RIGHT, 1.0f);
        key("throttle", SDL_SCANCODE_W, 1.0f);
        key("throttle", SDL_SCANCODE_UP, 1.0f);
        key("brake", SDL_SCANCODE_S, 1.0f);
        key("brake", SDL_SCANCODE_DOWN, 1.0f);
        in.addBinding(IM::bind("handbrake", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH), kke::Trigger::Continuous));
        in.addBinding(IM::bind("handbrake", IM::key(SDL_SCANCODE_SPACE), kke::Trigger::Continuous));
        in.addBinding(IM::bind("shift.up", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("shift.up", IM::key(SDL_SCANCODE_E)));
        in.addBinding(IM::bind("shift.down", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        in.addBinding(IM::bind("shift.down", IM::key(SDL_SCANCODE_Q)));
        in.addBinding(IM::bind("look.back", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), kke::Trigger::Continuous));
        in.addBinding(IM::bind("look.back", IM::key(SDL_SCANCODE_B), kke::Trigger::Continuous));
        in.addBinding(IM::bind("reset.car", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
        in.addBinding(IM::bind("reset.car", IM::key(SDL_SCANCODE_T)));
        in.addBinding(IM::bind("camera", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
        in.addBinding(IM::bind("camera", IM::key(SDL_SCANCODE_C)));
        in.addBinding(IM::bind("race.again", IM::pad(SDL_GAMEPAD_BUTTON_START)));
        in.addBinding(IM::bind("race.again", IM::key(SDL_SCANCODE_R)));
        in.addBinding(IM::bind("race.new", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
        in.addBinding(IM::bind("race.new", IM::key(SDL_SCANCODE_N)));
        // Select (and Esc) is the pause menu (kke::GameShellModule), whose
        // Main menu comes back here; M stays a keyboard shortcut.
        in.addBinding(IM::bind("menu", IM::key(SDL_SCANCODE_M)));
        in.addBinding(IM::bind("help", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)));
        in.addBinding(IM::bind("help", IM::key(SDL_SCANCODE_H)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
    }
    m_input->setPlayers(1);
    // Touch: the stick steers; the pedals and the car's buttons on the right.
    kke::TouchLayoutOptions touch;
    touch.buttons = { "throttle", "brake", "handbrake", "reset.car", "camera", "look.back" };
    m_input->setTouchLayout(touch);
    m_input->commitDefaults();
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        // The pause menu's Main menu goes back to the start menu (the lobby);
        // Start is "race again" once the race is over or a car here is totalled.
        shell->onMainMenu = [this] {
            if (m_phase != Phase::Lobby) backToLobby();
        };
        shell->startIsTheGames = [this] {
            if (m_phase == Phase::Finished) return true;
            if (m_phase != Phase::Racing) return false;
            for (const Car& c : m_cars)
                if (c.seat >= 0 && c.totalled) return true;
            return false;
        };
    }

    // Synty's POLYGON Street Racer (cars and track props) and POLYGON
    // Nature (a rally stage's trees and rocks), from the asset folder
    // (assets/synty or KKE_ASSETS_DIR). Only those packs are scanned.
    {
        const char* base = SDL_GetBasePath();
        const std::string folder = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
        kke::CatalogScanOptions only;
        only.onlyPacks = { "POLYGON_Street_Racer", "POLYGON_Nature" };
        if (!folder.empty()) m_catalog = kke::AssetCatalog::scan(folder, only);
        m_garage = std::make_unique<CarGarage>(m_models, &m_catalog);
        if (!m_garage->hasPack())
            kke::log::get(name())->info("POLYGON Street Racer not found (assets/synty or KKE_ASSETS_DIR): block cars and a bare track");
    }
    m_fx = std::make_unique<kke::ParticleEffects>(app, 8000);

    app.camera().farPlane = 1400.0f;
    app.camera().nearPlane = 0.2f;
    loadTrackList();
    setupLobby();
    setupNet();
    buildTrack(m_tracks[static_cast<size_t>(chosenTrack())]);
    buildRace(wantedRoster());
    resetRace();
    m_phase = Phase::Lobby;
    buildHud();
    // Without the menu (or asked to skip it): straight into a race.
    if (!m_lobby || !m_lobby->isOpen()) startFromLobby();
}

void RacingModule::shutdown() {
    stopSounds();
    kke::RigidWorld& w = m_rigid->world();
    for (Car& c : m_cars) removeCar(c);
    m_cars.clear();
    for (Debris& d : m_debris) w.remove(d.body);
    m_debris.clear();
    for (kke::RigidWorld::BodyId b : m_trackBodies) w.remove(b);
    m_trackBodies.clear();
}

void RacingModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    if (!m_track) return;
    const float dt = ctx.fixedDt;
    for (Car& c : m_cars) {
        if (c.remote) {
            updateRemoteCar(c, dt);
            continue;
        }
        readCarState(c);
        wobbleWheels(c, dt);
        updateTrackPosition(c, dt);
        driveCar(c, dt);
    }
    updateShells();
    // The race's clock runs with the physics (a slow frame is the same race).
    if (m_phase != Phase::Lobby) updateRace(dt);
    std::fill(m_shift.begin(), m_shift.end(), 0);
    std::fill(m_resetAsked.begin(), m_resetAsked.end(), 0);
}

void RacingModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    kke::InputMap& p1 = m_input->map(0);
    if (p1.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    // Presses the physics tick acts on (it may run 0 or 2 times this frame).
    const int players = m_input->players();
    m_shift.resize(static_cast<size_t>(players), 0);
    m_resetAsked.resize(static_cast<size_t>(players), 0);
    bool again = false, fresh = false, menu = false, help = false, close = false, camera = false;
    for (int p = 0; p < players; ++p) {
        kke::InputMap& in = m_input->map(p);
        m_shift[static_cast<size_t>(p)] += (in.pressed("shift.up") ? 1 : 0) - (in.pressed("shift.down") ? 1 : 0);
        if (in.pressed("reset.car")) m_resetAsked[static_cast<size_t>(p)] = 1;
        again = again || in.pressed("race.again");
        fresh = fresh || in.pressed("race.new");
        menu = menu || in.pressed("menu");
        help = help || in.pressed("help");
        close = close || in.pressed("handbrake") || in.pressed("throttle");
        camera = camera || in.pressed("camera");
    }
    for (Car& c : m_cars)
        if (c.seat >= 0 && !c.remote) c.lookBack = m_input->map(c.player).held("look.back");
    if (camera) m_cameraMode = (m_cameraMode + 1) % 5;

    updateNet(dt);
    m_fx->update(dt, glm::vec3(1.2f, 0.0f, 0.6f));
    updateSounds(dt);
    if (m_phase == Phase::Lobby) {
        updateLobby(dt);
        for (Car& c : m_cars) placeInstances(c);
        updateHud(dt);
        return;
    }
    // How to play: help opens it; throttle, the handbrake or help again
    // closes it. The lights wait meanwhile (offline).
    if (m_howto) {
        m_howtoAge += dt;
        // On a touch screen a finger anywhere closes it too.
        int touchDevices = 0;
        bool finger = false;
        if (SDL_TouchID* ids = SDL_GetTouchDevices(&touchDevices)) {
            for (int d = 0; d < touchDevices && !finger; ++d) {
                int count = 0;
                if (SDL_Finger** f = SDL_GetTouchFingers(ids[d], &count)) {
                    finger = count > 0;
                    SDL_free(f);
                }
            }
            SDL_free(ids);
        }
        if (m_howtoAge > 0.3f && (help || close || finger)) showHowTo(false);
        again = fresh = menu = false;
    } else if (help) {
        showHowTo(true);
    }
    if (menu && m_lobby) {
        backToLobby();
        updateHud(dt);
        return;
    }
    if (netClient()) fresh = again = false; // online, the host starts each race
    if (fresh || again) {
        if (fresh && m_lobby) {
            // The next track in the menu's list.
            if (kke::Lobby::Option* o = m_lobby->lobby().option("track")) o->value = (o->value + 1) % static_cast<int>(o->choices.size());
        }
        if (fresh || m_rosterChanged) {
            startFromLobby();
        } else {
            resetRace();
        }
    }

    handleContacts();
    updateScrapes(dt);
    for (Car& c : m_cars) {
        updateEffects(c, dt);
        if (!c.remote) updateDrift(c, dt);
        c.noteTime = std::max(0.0f, c.noteTime - dt);
        c.hitCooldown = std::max(0.0f, c.hitCooldown - dt);
    }
    updateDebris(dt);
    updateLooseWheels(dt);
    m_skidRebuild -= dt;
    if (m_skidsChanged && m_skidRebuild <= 0.0f) rebuildSkids();
    for (Car& c : m_cars) placeInstances(c);
    updateTyreLooks();
    sendNet();

    // Cameras: player 1 is the engine's camera; split screen adds the
    // others' (one above the other: a wide view down the road each).
    std::vector<Car*> views;
    for (Car& c : m_cars)
        if (c.seat >= 0 && !c.remote) views.push_back(&c);
    std::sort(views.begin(), views.end(), [](const Car* a, const Car* b) { return a->player < b->player; });
    std::vector<kke::Application::View>& appViews = m_app->views();
    appViews.clear();
    if (views.empty()) {
        if (!m_cars.empty()) {
            // Nobody playing here (autopilot, or watching online): the leader.
            Car* lead = &m_cars[0];
            for (Car& c : m_cars)
                if (c.place == 1) lead = &c;
            updateCamera(*lead, dt, m_app->camera());
        }
    } else {
        const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(views.size()), views.size() > 2);
        for (size_t i = 0; i < views.size(); ++i) {
            kke::Camera& cam = cameraOf(*views[i]);
            if (&cam != &m_app->camera()) {
                cam.nearPlane = m_app->camera().nearPlane;
                cam.farPlane = m_app->camera().farPlane;
            }
            updateCamera(*views[i], dt, cam);
            if (views.size() > 1) appViews.push_back({ cam, rects[i] });
        }
        // Three players: the empty quarter is the TV camera on the leader.
        if (views.size() == 3) {
            const int saved = m_cameraMode;
            m_cameraMode = 3;
            Car* lead = &m_cars[0];
            for (Car& c : m_cars)
                if (c.place == 1) lead = &c;
            kke::Camera tv = m_tvCamera;
            updateCamera(*lead, dt, tv);
            m_cameraMode = saved;
            appViews.push_back({ tv, kke::splitScreen(4, true)[3] });
        }
    }
    updateHud(dt);

    m_clock += dt;
    if (m_quitAfter > 0.0f) {
        if (m_clock >= m_reportAt) {
            m_reportAt += 5.0f;
            kke::log::get(name())->info("t {:.0f} s: race clock {:.1f} s, physics {:.1f} s, {:.0f} fps{}", m_clock, m_raceClock,
                                        m_rigid->world().simulatedTime(), dt > 0.0f ? 1.0f / dt : 0.0f, crumpleReport());
            for (const Car& c : m_cars) {
                std::string tyres;
                for (const kke::VehicleWheelState& w : c.state.wheels)
                    tyres += fmt::format("{}{:.0f}{}", tyres.empty() ? "" : "/", w.surfaceTemp,
                                         w.condition == kke::TyreCondition::Inflated ? "" : w.condition == kke::TyreCondition::Flat ? "F" : w.condition == kke::TyreCondition::Rim ? "R" : "X");
                kke::log::get(name())->info("t {:.0f} s: P{} {} ({}) lap {} s {:.0f} u {:.1f}, {:.0f} km/h, gear {}, tyres {} C, health {:.0f}%{}{}{}", m_clock, c.place,
                                            c.name, carTypes()[static_cast<size_t>(c.type)].id, c.lap, c.where.s, c.where.u, carSpeed(c) * 3.6f, c.state.gear, tyres, c.health,
                                            c.totalled ? ", totalled" : "", c.finished ? ", finished" : "",
                                            event() == Event::Drift ? fmt::format(", drift {:.0f}", c.driftScore + c.driftChain) : std::string());
            }
        }
        if (m_clock >= m_quitAfter) {
            kke::log::get(name())->info("particles {} of {}, skid marks {}, debris {}", m_fx->count(), m_fx->capacity(), m_skids.size(),
                                        m_debris.size());
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void RacingModule::sound(const glm::vec3& at, uint32_t material, float intensity) {
    if (m_audio) m_audio->playImpact(at, material, std::clamp(intensity, 0.0f, 1.0f));
}

void RacingModule::tone(int earcon, float gain) {
    if (m_audio) m_audio->playEarcon(static_cast<kke::Earcon>(earcon), gain);
}

void RacingModule::render(const kke::RenderContext& ctx) {
    if (m_ground) m_ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    if (m_road) m_road->draw(ctx, glm::mat4(1.0f), 0.0f, 0.85f);
    if (m_markings) m_markings->draw(ctx, glm::mat4(1.0f), 0.0f, 0.7f);
    if (m_skidMesh && !m_skids.empty()) m_skidMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    if (m_walls) m_walls->draw(ctx, glm::mat4(1.0f), 0.0f, 0.6f);
    kke::RigidWorld& w = m_rigid->world();
    for (const Debris& d : m_debris)
        m_debrisMeshes[static_cast<size_t>(d.mesh)]->draw(ctx, w.transform(d.body) * glm::scale(glm::mat4(1.0f), d.half * 2.0f), 0.3f, 0.5f);
}

void RacingModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_walls) m_walls->drawShadow(ctx, glm::mat4(1.0f));
    kke::RigidWorld& w = m_rigid->world();
    for (const Debris& d : m_debris)
        m_debrisMeshes[static_cast<size_t>(d.mesh)]->drawShadow(ctx, w.transform(d.body) * glm::scale(glm::mat4(1.0f), d.half * 2.0f));
}

void RacingModule::renderTranslucent(const kke::RenderContext& ctx) { m_fx->draw(ctx); }

} // namespace racing
