#include "TennisModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/PhysicsModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <typeindex>

namespace tennis {

namespace {

float envFloat(const char* name, float fallback) {
    const char* v = kke::dev::env(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}
bool envOn(const char* name) { return kke::dev::flag(name); }

} // namespace

TennisModule::TennisModule() = default;
TennisModule::~TennisModule() = default;

std::vector<kke::ModuleDependency> TennisModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the people, the fences and the stands (Jolt)" },
             { std::type_index(typeid(kke::PhysicsModule)), true, "the FEMFX rubber balls" },
             { std::type_index(typeid(kke::InputModule)), true, "the controls, rebindable" },
             { std::type_index(typeid(kke::ModelModule)), false, "the players' animated bodies" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join, CPU players" },
             { std::type_index(typeid(kke::NetModule)), false, "the sport center online" } };
}

void TennisModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_physics = app.getModule<kke::PhysicsModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    m_net = app.getModule<kke::NetModule>();

    m_allBots = envOn("KKE_TENNIS_BOTS");
    m_doubles = envOn("KKE_TENNIS_DOUBLES");
    m_ballTest = envOn("KKE_TENNIS_BALLTEST");
    m_level = static_cast<int>(envFloat("KKE_TENNIS_LEVEL", 1.0f));
    m_seed = static_cast<uint32_t>(envFloat("KKE_TENNIS_SEED", 1.0f));
    m_quitAfter = envFloat("KKE_TENNIS_QUIT", -1.0f);

    const char* base = SDL_GetBasePath();
    const std::filesystem::path tex = std::filesystem::path(base ? base : "") / "textures" / "tennis_ball.png";
    std::error_code ec;
    if (std::filesystem::exists(tex, ec)) m_ballTexture = tex.string();

    app.camera().farPlane = 400.0f;
    app.camera().fovDegrees = 50.0f;
    defineControls();
    m_rig = std::make_unique<Rig>(app);
    if (m_models) m_rig->load(*m_models);
    buildWorld();
    buildHud();
    setupLobby();
    if (m_ballTest) {
        kke::log::get(name())->info("ball test: test shots at the net, the court and the fence (KKE_TENNIS_BALLTEST)");
        if (m_lobby) m_lobby->close();
        m_inMenu = false;
    } else if (!m_inMenu) {
        startLocalMatch();
    }
}

void TennisModule::defineControls() {
    using IM = kke::InputModule;
    m_input->setPlayers(4);
    for (int p = 0; p < 4; ++p) {
        kke::InputMap& in = m_input->map(p);
        kke::InputModule::defineCharacterActions(in);
        // Tennis needs the move stick and the shot buttons, nothing else.
        for (const char* a : { "jump", "sprint", "walk", "crouch", "fire", "aim", "interact", "camera.toggle", "camera.zoom", "look", "look.rate",
                               "voice.talk" })
            in.clearBindings(a);
        in.defineAction({ "tennis.topspin", "Topspin (the all-round shot)", "Shots", "game" });
        in.defineAction({ "tennis.flat", "Flat (hard and fast)", "Shots", "game" });
        in.defineAction({ "tennis.slice", "Slice (low and slow)", "Shots", "game" });
        in.defineAction({ "tennis.lob", "Lob (over their head)", "Shots", "game" });
        in.defineAction({ "tennis.menu", "Back to the menu", "Game", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        in.addBinding(IM::bind("tennis.topspin", IM::key(SDL_SCANCODE_SPACE)));
        in.addBinding(IM::bind("tennis.topspin", IM::mouse(SDL_BUTTON_LEFT)));
        in.addBinding(IM::bind("tennis.flat", IM::key(SDL_SCANCODE_J)));
        in.addBinding(IM::bind("tennis.slice", IM::key(SDL_SCANCODE_K)));
        in.addBinding(IM::bind("tennis.slice", IM::mouse(SDL_BUTTON_RIGHT)));
        in.addBinding(IM::bind("tennis.lob", IM::key(SDL_SCANCODE_L)));
        in.addBinding(IM::bind("tennis.menu", IM::key(SDL_SCANCODE_ESCAPE)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
        in.addBinding(IM::bind("tennis.topspin", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("tennis.flat", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        in.addBinding(IM::bind("tennis.slice", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("tennis.lob", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
        in.addBinding(IM::bind("tennis.menu", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));
    }
    m_input->commitDefaults();
}

void TennisModule::clearPlayers() {
    kke::RigidWorld& w = m_rigid->world();
    for (Player& p : m_players)
        if (p.body) w.removeCharacter(p.body);
    m_players.clear();
    m_matches.clear();
}

void TennisModule::spawnPlayer(const std::string& nm, const glm::vec3& tint, int team, bool cpu, int input, int level) {
    Player p;
    p.name = nm;
    p.tint = tint;
    p.team = team;
    p.cpu = cpu;
    p.input = input;
    p.level = level;
    kke::RigidWorld::CharacterDesc cd;
    cd.radius = 0.3f;
    cd.height = 1.8f;
    cd.pushStrength = 0.0f;
    p.body = m_rigid->world().addCharacter(cd);
    if (cpu) p.bot = std::make_unique<Bot>(m_seed * 7919u + static_cast<uint32_t>(m_players.size()) * 104729u, level);
    p.look = std::make_unique<Body>(*m_rig, *m_models, tint, true);
    m_players.push_back(std::move(p));
}

void TennisModule::startLocalMatch() {
    clearPlayers();
    auto m = std::make_unique<Match>();
    m->court = 0;
    // Who plays: the menu's seats, then its CPU players; the switches
    // without the menu.
    struct Entry { std::string name; glm::vec3 tint; bool cpu; int input; int level; };
    std::vector<Entry> entries;
    static const glm::vec3 kTints[] = { { 0.35f, 0.6f, 1.25f }, { 1.25f, 0.45f, 0.32f }, { 0.45f, 1.1f, 0.45f }, { 1.2f, 1.0f, 0.35f } };
    if (m_lobby && !m_allBots) {
        kke::Lobby& l = m_lobby->lobby();
        for (int seat : l.joinedSeats()) {
            glm::vec3 tint = kTints[entries.size() % 4];
            const auto& fields = l.lookFields();
            for (size_t f = 0; f < fields.size(); ++f)
                if (fields[f].id == "colour" && !fields[f].swatches.empty())
                    tint = fields[f].swatches[static_cast<size_t>(l.seat(seat).look[f]) % fields[f].swatches.size()] * 1.25f;
            entries.push_back({ l.seatName(seat), tint, false, m_lobby->playerOf(seat), 0 });
        }
        for (int i = 0; i < l.cpuCount(); ++i) entries.push_back({ "", kTints[entries.size() % 4], true, -1, l.cpuDifficulty(i) });
    } else if (!m_allBots) {
        entries.push_back({ "You", kTints[0], false, 0, 0 });
    }
    // At least an opponent; doubles when there are more than two.
    const size_t want = (m_doubles || entries.size() > 2) ? 4 : 2;
    while (entries.size() < want) entries.push_back({ "", kTints[entries.size() % 4], true, -1, m_level });
    entries.resize(want);
    static const char* const kCpuNames[] = { "Ace", "Deuce", "Volley", "Lobster" };
    int cpuNumber = 0;
    for (Entry& e : entries)
        if (e.cpu && e.name.empty()) e.name = std::string(kCpuNames[cpuNumber++ % 4]) + " (CPU)";
    // Teams: seats alternate across the net, unless the menu put the
    // people at this screen on one side against the CPU players.
    const int size = static_cast<int>(want) / 2;
    std::vector<int> team(want, 0);
    if (m_teams == 1) {
        for (size_t i = 0; i < want; ++i) team[i] = entries[i].cpu ? 1 : 0;
        int n0 = static_cast<int>(std::count(team.begin(), team.end(), 0));
        for (size_t i = want; i-- > 0 && n0 > size;)
            if (team[i] == 0) { team[i] = 1; --n0; }
        for (size_t i = 0; i < want && n0 < size; ++i)
            if (team[i] == 1) { team[i] = 0; ++n0; }
    } else {
        for (size_t i = 0; i < want; ++i) team[i] = static_cast<int>(i % 2);
    }
    for (size_t i = 0; i < want; ++i) {
        spawnPlayer(entries[i].name, entries[i].tint, team[i], entries[i].cpu, entries[i].input, entries[i].level);
        m->players.push_back(static_cast<int>(m_players.size()) - 1);
    }
    // Slots within each team.
    int slots[2] = { 0, 0 };
    for (int idx : m->players) player(idx).slot = slots[player(idx).team]++;

    m->rules.teamSize = size;
    switch (m_length) {
    case 1: m->rules.gamesPerSet = 6; break;
    case 2: m->rules.gamesPerSet = 4; m->rules.setsToWin = 2; break;
    case 3: m->rules.gamesPerSet = 2; break; // quick: to 2 games
    default: m->rules.gamesPerSet = 4; break;
    }
    if (const char* g = kke::dev::env("KKE_TENNIS_GAMES")) m->rules.gamesPerSet = std::max(1, std::atoi(g));
    m->score = Score(m->rules);
    m->ball = std::make_unique<Ball>(*m_physics, m_center.courts[static_cast<size_t>(m->court)], m_ballTexture);
    m->phase = Match::Phase::Warmup;
    m->phaseTime = 0.0f;
    m_matches.push_back(std::move(m));
    std::string who;
    for (int idx : m_matches.back()->players) who += (who.empty() ? "" : ", ") + player(idx).name + " (team " + std::to_string(player(idx).team + 1) + ")";
    kke::log::get(name())->info("{} match on court {}: {}; first to {} games{}", size == 2 ? "doubles" : "singles", 1, who,
                                m_matches.back()->rules.gamesPerSet, m_matches.back()->rules.setsToWin > 1 ? ", best of three" : "");
}

void TennisModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    if (m_ballTest) {
        ballTest(ctx.fixedDt);
        return;
    }
    for (auto& m : m_matches) stepMatch(*m, ctx.fixedDt);
    // KKE_TENNIS_QUIT: a report every 10 s of game time, then quit.
    m_clock += ctx.fixedDt;
    if (m_quitAfter > 0.0f) {
        if (m_clock >= m_reportAt) {
            m_reportAt += 10.0f;
            for (const auto& m : m_matches)
                kke::log::get(name())->info("t {:.0f} s: sets {}-{}, games {} / {}, points {}-{} ({}), {} points played, longest rally {} shots",
                                            m_clock, m->score.sets(0), m->score.sets(1), m->score.setsText(0), m->score.setsText(1),
                                            m->score.pointText(0), m->score.pointText(1), m->score.callText(), m_pointsPlayed, m_longestRally);
        }
        if (m_clock >= m_quitAfter) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void TennisModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    kke::InputMap& p1 = m_input->map(0);
    if (p1.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    if (m_inMenu) {
        updateLobby(dt);
    } else if (m_lobby && !m_ballTest) {
        for (int i = 0; i < 4; ++i)
            if (m_input->map(i).pressed("tennis.menu")) {
                m_inMenu = true;
                clearPlayers();
                m_lobby->open();
                break;
            }
    }
    for (auto& m : m_matches)
        for (int idx : m->players)
            if (!player(idx).cpu) readHuman(*m, player(idx));
    updateBodies(dt);
    updateCameras(dt);
    updateHud();

}

void TennisModule::render(const kke::RenderContext& ctx) {
    renderCourts(ctx);
    for (Player& p : m_players)
        if (p.look) p.look->render(ctx);
}

void TennisModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_standMesh) m_standMesh->drawShadow(ctx);
    if (m_fenceMesh) m_fenceMesh->drawShadow(ctx);
    for (Player& p : m_players)
        if (p.look) p.look->renderShadow(ctx);
}

void TennisModule::shutdown() {
    clearPlayers();
    m_testBall.reset();
    if (m_rigid) {
        kke::RigidWorld& w = m_rigid->world();
        for (kke::RigidWorld::BodyId b : m_statics) w.remove(b);
    }
    m_statics.clear();
    m_courtMesh.reset();
    m_standMesh.reset();
    m_fenceMesh.reset();
    m_rig.reset();
}

// ------------------------------------------------------------ ball test

// Fires the same shots at the same spots and logs what the FEMFX ball
// does: how high it comes off the court (restitution), how much speed it
// keeps along the court, whether the net and the fence stop it, and
// whether the contact detection sees each touch.
void TennisModule::ballTest(float dt) {
    if (!m_testBall) {
        m_testBall = std::make_unique<Ball>(*m_physics, m_center.courts[0], m_ballTexture);
        m_testTime = 10.0f;
    }
    Ball& b = *m_testBall;
    b.step(dt);
    for (const Ball::Event& e : b.takeEvents()) {
        const char* kind = e.kind == Ball::Event::Kind::Bounce ? "bounce" : e.kind == Ball::Event::Kind::Net ? "net" : "out";
        const float gap = glm::length(b.bodyPosition() - b.position());
        kke::log::get(name())->info("ball test {}: {} at ({:.2f}, {:.2f}, {:.2f}), velocity now ({:.2f}, {:.2f}, {:.2f}), FEMFX body {:.3f} m off", m_testShot,
                                    kind, e.at.x, e.at.y, e.at.z, b.velocity().x, b.velocity().y, b.velocity().z, gap);
    }
    m_testTime += dt;
    if (m_testTime < 3.0f) return;
    m_testTime = 0.0f;
    ++m_testShot;
    struct Test { const char* what; glm::vec3 from, vel; };
    const Test tests[] = {
        { "drop from 2 m", { 0.0f, 2.0f, 5.0f }, { 0.0f, 0.0f, 0.0f } },
        { "groundstroke 25 m/s", { 0.0f, 1.0f, 11.0f }, planShot({ 0.0f, 1.0f, 11.0f }, { 1.0f, 0.0f, -9.0f }, 25.0f, ShotKind::Flat).velocity },
        { "into the net at 30 m/s", { 0.0f, 0.5f, 6.0f }, { 0.0f, 0.0f, -30.0f } },
        { "serve 45 m/s", { 0.5f, 2.6f, 12.2f }, planShot({ 0.5f, 2.6f, 12.2f }, { -2.0f, 0.0f, -5.5f }, 45.0f, ShotKind::Flat).velocity },
        { "at the side fence 35 m/s", { 0.0f, 1.0f, 0.0f + 5.0f }, { 35.0f, 0.5f, 0.0f } },
        { "at the back fence 40 m/s", { 0.0f, 1.0f, -5.0f }, { 0.0f, 1.0f, -40.0f } },
        { "lob at the roof", { 0.0f, 1.0f, 10.0f }, { 0.0f, 22.0f, -2.0f } },
    };
    constexpr int kTests = static_cast<int>(sizeof(tests) / sizeof(tests[0]));
    if (m_testShot >= kTests) {
        kke::log::get(name())->info("ball test done");
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        m_ballTest = false;
        return;
    }
    const Test& t = tests[m_testShot];
    b.place(t.from, glm::vec3(0.0f));
    b.strike(t.vel, glm::vec3(0.0f), 0.0f, 0.0f);
    kke::log::get(name())->info("ball test {}: {} from ({:.1f}, {:.1f}, {:.1f}) at ({:.1f}, {:.1f}, {:.1f}) m/s", m_testShot, t.what, t.from.x, t.from.y,
                                t.from.z, t.vel.x, t.vel.y, t.vel.z);
}

} // namespace tennis
