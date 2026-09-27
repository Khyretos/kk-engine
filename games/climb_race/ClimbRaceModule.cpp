#include "ClimbRaceModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <typeindex>

namespace climb_race {

namespace {

constexpr float kLaneX = 10.0f;     // each face's centre, left and right of x = 0
constexpr float kStartOut = 3.5f;   // m in front of the rock at the start line
constexpr float kFallRegrab = 0.35f;
constexpr float kRestRate = 30.0f;  // stamina per second standing on a ledge or the ground

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}
bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v == '1';
}

void appendMesh(const kke::ClimbMesh& m, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const uint32_t base = static_cast<uint32_t>(v.size());
    for (size_t i = 0; i < m.positions.size(); ++i) v.push_back({ m.positions[i], m.colors[i], m.normals[i], glm::vec2(0.0f) });
    for (uint32_t i : m.indices) idx.push_back(base + i);
}

// One box as 24 vertices (flat-shaded faces).
void appendBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (const glm::vec3& n : normals) {
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ center + (n + u * k.x + w * k.y) * half, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

float yawOf(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, -d.z)); }

} // namespace

ClimbRaceModule::ClimbRaceModule() = default;
ClimbRaceModule::~ClimbRaceModule() = default;

std::vector<kke::ModuleDependency> ClimbRaceModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the rock, the ledges and the climbers' bodies (Jolt)" },
             { std::type_index(typeid(kke::InputModule)), true, "the grab controls, rebindable" },
             { std::type_index(typeid(kke::ModelModule)), false, "the climbers' animated bodies" },
             { std::type_index(typeid(kke::UiModule)), false, "the HUD: stamina, clock, hands" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join, pick a look, set the CPU climbers" },
             { std::type_index(typeid(kke::NetModule)), false, "online races: Host / Join in the start menu" } };
}

void ClimbRaceModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_net = app.getModule<kke::NetModule>();
    m_lobby = app.getModule<kke::LobbyModule>();

    m_randomSeed = static_cast<uint32_t>(envFloat("KKE_CLIMB_SEED", 7.0f));
    m_autopilot = envOn("KKE_CLIMB_AUTOPILOT");
    m_botPause = envFloat("KKE_CLIMB_BOT_PAUSE", -1.0f);
    m_defaultCpus = std::clamp(static_cast<int>(envFloat("KKE_CLIMB_CPUS", 1.0f)), 0, kke::Lobby::kMaxCpus);
    m_quitAfter = envFloat("KKE_CLIMB_QUIT", -1.0f);
    if (envOn("KKE_CLIMB_ROCKFALL")) m_rockfall = 1.0f;
    m_climbCamera = envFloat("KKE_CLIMB_CLOSEUP", m_climbCamera);
    // How to play before the first race, unless nobody's there to read it.
    const char* intro = std::getenv("KKE_CLIMB_INTRO");
    m_howtoFirst = intro && *intro ? *intro == '1' : !(m_autopilot || m_quitAfter > 0.0f || m_rockfall >= 0.0f);

    // Controls: the usual character actions (move, look, jump, sprint),
    // and the four grab buttons. Left side of the pad (or the mouse's
    // left button, Q) is the left hand, right side the right hand.
    for (int p = 0; p < kke::Lobby::kMaxSeats; ++p) {
        m_input->setPlayers(p + 1);
        kke::InputMap& in = m_input->map(p);
        kke::InputModule::defineCharacterActions(in);
        // Those buttons are the hands here.
        for (const char* a : { "fire", "aim", "interact", "crouch" }) in.clearBindings(a);
        using IM = kke::InputModule;
        in.defineAction({ "grab.left", "Left hand: power (hold, let go to lunge)", "Climbing", "game", kke::ActionType::Axis1D });
        in.defineAction({ "grab.right", "Right hand: power (hold, let go to lunge)", "Climbing", "game", kke::ActionType::Axis1D });
        in.defineAction({ "reach.left", "Left hand: reach (with power held: quick)", "Climbing", "game" });
        in.defineAction({ "reach.right", "Right hand: reach (with power held: quick)", "Climbing", "game" });
        in.defineAction({ "letgo", "Let go of the rock", "Climbing", "game" });
        in.defineAction({ "race.again", "Race again", "Race", "game" });
        in.defineAction({ "race.new", "Next mountain", "Race", "game" });
        in.defineAction({ "menu", "Back to the menu (players, CPU climbers)", "Race", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        in.defineAction({ "help", "How to play", "Race", "game" });
        auto trigger = [&](const char* action, SDL_GamepadAxis axis) {
            kke::Binding b = IM::bind(action, IM::padAxis(axis, 1), kke::Trigger::Continuous);
            b.deadzone = 0.05f;
            in.addBinding(b);
        };
        trigger("grab.left", SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        trigger("grab.right", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        in.addBinding(IM::bind("grab.left", IM::mouse(SDL_BUTTON_LEFT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("grab.right", IM::mouse(SDL_BUTTON_RIGHT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("reach.left", IM::pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)));
        in.addBinding(IM::bind("reach.right", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
        in.addBinding(IM::bind("reach.left", IM::key(SDL_SCANCODE_Q)));
        in.addBinding(IM::bind("reach.right", IM::key(SDL_SCANCODE_E)));
        in.addBinding(IM::bind("letgo", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("letgo", IM::key(SDL_SCANCODE_C)));
        in.addBinding(IM::bind("race.again", IM::pad(SDL_GAMEPAD_BUTTON_START)));
        in.addBinding(IM::bind("race.again", IM::key(SDL_SCANCODE_R)));
        in.addBinding(IM::bind("race.new", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
        in.addBinding(IM::bind("race.new", IM::key(SDL_SCANCODE_N)));
        in.addBinding(IM::bind("menu", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));
        in.addBinding(IM::bind("menu", IM::key(SDL_SCANCODE_M)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
        in.addBinding(IM::bind("help", IM::key(SDL_SCANCODE_H)));
        in.addBinding(IM::bind("help", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
    }
    m_input->setPlayers(1);
    m_input->commitDefaults();

    // The unit cube markers: left hand, right hand, out of reach, charging.
    const glm::vec3 colors[4] = { { 0.2f, 0.95f, 1.0f }, { 1.0f, 0.3f, 0.85f }, { 1.0f, 0.2f, 0.15f }, { 1.0f, 0.85f, 0.2f } };
    for (int i = 0; i < 4; ++i) {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendBox(glm::vec3(0.0f), glm::vec3(0.5f), colors[i], v, idx);
        m_markers[i] = std::make_unique<kke::DynamicMeshRenderer>(app);
        m_markers[i]->upload(v, idx);
    }
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendBox({ 0.0f, 0.9f, 0.0f }, { 0.28f, 0.9f, 0.2f }, { 0.85f, 0.85f, 0.9f }, v, idx);
        appendBox({ 0.0f, 1.55f, -0.2f }, { 0.2f, 0.08f, 0.03f }, { 0.1f, 0.1f, 0.15f }, v, idx);
        m_capsule = std::make_unique<kke::DynamicMeshRenderer>(app);
        m_capsule->upload(v, idx);
    }

    app.camera().farPlane = 250.0f;
    app.window().setQuitOnEscape(false); // Esc frees the mouse
    loadCharacter();
    buildScenery();
    loadMountainList();
    loadProgress();
    setupLobby(); // the Mountain row, and KKE_CLIMB_MOUNTAIN / KKE_CLIMB_SEED
    setupNet();
    const std::vector<Entry> roster = wantedRoster();
    useMountain(chosenMountain());
    buildMountain(static_cast<int>(roster.size()));
    buildRacers(roster);
    buildHud();
    // Without the menu (or asked to skip it): straight into a race.
    if (!m_lobby || !m_lobby->isOpen()) startFromLobby();
}

void ClimbRaceModule::buildScenery() {
    kke::RigidWorld& w = m_rigid->world();
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    // The meadow in front of the mountain.
    const glm::vec3 groundHalf(130.0f, 0.5f, 60.0f), groundCenter(0.0f, -0.5f, 20.0f);
    appendBox(groundCenter, groundHalf, { 0.3f, 0.42f, 0.24f }, v, idx);
    kke::RigidWorld::BodyDesc d;
    d.motion = kke::RigidWorld::Motion::Static;
    d.halfExtents = groundHalf;
    d.position = groundCenter;
    m_scenery.push_back(w.add(d));
    m_ground = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_ground->upload(v, idx);
}

void ClimbRaceModule::clearMountain() {
    kke::RigidWorld& w = m_rigid->world();
    for (auto& lane : m_lanes) {
        for (kke::RigidWorld::BodyId b : lane->bodies) w.remove(b);
        for (Loose& l : lane->loose) w.remove(l.body);
        // Frames still in flight draw its meshes: they go once those are done.
        m_app->renderer().retire(std::move(lane));
    }
    m_lanes.clear();
}

void ClimbRaceModule::loadMountainList() {
    const char* base = SDL_GetBasePath();
    const std::filesystem::path folder = std::filesystem::path(base ? base : "") / "mountains";
    std::vector<std::string> problems;
    m_mountains = loadMountains(folder, problems);
    // A mountain file with a mistake in it still loads; say what's wrong.
    for (const std::string& p : problems) kke::log::get(name())->warn("mountains: {}", p);
    kke::log::get(name())->info("{} mountains in {}", m_mountains.size(), folder.generic_string());
}

void ClimbRaceModule::useMountain(const Mountain& m) {
    m_mountain = m;
    if (!m.mood.empty() && m.mood != m_builtMood) {
        m_builtMood = m.mood;
        m_app->setMood(m.mood);
    }
}

void ClimbRaceModule::buildMountain(int lanes) {
    clearMountain();
    const kke::ClimbWallDesc& desc = m_mountain.desc;
    const uint32_t seed = desc.seed;
    kke::RigidWorld& w = m_rigid->world();
    const kke::ClimbWall generated = kke::ClimbWall::generate(desc);
    m_builtKey = keyOf(m_mountain);
    kke::log::get(name())->info("mountain {} (seed {}): {} m, {} holds per face, {} ledges, {} faces", m_mountain.name, seed,
                                generated.summitY(), generated.holds().size(), generated.ledges().size(), std::max(lanes, 1));
    lanes = std::max(lanes, 1);
    const glm::vec3 flags[6] = { { 0.2f, 0.6f, 1.0f }, { 1.0f, 0.5f, 0.1f }, { 0.4f, 0.85f, 0.3f }, { 0.8f, 0.4f, 1.0f }, { 1.0f, 0.85f, 0.2f }, { 1.0f, 0.4f, 0.55f } };
    for (int li = 0; li < lanes; ++li) {
        auto lane = std::make_unique<Lane>();
        lane->wall = std::make_unique<kke::ClimbWall>(generated);
        // Side by side, 20 m apart, centred on x = 0 (two faces: -10 and 10).
        lane->offset = glm::vec3((static_cast<float>(li) - static_cast<float>(lanes - 1) * 0.5f) * 2.0f * kLaneX, 0.0f, 0.0f);
        const kke::ClimbWall& wall = *lane->wall;
        kke::ClimbMesh rock = wall.buildMesh();

        // The rock (with every fixed hold on it) as one static mesh.
        kke::RigidWorld::BodyDesc body;
        body.shape = kke::RigidWorld::Shape::Mesh;
        body.motion = kke::RigidWorld::Motion::Static;
        body.points = rock.positions;
        body.indices = rock.indices;
        body.position = lane->offset;
        body.friction = 0.9f;
        lane->bodies.push_back(w.add(body));

        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        appendMesh(rock, v, idx);
        // Ledges: boxes in the rock, to stand and rest on.
        for (const kke::ClimbLedge& l : wall.ledges()) {
            appendBox(l.center, l.halfExtents, { 0.5f, 0.46f, 0.4f }, v, idx);
            kke::RigidWorld::BodyDesc b;
            b.motion = kke::RigidWorld::Motion::Static;
            b.halfExtents = l.halfExtents;
            b.position = l.center + lane->offset;
            lane->bodies.push_back(w.add(b));
        }
        // The start line and the summit flag.
        const float x0 = wall.holds()[static_cast<size_t>(wall.line().front())].position.x;
        appendBox({ 0.0f, 0.01f, wall.surfaceZ(0.0f, 0.5f) + kStartOut }, { 6.0f, 0.02f, 0.08f }, { 0.95f, 0.95f, 0.9f }, v, idx);
        const glm::vec3 top(x0 * 0.3f, wall.summitY(), wall.summitZ() - 2.0f);
        appendBox(top + glm::vec3(0.0f, 1.5f, 0.0f), { 0.05f, 1.5f, 0.05f }, { 0.8f, 0.8f, 0.8f }, v, idx);
        appendBox(top + glm::vec3(0.45f, 2.7f, 0.0f), { 0.45f, 0.28f, 0.02f }, flags[li % 6], v, idx);
        // To the next face: a dark gully at the back, so there's no sky
        // between them (the faces' own sides are rock).
        const float gully = (wall.summitY() + 1.0f) * 0.5f; // up to just over the summit
        if (li + 1 < lanes) appendBox({ kLaneX, gully, -15.0f }, { kLaneX - 7.4f, gully, 3.5f }, { 0.22f, 0.2f, 0.19f }, v, idx);
        lane->mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        lane->mesh->upload(v, idx);

        // Loose holds: their own bodies, kinematic until they break.
        for (size_t h = 0; h < wall.holds().size(); ++h) {
            const kke::ClimbHold& hold = wall.holds()[h];
            if (!hold.loose) continue;
            kke::ClimbMesh m;
            kke::ClimbWall::appendHold(hold, seed, hold.position, m);
            if (m.positions.size() < 4) continue;
            Loose l;
            l.hold = static_cast<int>(h);
            kke::RigidWorld::BodyDesc b;
            b.shape = kke::RigidWorld::Shape::ConvexHull;
            b.motion = kke::RigidWorld::Motion::Kinematic;
            b.points = m.positions;
            b.position = hold.position + lane->offset;
            b.density = 2600.0f;
            b.restitution = 0.25f;
            l.body = w.add(b);
            std::vector<kke::Vertex> hv;
            std::vector<uint32_t> hi;
            appendMesh(m, hv, hi);
            l.mesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
            l.mesh->upload(hv, hi);
            lane->loose.push_back(std::move(l));
        }
        m_lanes.push_back(std::move(lane));
    }
    // New walls: every climber holds on to the old one, so they start again.
    for (size_t i = 0; i < m_racers.size(); ++i) {
        Racer& r = m_racers[i];
        r.lane = std::min(static_cast<int>(i), lanes - 1);
        r.climber = makeClimber(r.lane);
        makeBrain(r);
    }
}

void ClimbRaceModule::resetRace() {
    ++m_round;
    if (!netClient()) m_mode = chosenMode(); // online, the host's (applySetup)
    m_phase = Phase::Countdown;
    m_countdown = 3.0f;
    m_winner.clear();
    m_opened.clear();
    // Loose holds back on the rock.
    kke::RigidWorld& w = m_rigid->world();
    for (auto& lane : m_lanes)
        for (Loose& l : lane->loose) {
            if (!l.fallen) continue;
            w.setMotion(l.body, kke::RigidWorld::Motion::Kinematic);
            w.setTransform(l.body, lane->wall->holds()[static_cast<size_t>(l.hold)].position + lane->offset, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            w.setVelocity(l.body, glm::vec3(0.0f));
            w.setAngularVelocity(l.body, glm::vec3(0.0f));
            l.fallen = false;
        }
    for (Racer& r : m_racers) {
        const kke::ClimbWall& wall = *m_lanes[static_cast<size_t>(r.lane)]->wall;
        // At the start line, in front of the first hold of the line.
        const float x = wall.holds()[static_cast<size_t>(wall.line().front())].position.x;
        const glm::vec3 feet(x, 0.05f, wall.surfaceZ(x, 0.5f) + kStartOut);
        r.climber = makeClimber(r.lane);
        makeBrain(r);
        r.loco->teleport(toWorld(r, feet));
        r.loco->setFacing(glm::vec3(0, 0, -1));
        r.rig.yaw = 0.0f;
        r.rig.pitch = 8.0f;
        r.time = 0.0f;
        r.finished = false;
        r.regrab = 0.0f;
        r.falls = 0;
        r.medal = -1;
        r.newBest = false;
        r.wasClimbing = false;
    }
    startMode();
}

// The CPU climbers' brain, from their difficulty: how long they breathe
// between moves, whether they lunge, and how tired they let themselves get.
void ClimbRaceModule::makeBrain(Racer& r) {
    if (!r.bot) {
        r.brain.reset();
        return;
    }
    r.brain = std::make_unique<kke::ClimbBot>(m_lanes[static_cast<size_t>(r.lane)]->wall->line());
    kke::ClimbBot& b = *r.brain;
    switch (std::clamp(r.difficulty, 0, 3)) {
    case 0: // Easy: slow, never lunges, rests early and long
        b.pause = 1.0f;
        b.lunges = false;
        b.restBelow = 0.55f;
        b.restUntil = 0.97f;
        break;
    case 1: // Normal
        b.pause = 0.6f;
        break;
    case 2: // Hard
        b.pause = 0.42f;
        break;
    default: // Expert: quick, and pushes on tired
        b.pause = 0.26f;
        b.restBelow = 0.35f;
        b.restUntil = 0.8f;
        break;
    }
    if (m_botPause >= 0.0f) b.pause = m_botPause;
    r.pause = b.pause;
}

void ClimbRaceModule::showHowTo(bool on) {
    m_howto = on;
    m_howtoAge = 0.0f;
    // Closing it doesn't also jump: the press that closed it is used up.
    for (Racer& r : m_racers) r.jumpQueued = false;
    if (on && m_captured) {
        m_captured = false;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    }
    m_hud.howto = on;
    if (m_hudModel) m_hudModel.DirtyVariable("howto");
}

void ClimbRaceModule::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && m_phase != Phase::Lobby && !m_app->uiCapturesMouse() &&
        e.button.button == SDL_BUTTON_LEFT) {
        m_captured = true;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), true);
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE) {
        m_captured = false;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    }
}

ClimbRaceModule::RacerInput ClimbRaceModule::readPlayer(Racer& r, float dt) {
    RacerInput ri;
    kke::InputMap& in = m_input->map(r.player);
    const glm::vec2 move = in.axis2("move");
    // Look: the mouse while it's captured (the keyboard player), a stick any time.
    if (r.mouse && m_captured) ri.look += in.axis2("look") * m_mouseSensitivity;
    const glm::vec2 rate = in.axis2("look.rate");
    ri.look += glm::vec2(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);

    ri.loco.move = r.rig.forward() * move.y + r.rig.right() * move.x;
    ri.loco.move.y = 0.0f;
    if (glm::length(ri.loco.move) > 1e-3f) ri.loco.move = glm::normalize(ri.loco.move) * std::min(1.0f, glm::length(move));
    ri.loco.fast = in.held("sprint");
    ri.loco.slow = in.held("walk");
    if (in.pressed("jump")) r.jumpQueued = true;
    ri.loco.goUp = r.jumpQueued;
    r.jumpQueued = false;

    // On the rock the stick aims (x right, y up), the four buttons grab.
    ri.climb.aim = move;
    ri.climb.reach[0] = in.pressed("reach.left");
    ri.climb.reach[1] = in.pressed("reach.right");
    ri.climb.power[0] = in.axis("grab.left");
    ri.climb.power[1] = in.axis("grab.right");
    ri.climb.letGo = in.pressed("letgo");
    ri.mantle = ri.loco.goUp;
    // Mouse players aim with the crosshair when WASD is let go.
    if (r.mouse && m_captured && glm::length(move) < 0.1f && r.crosshair >= 0) ri.climb.pick[0] = ri.climb.pick[1] = r.crosshair;
    ri.grab = ri.climb.reach[0] || ri.climb.reach[1] || ri.climb.power[0] > 0.3f || ri.climb.power[1] > 0.3f;
    return ri;
}

// The bot: kke::ClimbBot on the rock; on the ground it walks to the foot
// of its line and grabs; on a ledge it rests, then goes on.
ClimbRaceModule::RacerInput ClimbRaceModule::readBot(Racer& r, float dt) {
    RacerInput ri;
    kke::Climber& c = *r.climber;
    if (c.climbing()) {
        ri.climb = r.brain->think(c, dt); // mantles by itself (up + reach)
        return ri;
    }
    const kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 feet = toWall(r, w.characterPosition(r.id));
    const kke::ClimbWall& wall = c.wall();
    const bool onLedge = feet.y > 1.0f;
    if (onLedge) {
        // Rest until fresh, then grab what's above.
        r.restTimer += dt;
        if (c.staminaFraction() >= r.brain->restUntil && r.restTimer > 0.6f) ri.grab = true;
        return ri;
    }
    r.restTimer = 0.0f;
    // Walk to the foot of the line, face the rock, grab.
    const glm::vec3 first = wall.holds()[static_cast<size_t>(r.brain->route().front())].position;
    const glm::vec3 spot(first.x, 0.0f, wall.surfaceZ(first.x, 0.5f) + 0.45f);
    glm::vec3 to = spot - feet;
    to.y = 0.0f;
    if (glm::length(to) > 0.25f) {
        ri.loco.move = glm::normalize(to) * std::min(1.0f, glm::length(to));
        ri.loco.fast = glm::length(to) > 3.0f;
    } else {
        r.loco->setFacing(glm::vec3(0, 0, -1));
        ri.grab = true;
    }
    return ri;
}

int ClimbRaceModule::crosshairHold(const Racer& r, const kke::Camera& cam, bool& outOfReach) const {
    outOfReach = false;
    const kke::ClimbWall& wall = *m_lanes[static_cast<size_t>(r.lane)]->wall;
    const glm::vec3 origin = toWall(r, cam.position);
    const glm::vec3 dir = glm::normalize(cam.target - cam.position);
    int best = -1;
    float bestD = 0.3f;
    for (size_t i = 0; i < wall.holds().size(); ++i) {
        if (r.climber->holdGone(static_cast<int>(i))) continue;
        const glm::vec3 p = wall.holds()[i].position - origin;
        const float along = glm::dot(p, dir);
        if (along < 0.5f || along > 14.0f) continue;
        const float off = glm::length(p - dir * along) / (1.0f + along * 0.04f);
        if (off < bestD) {
            bestD = off;
            best = static_cast<int>(i);
        }
    }
    if (best >= 0 && r.climber->climbing()) {
        const kke::Climber& c = *r.climber;
        outOfReach = true;
        for (int h = 0; h < 2; ++h) {
            const int other = c.handHold(1 - h);
            if (other >= 0 && best != other &&
                kke::ClimbWall::reachDistance(wall.holds()[static_cast<size_t>(best)].position, wall.holds()[static_cast<size_t>(other)].position) <=
                    c.reachNow(h))
                outOfReach = false;
        }
    }
    return best;
}

void ClimbRaceModule::dropLoose(Lane& lane, int hold, const glm::vec3& push) {
    for (Loose& l : lane.loose) {
        if (l.hold != hold || l.fallen) continue;
        kke::RigidWorld& w = m_rigid->world();
        w.setMotion(l.body, kke::RigidWorld::Motion::Dynamic);
        w.setVelocity(l.body, push + glm::vec3(0.0f, 0.5f, 0.0f));
        w.setAngularVelocity(l.body, glm::vec3(4.0f, 1.5f, -2.5f));
        l.fallen = true;
        kke::log::get(name())->info("a loose hold came off at {:.1f} m", lane.wall->holds()[static_cast<size_t>(hold)].position.y);
    }
}

void ClimbRaceModule::updateRacer(Racer& r, float dt) {
    kke::RigidWorld& w = m_rigid->world();
    kke::Climber& c = *r.climber;
    RacerInput ri = r.bot ? readBot(r, dt) : readPlayer(r, dt);
    if (!r.bot) r.rig.addLook(ri.look.x, ri.look.y);
    r.idleLook = glm::length(ri.look) > 0.05f ? 0.0f : r.idleLook + dt;
    const bool racing = m_phase == Phase::Racing && !isDone(r);
    if (!racing) {
        ri = RacerInput{};
        ri.look = glm::vec2(0.0f);
    }
    if (r.out) ri.climb.letGo = true; // Elimination: off the rock, and watch
    r.regrab = std::max(0.0f, r.regrab - dt);

    if (c.climbing()) {
        // Jump with both hands on an edge: over it.
        if (ri.mantle && c.handHold(0) >= 0 && c.handHold(1) >= 0) {
            ri.climb.aim = glm::vec2(0.0f, 1.0f);
            ri.climb.reach[0] = true;
        }
        c.update(ri.climb, dt);
        if (c.brokeHold() >= 0) {
            dropLoose(*m_lanes[static_cast<size_t>(r.lane)], c.brokeHold(), c.facing() * -1.5f);
            netLoose(r.lane, c.brokeHold(), c.facing() * -1.5f);
        }
        switch (c.state()) {
        case kke::Climber::State::Climbing:
        case kke::Climber::State::Mantle:
            w.moveCharacter(r.id, toWorld(r, c.feet()));
            break;
        case kke::Climber::State::Fell: {
            // Off the rock: Locomotion has the body again, falling.
            const glm::vec3 feet = toWorld(r, c.feet());
            r.loco->teleport(feet);
            r.loco->setFacing(c.facing());
            w.setCharacterVelocity(r.id, -c.facing() * 1.2f + glm::vec3(0.0f, -0.5f, 0.0f));
            r.regrab = kFallRegrab;
            r.fallStartY = feet.y;
            ++r.falls;
            kke::log::get(name())->info("{} fell from {:.1f} m", r.name, feet.y);
            break;
        }
        case kke::Climber::State::Topped:
            r.loco->teleport(toWorld(r, c.mantleFeet()));
            r.loco->setFacing(c.facing());
            if (c.mantleLedge() < 0 && !r.finished) {
                r.finished = true;
                if (m_winner.empty()) m_winner = r.name;
                kke::log::get(name())->info("{} topped out in {:.2f} s", r.name, r.time);
                netFinished(r);
                recordFinish(r);
            }
            break;
        case kke::Climber::State::Off: break;
        }
    } else {
        r.loco->update(ri.loco, dt);
        const glm::vec3 feet = w.characterPosition(r.id);
        // Standing (ground or a ledge): the arms come back.
        if (r.loco->state() == kke::Locomotion::State::Ground) c.recover(kRestRate, dt);
        if (racing && ri.grab && r.regrab <= 0.0f) {
            const kke::Locomotion::State ls = r.loco->state();
            if (ls == kke::Locomotion::State::Ground || ls == kke::Locomotion::State::Air || ls == kke::Locomotion::State::Hang) {
                if (c.start(toWall(r, feet))) {
                    w.setCharacterKinematic(r.id, true);
                    r.restTimer = 0.0f;
                }
            }
        }
        if (feet.y < -10.0f) resetRace(); // off the world: shouldn't happen
    }
    if (racing) r.time += dt;
}

void ClimbRaceModule::updateCamera(Racer& r, float dt, kke::Camera& out) {
    kke::RigidWorld& w = m_rigid->world();
    const bool climbing = r.climber->climbing();
    // On the rock the camera settles behind you, looking a little up the
    // face (where the next holds are), unless you're looking around.
    if (climbing && r.idleLook > 0.8f) {
        const float target = yawOf(r.climber->facing());
        const float diff = kke::Locomotion::angleBetween(r.rig.yaw, target);
        r.rig.yaw += diff * (1.0f - std::exp(-2.0f * dt));
        r.rig.pitch += (12.0f - r.rig.pitch) * (1.0f - std::exp(-2.0f * dt));
    }
    r.rig.settings.armLength += ((climbing ? m_climbCamera : 4.0f) - r.rig.settings.armLength) * (1.0f - std::exp(-3.0f * dt));
    r.rig.settings.shoulderOffset = climbing ? 0.0f : 0.45f;
    const glm::vec3 feet = w.characterPosition(r.id);
    r.rig.update(dt, feet, [&w](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = w.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    }, out);
}

void ClimbRaceModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    kke::InputMap& p1 = m_input->map(0);
    if (p1.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    updateNet(dt);
    if (m_phase == Phase::Lobby) {
        updateLobby(dt);
        updateHud(dt);
        return;
    }
    // Any player: race again, the next mountain, the menu, or how to play.
    bool again = false, fresh = false, menu = false, help = false, start = false;
    for (int p = 0; p < m_input->players(); ++p) {
        kke::InputMap& in = m_input->map(p);
        again = again || in.pressed("race.again");
        fresh = fresh || in.pressed("race.new");
        menu = menu || in.pressed("menu");
        help = help || in.pressed("help");
        start = start || in.pressed("jump");
    }
    // How to play: help opens it; jump (or help again) closes it. The race
    // stands still meanwhile, and the climbers breathe.
    if (m_howto) {
        m_howtoAge += dt;
        if (m_howtoAge > 0.3f && (help || start)) showHowTo(false);
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
        // Someone joined during the race: they're in this one.
        if (m_rosterChanged) {
            m_rosterChanged = false;
            startFromLobby();
            if (fresh) {
                nextMountain();
                buildMountain(static_cast<int>(m_racers.size()));
                resetRace();
            }
        } else {
            if (fresh) {
                nextMountain();
                buildMountain(static_cast<int>(m_racers.size()));
            }
            resetRace();
        }
    }

    // Online the race can't wait for one screen's how-to page: only the
    // other machines' "ready" holds it (m_netHold).
    const bool stopped = m_howto && !(m_net && m_net->connected());
    if (m_phase == Phase::Countdown && !stopped) {
        if (!m_netHold) m_countdown -= dt; // online: until every machine is at the line
        if (m_countdown <= 0.0f) m_phase = Phase::Racing;
    }
    // The crosshair's hold for mouse aiming (last frame's camera).
    for (Racer& r : m_racers) r.crosshair = r.mouse && m_captured ? crosshairHold(r, cameraOf(r), r.crosshairOut) : -1;
    if (!stopped) {
        for (Racer& r : m_racers)
            if (!r.remote) updateRacer(r, dt);
        updateMode(dt);
    }
    if (m_phase == Phase::Racing && !stopped) {
        // Every player at the top (or everyone, in a race of bots): results.
        bool playersDone = true, allDone = true, anyPlayer = false;
        for (const Racer& r : m_racers) {
            allDone = allDone && isDone(r);
            if (r.seat >= 0 && !r.bot) {
                anyPlayer = true;
                playersDone = playersDone && isDone(r);
            }
        }
        // Elimination goes on until one is left (updateMode), players out or not.
        if (allDone || (anyPlayer && playersDone && m_mode != Mode::Elimination)) m_phase = Phase::Finished;
    }

    for (Racer& r : m_racers) animateBody(r, dt);
    sendNet();
    // Cameras: player 1 is the engine's camera; split screen adds the others'.
    std::vector<Racer*> views;
    for (Racer& r : m_racers)
        if (r.seat >= 0) views.push_back(&r);
    std::sort(views.begin(), views.end(), [](const Racer* a, const Racer* b) { return a->player < b->player; });
    std::vector<kke::Application::View>& appViews = m_app->views();
    appViews.clear();
    if (views.empty()) {
        updateCamera(m_racers[0], dt, m_app->camera());
    } else {
        const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(views.size()), true);
        for (size_t i = 0; i < views.size(); ++i) {
            kke::Camera& cam = cameraOf(*views[i]);
            if (&cam != &m_app->camera()) {
                cam.fovDegrees = m_app->camera().fovDegrees;
                cam.nearPlane = m_app->camera().nearPlane;
                cam.farPlane = m_app->camera().farPlane;
            }
            updateCamera(*views[i], dt, cam);
            if (views.size() > 1) appViews.push_back({ cam, rects[i] });
        }
        // Three players: the empty quarter watches the whole mountain,
        // rising with the leader.
        if (views.size() == 3) {
            float leader = 0.0f;
            for (const Racer& r : m_racers) leader = std::max(leader, m_rigid->world().characterPosition(r.id).y);
            const float span = 2.0f * kLaneX * static_cast<float>(m_lanes.size());
            kke::Camera& wide = m_overview;
            wide.fovDegrees = m_app->camera().fovDegrees;
            wide.nearPlane = 0.5f;
            wide.farPlane = m_app->camera().farPlane;
            const float lift = std::max(8.0f, leader * 0.8f + 6.0f);
            wide.position = glm::vec3(0.0f, lift + 4.0f, std::max(32.0f, span * 0.4f + 8.0f));
            wide.target = glm::vec3(0.0f, lift, 0.0f);
            appViews.push_back({ wide, kke::splitScreen(4, true)[3] });
        }
    }
    updateHud(dt);
    updateRockfall(dt);

    m_clock += dt;
    if (m_quitAfter > 0.0f) {
        if (m_clock >= m_reportAt) {
            m_reportAt += 5.0f;
            for (const Racer& r : m_racers)
                kke::log::get(name())->info("t {:.0f} s: {} at {:.1f} m, stamina {:.0f}%, {}", m_clock, r.name,
                                            m_rigid->world().characterPosition(r.id).y, r.climber->staminaFraction() * 100.0f,
                                            r.finished ? "finished" : bodyInput(r).climbing ? "climbing" : "on foot");
        }
        if (m_clock >= m_quitAfter) {
            for (const Racer& r : m_racers)
                if (r.gripError.samples > 0)
                    kke::log::get(name())->info("{}: hands on their holds within {:.1f} cm on average, {:.1f} cm at worst ({} samples)", r.name,
                                                r.gripError.sum / static_cast<float>(r.gripError.samples) * 100.0f, r.gripError.worst * 100.0f,
                                                r.gripError.samples);
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void ClimbRaceModule::updateRockfall(float dt) {
    if (m_rockfall < 0.0f) return;
    const float before = m_rockfall;
    m_rockfall += dt;
    Lane& lane = *m_lanes[0];
    if (before < 2.0f && m_rockfall >= 2.0f)
        for (const Loose& l : lane.loose) dropLoose(lane, l.hold, glm::vec3(0.0f, 0.0f, 1.2f));
    if (before < 14.0f && m_rockfall >= 14.0f) {
        // Where they ended up: on the ground, on a ledge, or (a bug) under the world.
        const kke::RigidWorld& w = m_rigid->world();
        int ground = 0, ledge = 0, lost = 0, moving = 0;
        for (const Loose& l : lane.loose) {
            const glm::vec3 p = w.position(l.body);
            if (glm::length(w.velocity(l.body)) > 0.2f) ++moving;
            if (p.y < -0.5f) ++lost;
            else if (p.y < 0.6f) ++ground;
            else ++ledge;
        }
        kke::log::get(name())->info("rockfall: {} loose holds came down: {} on the ground, {} on ledges or the rock, {} still moving, {} under the world",
                                    lane.loose.size(), ground, ledge, moving, lost);
    }
}

void ClimbRaceModule::render(const kke::RenderContext& ctx) {
    m_ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    kke::RigidWorld& w = m_rigid->world();
    for (const auto& lane : m_lanes) {
        lane->mesh->draw(ctx, glm::translate(glm::mat4(1.0f), lane->offset), 0.0f, 0.85f);
        for (const Loose& l : lane->loose) l.mesh->draw(ctx, w.transform(l.body), 0.0f, 0.8f);
    }
    for (const Rock& rock : m_rocks) m_rockMesh->draw(ctx, w.transform(rock.body), 0.0f, 0.85f);
    // Where each hand would go: cyan left, magenta right; gold while a
    // lunge charges (bigger with the charge); red = the crosshair's hold
    // is out of reach.
    for (const Racer& r : m_racers) {
        if (r.seat < 0) continue; // players' markers only
        const kke::Climber& c = *r.climber;
        if (!c.climbing()) continue;
        const kke::ClimbWall& wall = c.wall();
        for (int h = 0; h < 2; ++h) {
            const int t = c.aimTarget(h);
            if (t < 0) continue;
            const glm::vec3 p = toWorld(r, wall.holds()[static_cast<size_t>(t)].position + wall.holds()[static_cast<size_t>(t)].normal * 0.06f);
            const float s = 0.07f + 0.08f * c.charge(h);
            const int color = c.charge(h) > 0.05f ? 3 : h;
            const glm::vec3 side(h == 0 ? -0.05f : 0.05f, 0.0f, 0.0f);
            m_markers[color]->draw(ctx, glm::scale(glm::translate(glm::mat4(1.0f), p + side), glm::vec3(s)), 0.0f, 0.3f);
        }
        if (r.crosshair >= 0 && r.crosshairOut) {
            const kke::ClimbHold& hold = wall.holds()[static_cast<size_t>(r.crosshair)];
            m_markers[2]->draw(ctx, glm::scale(glm::translate(glm::mat4(1.0f), toWorld(r, hold.position + hold.normal * 0.06f)), glm::vec3(0.06f)), 0.0f,
                               0.3f);
        }
    }
    if (!m_charModel)
        for (const Racer& r : m_racers) {
            const glm::vec3 feet = w.characterPosition(r.id);
            const float yaw = bodyInput(r).yaw;
            m_capsule->draw(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-yaw), glm::vec3(0, 1, 0)), 0.0f, 0.6f);
        }
}

void ClimbRaceModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    kke::RigidWorld& w = m_rigid->world();
    for (const auto& lane : m_lanes) {
        lane->mesh->drawShadow(ctx, glm::translate(glm::mat4(1.0f), lane->offset));
        for (const Loose& l : lane->loose) l.mesh->drawShadow(ctx, w.transform(l.body));
    }
    for (const Rock& rock : m_rocks) m_rockMesh->drawShadow(ctx, w.transform(rock.body));
    if (!m_charModel)
        for (const Racer& r : m_racers) {
            const float yaw = bodyInput(r).yaw;
            m_capsule->drawShadow(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), w.characterPosition(r.id)), glm::radians(-yaw), glm::vec3(0, 1, 0)));
        }
}

} // namespace climb_race
