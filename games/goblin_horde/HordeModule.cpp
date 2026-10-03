#include "HordeModule.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace horde {

namespace {

constexpr float kFort = 13.0f;   // half the width of the ruined fort's walls
constexpr float kGateHalf = 2.2f; // half the width of each gateway

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}

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

} // namespace

HordeModule::HordeModule() = default;
HordeModule::~HordeModule() = default;

std::vector<kke::ModuleDependency> HordeModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the ruins, the king's body and the goblins' ragdolls (Jolt)" },
             { std::type_index(typeid(kke::InputModule)), true, "the controls, rebindable" },
             { std::type_index(typeid(kke::ModelModule)), true, "the king and the goblins" } };
}

void HordeModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_ragdolls = kke::bestRagdollPhysics(app.findCapability<kke::IRagdollPhysics>());

    if (const char* b = std::getenv("KKE_HORDE_BOT"); b && *b == '1') m_bot = true;
    m_quitAfter = envFloat("KKE_HORDE_QUIT", -1.0f);
    m_maxAlive = std::max(1, static_cast<int>(envFloat("KKE_HORDE_MAX", 60.0f)));
    m_startWave = std::max(1, static_cast<int>(envFloat("KKE_HORDE_WAVE", 1.0f)));
    m_ragdollCap = std::max(0, static_cast<int>(envFloat("KKE_HORDE_RAGDOLLS", 12.0f)));

    // Controls: WASD and the mouse (click the view to grab it, Esc lets go),
    // left mouse slash, right mouse the big swing, Shift block, Space roll.
    // Controller: sticks, X slash, Y big swing, RB/LT block, A roll.
    using IM = kke::InputModule;
    kke::InputMap& in = m_input->map(0);
    kke::InputModule::defineCharacterActions(in);
    // No voice chat here, so B (push-to-talk) stays free; Q is the ping.
    for (const char* a : { "jump", "sprint", "walk", "crouch", "fire", "aim", "interact", "camera.toggle", "camera.zoom", "voice.talk" }) in.clearBindings(a);
    in.defineAction({ "horde.slash", "Slash (quick, hits a few)", "Fight", "game" });
    in.defineAction({ "horde.heavy", "Great swing (all around you)", "Fight", "game" });
    in.defineAction({ "horde.block", "Block (just in time: parry)", "Fight", "game" });
    in.defineAction({ "horde.roll", "Roll", "Fight", "game" });
    in.defineAction({ "horde.again", "Try again", "Game", "game" });
    in.defineAction({ "panels", "Developer panels", "Game", "game" });
    in.addBinding(IM::bind("horde.slash", IM::mouse(SDL_BUTTON_LEFT)));
    in.addBinding(IM::bind("horde.slash", IM::key(SDL_SCANCODE_J)));
    in.addBinding(IM::bind("horde.slash", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
    in.addBinding(IM::bind("horde.heavy", IM::mouse(SDL_BUTTON_RIGHT)));
    in.addBinding(IM::bind("horde.heavy", IM::key(SDL_SCANCODE_K)));
    in.addBinding(IM::bind("horde.heavy", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
    in.addBinding(IM::bind("horde.block", IM::key(SDL_SCANCODE_LSHIFT), kke::Trigger::Continuous));
    in.addBinding(IM::bind("horde.block", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), kke::Trigger::Continuous));
    kke::Binding lt = IM::bind("horde.block", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), kke::Trigger::Continuous);
    lt.threshold = 0.3f;
    in.addBinding(lt);
    in.addBinding(IM::bind("horde.roll", IM::key(SDL_SCANCODE_SPACE)));
    in.addBinding(IM::bind("horde.roll", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    in.addBinding(IM::bind("horde.again", IM::key(SDL_SCANCODE_R)));
    in.addBinding(IM::bind("horde.again", IM::pad(SDL_GAMEPAD_BUTTON_START)));
    in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
    m_input->commitDefaults();
    app.window().setQuitOnEscape(false); // Esc lets go of the mouse instead
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        // Start is "try again" once the horde has won; otherwise it pauses.
        shell->startIsTheGames = [this] { return m_phase == Phase::Overrun; };
        shell->addPauseItem("Start over", [this] { restart(); });
    }

    app.camera().farPlane = 200.0f;
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.settings.armLength = 5.0f;
    m_rig.settings.pivotHeight = 1.7f;
    m_rig.settings.shoulderOffset = 0.0f;
    m_rig.settings.fovDegrees = 60.0f;
    m_rig.pitch = -18.0f;

    auto log = kke::log::get(name());
    {
        // The minds (data/goblin.yml or .json).
        const char* base = SDL_GetBasePath();
        std::string error;
        if (m_ai.loadSpecies(std::string(base ? base : "") + "data/goblin.yml", &error) == 0)
            log->error("data/goblin.yml: {} (goblins will stand still)", error);
    }
    loadWaves();
    buildArena();
    loadHero();
    loadGoblins();
    spawnHero();
    buildHud();
    restart();
    log->info("{} goblin look(s), {} triangles each on average (from {}); up to {} alive, {} ragdolls; king: {}", m_variants.size(),
              m_variants.empty() ? 0 : m_triangles / m_variants.size(), m_variants.empty() ? 0 : m_fullTriangles / m_variants.size(), m_maxAlive,
              m_ragdollCap, m_heroModel ? "animated" : "a block");
}

void HordeModule::loadWaves() {
    const char* base = SDL_GetBasePath();
    nlohmann::json j;
    std::string error;
    if (!kke::datafile::loadPath(std::string(base ? base : "") + "data/waves.yml", j, &error)) {
        kke::log::get(name())->error("data/waves.yml: {} (using one wave of 10)", error);
        m_waves = { Wave{ 10, 10, 1.0f, 1.0f } };
        return;
    }
    m_attackers = std::max(1, j.value("attackers", 6));
    m_breather = std::max(0.0f, j.value("breather", 4.0f));
    if (j.contains("waves") && j["waves"].is_array())
        for (const nlohmann::json& w : j["waves"]) {
            if (!w.is_object()) continue;
            Wave wave;
            wave.goblins = std::max(1, w.value("goblins", wave.goblins));
            wave.atOnce = std::max(1, w.value("atOnce", wave.goblins));
            wave.speed = std::clamp(w.value("speed", 1.0f), 0.2f, 3.0f);
            wave.health = std::clamp(w.value("health", 1.0f), 0.1f, 20.0f);
            m_waves.push_back(wave);
        }
    if (m_waves.empty()) m_waves = { Wave{ 10, 10, 1.0f, 1.0f } };
}

HordeModule::Wave HordeModule::waveAt(int n) const {
    const int last = static_cast<int>(m_waves.size());
    if (n <= last) return m_waves[static_cast<size_t>(n - 1)];
    // Past the list: the last wave, half again bigger each time round.
    Wave w = m_waves.back();
    const float grow = std::pow(1.5f, static_cast<float>(n - last));
    w.goblins = static_cast<int>(static_cast<float>(w.goblins) * grow);
    w.health *= 1.0f + 0.15f * static_cast<float>(n - last);
    w.speed = std::min(w.speed * (1.0f + 0.03f * static_cast<float>(n - last)), 1.6f);
    return w;
}

void HordeModule::buildArena() {
    kke::RigidWorld& w = m_rigid->world();
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    auto solid = [&](const glm::vec3& c, const glm::vec3& h) {
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.position = c;
        d.halfExtents = h;
        w.add(d);
    };
    auto block = [&](const glm::vec3& c, const glm::vec3& h, const glm::vec3& color, bool obstacle) {
        appendBox(c, h, color, v, idx);
        solid(c, h);
        if (obstacle) m_obstacles.push_back({ c.x, c.z, h.x, h.z });
    };
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::mt19937 rng(11);
    // A hilltop meadow at dusk.
    block({ 0.0f, -0.5f, 0.0f }, { 60.0f, 0.5f, 60.0f }, { 0.2f, 0.27f, 0.13f }, false);
    // The fort: four walls with a gateway in the middle of each, broken
    // into uneven stretches of old stone.
    const glm::vec3 stone(0.42f, 0.4f, 0.37f), dark(0.3f, 0.29f, 0.27f);
    for (int side = 0; side < 4; ++side) {
        const bool alongX = side < 2;
        const float at = side % 2 == 0 ? kFort : -kFort;
        for (float s = -kFort; s < kFort - 0.01f;) {
            const float len = 2.0f + u(rng) * 2.5f;
            float a = s, b = std::min(s + len, kFort + 0.4f);
            s = b;
            if (b > -kGateHalf && a < kGateHalf) {
                // Leave the gateway open; keep what's either side of it.
                if (a < -kGateHalf) b = -kGateHalf;
                else if (b > kGateHalf) a = kGateHalf;
                else continue;
            }
            const float height = 1.0f + u(rng) * 1.8f;
            const float mid = (a + b) * 0.5f, half = (b - a) * 0.5f;
            const glm::vec3 c = alongX ? glm::vec3(mid, height * 0.5f, at) : glm::vec3(at, height * 0.5f, mid);
            const glm::vec3 h = alongX ? glm::vec3(half, height * 0.5f, 0.45f) : glm::vec3(0.45f, height * 0.5f, half);
            block(c, h, u(rng) < 0.5f ? stone : dark, true);
        }
        // Gate posts, taller.
        for (float sgn : { -1.0f, 1.0f }) {
            const float p = sgn * (kGateHalf + 0.5f);
            const glm::vec3 c = alongX ? glm::vec3(p, 1.8f, at) : glm::vec3(at, 1.8f, p);
            block(c, { 0.55f, 1.8f, 0.55f }, dark, true);
        }
        const glm::vec3 out = alongX ? glm::vec3(0.0f, 0.0f, at > 0 ? 1.0f : -1.0f) : glm::vec3(at > 0 ? 1.0f : -1.0f, 0.0f, 0.0f);
        m_gates.push_back(out);
    }
    // The watchtower's stump in the north, a well, rubble and crates.
    block({ 0.0f, 2.0f, -7.5f }, { 2.0f, 2.0f, 2.0f }, stone, true);
    block({ 0.0f, 4.2f, -7.5f }, { 2.3f, 0.2f, 2.3f }, dark, false);
    block({ 6.0f, 0.4f, 5.0f }, { 0.8f, 0.4f, 0.8f }, dark, true);
    for (const glm::vec3& c : { glm::vec3(-6.5f, 0.35f, 4.0f), glm::vec3(-7.3f, 0.35f, 4.6f), glm::vec3(-6.9f, 1.05f, 4.3f), glm::vec3(7.0f, 0.35f, -4.0f) })
        block(c, { 0.35f, 0.35f, 0.35f }, { 0.45f, 0.3f, 0.16f }, true);
    for (int i = 0; i < 18; ++i) {
        const float a = u(rng) * 6.283f, r = 3.0f + u(rng) * 9.0f;
        const glm::vec3 c(std::cos(a) * r, 0.08f, std::sin(a) * r);
        if (std::abs(c.x) < 1.5f && std::abs(c.z) < 1.5f) continue;
        appendBox(c, { 0.15f + u(rng) * 0.2f, 0.08f, 0.12f + u(rng) * 0.2f }, stone * 0.9f, v, idx);
    }
    // Pines outside the walls.
    for (int i = 0; i < 70; ++i) {
        const float a = u(rng) * 6.283f, r = 20.0f + u(rng) * 30.0f;
        const glm::vec3 base(std::cos(a) * r, 0.0f, std::sin(a) * r);
        bool nearGate = false;
        for (const glm::vec3& g : m_gates) nearGate = nearGate || glm::length(glm::vec2(base.x - g.x * r, base.z - g.z * r)) < 6.0f;
        if (nearGate) continue;
        const float hgt = 3.0f + u(rng) * 3.0f;
        appendBox(base + glm::vec3(0.0f, hgt * 0.2f, 0.0f), { 0.18f, hgt * 0.2f, 0.18f }, { 0.3f, 0.2f, 0.12f }, v, idx);
        for (int k = 0; k < 3; ++k) {
            const float s = 1.2f - 0.35f * static_cast<float>(k);
            appendBox(base + glm::vec3(0.0f, hgt * (0.45f + 0.2f * static_cast<float>(k)), 0.0f), { s, hgt * 0.13f, s }, { 0.09f, 0.2f + 0.03f * static_cast<float>(k), 0.12f },
                      v, idx);
        }
    }
    m_arena = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_arena->upload(v, idx);

    // The goblins find their way round the ruins on a navmesh of the same boxes.
    std::vector<glm::vec3> pts;
    pts.reserve(v.size());
    for (const kke::Vertex& vx : v) pts.push_back(vx.position);
    kke::ai::NavMeshSettings ns;
    ns.cellSize = 0.3f;
    ns.agentRadius = 0.35f;
    ns.agentHeight = 1.3f;
    std::string error;
    if (m_nav.build(pts, idx, ns, &error)) m_ai.setNavMesh(&m_nav);
    else kke::log::get(name())->error("navmesh: {} (goblins will walk straight)", error);

    std::vector<kke::Vertex> bv;
    std::vector<uint32_t> bi;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.3f, 0.9f, 0.22f }, { 0.8f, 0.7f, 0.25f }, bv, bi);
    m_block = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_block->upload(bv, bi);
}

void HordeModule::restart() {
    for (auto& g : m_goblins) releaseGoblin(*g);
    m_goblins.clear();
    m_ragdolled.clear();
    kke::Combatant& c = m_combat.get(m_hero.id);
    c.reset();
    m_rigid->world().teleportCharacter(m_hero.body, glm::vec3(0.0f, 0.05f, 2.0f));
    m_rigid->world().setCharacterVelocity(m_hero.body, glm::vec3(0.0f));
    m_hero.facing = glm::vec3(0.0f, 0.0f, 1.0f);
    m_hero.push = glm::vec3(0.0f);
    m_hero.lastState = -1;
    if (m_hero.anim && m_hs.move >= 0) m_hero.anim->play(m_hs.move, 0.0f, true);
    m_rig.yaw = 180.0f;
    m_kills = 0;
    startWave(m_startWave);
}

void HordeModule::startWave(int n) {
    m_wave = n;
    const Wave w = waveAt(n);
    m_toSpawn = w.goblins;
    m_waveSize = w.goblins;
    m_waveKills = 0;
    m_spawnTimer = 1.5f;
    m_morale = 1.0f;
    m_phase = Phase::Intro;
    m_phaseTime = 0.0f;
}

void HordeModule::setCaptured(bool on) {
    m_captured = on;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), on);
}

void HordeModule::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && !ImGui::GetIO().WantCaptureMouse && !m_app->uiCapturesMouse() && e.button.button == SDL_BUTTON_LEFT) setCaptured(true);
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE) setCaptured(false);
}

void HordeModule::onHit(const kke::HitEvent& e) {
    if (e.target == m_hero.id) {
        m_hero.push += e.push;
        if (e.outcome == kke::HitOutcome::Hit || e.outcome == kke::HitOutcome::Knockdown || e.outcome == kke::HitOutcome::Killed) {
            m_hero.flinch = 1.0f;
            m_hero.flinchDir = e.push;
        }
        return;
    }
    Goblin* g = goblinByCombatant(e.target);
    if (!g) return;
    if (e.outcome == kke::HitOutcome::Killed) {
        ++m_kills;
        ++m_waveKills;
        m_morale = std::max(0.0f, m_morale - 0.07f);
        killGoblin(*g, e.push + glm::vec3(0.0f, 1.5f, 0.0f));
        return;
    }
    g->push += e.push;
    g->flinch = 1.0f;
    g->flinchDir = e.push;
    g->hurtFlash = 1.0f;
}

void HordeModule::update(const kke::UpdateContext& ctx) {
    const auto frameStart = std::chrono::steady_clock::now();
    const float dt = std::min(ctx.dt, 0.05f);
    kke::InputMap& in = m_input->map(0);
    if (in.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());

    m_phaseTime += dt;
    kke::Combatant& hero = m_combat.get(m_hero.id);
    int alive = 0;
    for (const auto& g : m_goblins) alive += g->dead ? 0 : 1;
    switch (m_phase) {
    case Phase::Intro:
        if (m_phaseTime > 2.5f) {
            m_phase = Phase::Fighting;
            m_phaseTime = 0.0f;
        }
        [[fallthrough]];
    case Phase::Fighting: {
        // Feed the wave in through the gates.
        const Wave w = waveAt(m_wave);
        m_spawnTimer -= dt;
        const int cap = std::min(w.atOnce, m_maxAlive);
        while (m_toSpawn > 0 && alive < cap && m_spawnTimer <= 0.0f) {
            const glm::vec3& gate = m_gates[std::uniform_int_distribution<size_t>(0, m_gates.size() - 1)(m_rng)];
            const glm::vec3 side(gate.z, 0.0f, -gate.x);
            const float spread = std::uniform_real_distribution<float>(-5.0f, 5.0f)(m_rng);
            const float out = std::uniform_real_distribution<float>(20.0f, 26.0f)(m_rng);
            spawnGoblin(gate * out + side * spread);
            --m_toSpawn;
            ++alive;
            m_spawnTimer = 0.12f;
        }
        if (m_toSpawn <= 0 && alive == 0 && m_phase == Phase::Fighting) {
            m_phase = Phase::Cleared;
            m_phaseTime = 0.0f;
            hero.heal(hero.stats().maxHealth * 0.4f); // a breather
        }
        if (!hero.alive()) {
            m_phase = Phase::Overrun;
            m_phaseTime = 0.0f;
        }
        break;
    }
    case Phase::Cleared:
        if (m_phaseTime > m_breather) startWave(m_wave + 1);
        break;
    case Phase::Overrun:
        if ((m_phaseTime > 1.5f && in.pressed("horde.again")) || (m_bot && m_phaseTime > 5.0f)) restart();
        break;
    }
    // Morale: kills knock it down, it comes back; the last few of a wave lose heart.
    m_morale = std::min(1.0f, m_morale + dt * 0.04f);
    if (m_toSpawn <= 0 && alive > 0 && alive <= std::max(2, m_waveSize / 8)) m_morale = std::min(m_morale, 0.1f);

    updateHero(dt);
    const auto aiStart = std::chrono::steady_clock::now();
    updateGoblins(dt);
    const auto aiEnd = std::chrono::steady_clock::now();
    for (const kke::HitEvent& e : m_combat.step(dt)) onHit(e);
    animateHero(dt);
    for (auto& g : m_goblins) animateGoblin(*g, dt);
    const auto animEnd = std::chrono::steady_clock::now();
    updateCamera(dt);
    updateHud();

    // Timing and the headless report.
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
    m_frameMs += ctx.dt * 1000.0;
    m_worstMs = std::max(m_worstMs, static_cast<double>(ctx.dt) * 1000.0);
    m_aiMs += std::chrono::duration<double, std::milli>(aiEnd - aiStart).count();
    m_goblinMs += std::chrono::duration<double, std::milli>(animEnd - aiEnd).count();
    ++m_frames;
    (void)ms;
    m_clock += ctx.dt;
    if (m_quitAfter > 0.0f) {
        if (m_clock >= m_reportAt) {
            m_reportAt += 10.0f;
            const double n = std::max(1, m_frames);
            kke::log::get(name())->info("t {:.0f} s: wave {}, {} goblins alive, {} kills, king health {:.0f}; frame {:.1f} ms avg ({:.1f} worst), "
                                        "goblin minds+moves {:.2f} ms, animation {:.2f} ms",
                                        m_clock, m_wave, alive, m_kills, hero.health(), m_frameMs / n, m_worstMs, m_aiMs / n, m_goblinMs / n);
            m_frameMs = m_worstMs = m_aiMs = m_goblinMs = 0.0;
            m_frames = 0;
        }
        if (m_clock >= m_quitAfter) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void HordeModule::updateCamera(float dt) {
    kke::InputMap& in = m_input->map(0);
    if (m_captured) {
        const glm::vec2 look = in.axis2("look");
        m_rig.addLook(look.x * m_mouseSensitivity, look.y * m_mouseSensitivity);
    }
    const glm::vec2 rate = in.axis2("look.rate");
    m_rig.addLook(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);
    if (m_bot) {
        // Follow behind the king, slowly.
        const float want = glm::degrees(std::atan2(-m_hero.facing.x, -m_hero.facing.z)) + 180.0f;
        float d = std::fmod(want - m_rig.yaw + 540.0f, 360.0f) - 180.0f;
        m_rig.yaw += d * (1.0f - std::exp(-0.8f * dt));
        m_rig.pitch = -24.0f;
    }
    kke::RigidWorld& world = m_rigid->world();
    const glm::vec3 feet = world.characterPosition(m_hero.body);
    m_rig.update(dt, feet, [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = world.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    }, m_app->camera());
}

void HordeModule::render(const kke::RenderContext& ctx) {
    m_arena->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    if (m_heroModel) return;
    const glm::vec3 feet = m_rigid->world().characterPosition(m_hero.body);
    const float yaw = glm::degrees(std::atan2(m_hero.facing.x, m_hero.facing.z));
    m_block->draw(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(yaw), glm::vec3(0, 1, 0)), 0.3f, 0.5f);
}

void HordeModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_arena->drawShadow(ctx, glm::mat4(1.0f));
    if (m_heroModel) return;
    const glm::vec3 feet = m_rigid->world().characterPosition(m_hero.body);
    m_block->drawShadow(ctx, glm::translate(glm::mat4(1.0f), feet));
}

void HordeModule::shutdown() {
    for (auto& g : m_goblins)
        if (g->ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(g->ragdoll);
    m_goblins.clear();
    m_ai.setNavMesh(nullptr);
}

} // namespace horde
