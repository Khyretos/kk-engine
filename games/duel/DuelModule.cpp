#include "DuelModule.h"

#include "SparringBot.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace duel {

namespace {

constexpr float kRing = 4.2f;        // half the ring's width inside the ropes (m)
constexpr float kStart = 1.7f;       // each corner starts this far from the middle
constexpr float kMinGap = 0.72f;     // fighters' centres never closer than this
constexpr float kWalk = 2.2f, kBlockWalk = 1.0f, kDodgeSpeed = 5.5f;
constexpr float kRagdollTime = 1.3f; // on the ground before the get-up starts
constexpr float kRoundTime = 90.0f;  // then the healthier fighter takes it

float envFloat(const char* name, float fallback) {
    const char* v = std::getenv(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}
bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v == '1';
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

glm::vec3 flat(glm::vec3 v) {
    v.y = 0.0f;
    const float l = glm::length(v);
    return l > 1e-5f ? v / l : glm::vec3(0.0f, 0.0f, -1.0f);
}

} // namespace

DuelModule::DuelModule() = default;
DuelModule::~DuelModule() = default;

// Everything that lives in other modules goes while they are still there
// (ragdolls in the physics world, the HUD in RmlUi, the meshes on the GPU).
void DuelModule::shutdown() {
    for (Fighter& f : m_fighters) {
        if (f.ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(f.ragdoll);
        f.ragdoll = 0;
        f.brain.reset();
        f.anim.reset();
        f.ik.reset();
    }
    if (m_hudDoc) m_hudDoc->Close();
    m_hudDoc = nullptr;
    m_arena.reset();
    m_block.reset();
    m_animSet.reset();
}

kke::AttackDesc DuelModule::attackNamed(const std::string& n) const {
    // The engine's three presets, and the punches in between (Combat.cpp
    // has the numbers they start from; README.md the table).
    kke::AttackDesc a = kke::AttackDesc::light();
    if (n == "cross") {
        // The rear hand: a little slower than the jab, a little more.
        a.windup = 0.3f;
        a.damage = 12.0f;
        a.poiseDamage = 21.0f;
        a.staminaCost = 13.0f;
        a.reach = 1.05f;
    } else if (n == "hook") {
        // Round the guard's side: slower, hurts, wears the guard down.
        a.windup = 0.34f;
        a.recovery = 0.38f;
        a.damage = 15.0f;
        a.poiseDamage = 26.0f;
        a.staminaCost = 15.0f;
        a.reach = 0.9f;
        a.guardDamage = 24.0f;
        a.knockback = 2.0f;
    } else if (n == "uppercut") {
        a = kke::AttackDesc::heavy();
    } else if (n == "knee") {
        a = kke::AttackDesc::kick();
    } else if (n == "kick") {
        // A front kick: the longest reach, pushes them back.
        a = kke::AttackDesc::kick();
        a.windup = 0.4f;
        a.recovery = 0.45f;
        a.damage = 9.0f;
        a.reach = 1.45f;
        a.height = 1.0f;
        a.knockback = 3.5f;
        a.staminaCost = 16.0f;
    }
    a.name = n;
    return a;
}

void DuelModule::throwStrike(Fighter& f, kke::Combatant& c, float distance, float dt) {
    const Intent& in = f.intent;
    f.sinceLight += dt;
    if (in.light) f.buffered = "light";
    else if (in.heavy) f.buffered = "uppercut";
    else if (in.kick) f.buffered = distance < 1.05f || m_st.kick < 0 ? "knee" : "kick"; // close: the knee; at range: the kick
    if (in.light || in.heavy || in.kick) f.bufferAge = 0.0f;
    if (f.buffered.empty()) return;
    f.bufferAge += dt;
    if (f.bufferAge > 0.35f) {
        f.buffered.clear(); // pressed too early: dropped, not thrown late
        return;
    }
    if (!c.canAct()) return;
    std::string name = f.buffered;
    if (name == "light") {
        // Jab, cross, hook: a press soon after the last punch is the next in
        // the chain, a pause starts it again.
        static const char* chain[] = { "jab", "cross", "hook" };
        f.combo = f.sinceLight < 1.15f ? (f.combo + 1) % 3 : 0;
        name = chain[f.combo];
    }
    if (c.attack(attackNamed(name))) {
        ++m_tally[f.corner].thrown[name];
        if (f.buffered == "light") f.sinceLight = 0.0f;
    }
    f.buffered.clear();
}

std::vector<kke::ModuleDependency> DuelModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the ring, the fighters' bodies and knockdown ragdolls (Jolt)" },
             { std::type_index(typeid(kke::InputModule)), true, "the fight controls, rebindable" },
             { std::type_index(typeid(kke::ModelModule)), false, "the fighters' animated bodies" } };
}

void DuelModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_ragdolls = kke::bestRagdollPhysics(app.findCapability<kke::IRagdollPhysics>());

    m_allBots = envOn("KKE_DUEL_BOTS");
    m_quitAfter = envFloat("KKE_DUEL_QUIT", -1.0f);
    m_seed = static_cast<uint32_t>(envFloat("KKE_DUEL_SEED", 1.0f));
    if (const char* l = std::getenv("KKE_DUEL_LEVEL"); l && *l) m_level = l;

    // Controls. Player 1: WASD, J / K / L (or the mouse buttons) to strike,
    // Shift blocks, Space dodges. Player 2 on the same keyboard: the arrow
    // keys and the number pad. Controllers: X punch, Y uppercut, B knee/kick,
    // RB or LT block, A dodge.
    using IM = kke::InputModule;
    m_input->setPlayers(2);
    for (int p = 0; p < 2; ++p) {
        kke::InputMap& in = m_input->map(p);
        kke::InputModule::defineCharacterActions(in);
        // No voice chat here, so B (push-to-talk) stays free; Q is the ping.
        for (const char* a : { "jump", "sprint", "walk", "crouch", "fire", "aim", "interact", "camera.toggle", "camera.zoom", "look", "look.rate", "voice.talk" })
            in.clearBindings(a);
        in.defineAction({ "duel.light", "Punch: jab, cross, hook (press again for the next)", "Fight", "game" });
        in.defineAction({ "duel.heavy", "Uppercut (slow, knocks down)", "Fight", "game" });
        in.defineAction({ "duel.kick", "Knee up close, kick from further (breaks a guard)", "Fight", "game" });
        in.defineAction({ "duel.block", "Block (just in time: parry)", "Fight", "game" });
        in.defineAction({ "duel.dodge", "Dodge", "Fight", "game" });
        in.defineAction({ "duel.again", "Next round / rematch", "Match", "game" });
        in.defineAction({ "duel.two", "Second player takes the red corner", "Match", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        if (p == 1) {
            in.clearBindings("move");
            auto dirKey = [&](SDL_Scancode sc, int component, float scale) {
                kke::Binding b = IM::bind("move", IM::key(sc), kke::Trigger::Continuous);
                b.component = component;
                b.scale = scale;
                in.addBinding(b);
            };
            dirKey(SDL_SCANCODE_UP, 1, 1.0f);
            dirKey(SDL_SCANCODE_DOWN, 1, -1.0f);
            dirKey(SDL_SCANCODE_RIGHT, 0, 1.0f);
            dirKey(SDL_SCANCODE_LEFT, 0, -1.0f);
            kke::Binding stick = IM::bind("move", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTX), kke::Trigger::Continuous);
            stick.sourceY = IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY);
            stick.deadzone = 0.15f;
            stick.invert = true;
            in.addBinding(stick);
            in.addBinding(IM::bind("duel.light", IM::key(SDL_SCANCODE_KP_1)));
            in.addBinding(IM::bind("duel.heavy", IM::key(SDL_SCANCODE_KP_2)));
            in.addBinding(IM::bind("duel.kick", IM::key(SDL_SCANCODE_KP_3)));
            in.addBinding(IM::bind("duel.block", IM::key(SDL_SCANCODE_KP_0), kke::Trigger::Continuous));
            in.addBinding(IM::bind("duel.dodge", IM::key(SDL_SCANCODE_KP_ENTER)));
        } else {
            in.addBinding(IM::bind("duel.light", IM::key(SDL_SCANCODE_J)));
            in.addBinding(IM::bind("duel.light", IM::mouse(SDL_BUTTON_LEFT)));
            in.addBinding(IM::bind("duel.heavy", IM::key(SDL_SCANCODE_K)));
            in.addBinding(IM::bind("duel.heavy", IM::mouse(SDL_BUTTON_RIGHT)));
            in.addBinding(IM::bind("duel.kick", IM::key(SDL_SCANCODE_L)));
            in.addBinding(IM::bind("duel.block", IM::key(SDL_SCANCODE_LSHIFT), kke::Trigger::Continuous));
            in.addBinding(IM::bind("duel.dodge", IM::key(SDL_SCANCODE_SPACE)));
            in.addBinding(IM::bind("duel.again", IM::key(SDL_SCANCODE_R)));
            in.addBinding(IM::bind("duel.two", IM::key(SDL_SCANCODE_F2)));
            in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
        }
        in.addBinding(IM::bind("duel.light", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        in.addBinding(IM::bind("duel.heavy", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
        in.addBinding(IM::bind("duel.kick", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("duel.block", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), kke::Trigger::Continuous));
        kke::Binding lt = IM::bind("duel.block", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), kke::Trigger::Continuous);
        lt.threshold = 0.3f;
        in.addBinding(lt);
        in.addBinding(IM::bind("duel.dodge", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("duel.again", IM::pad(SDL_GAMEPAD_BUTTON_START)));
    }
    m_input->commitDefaults();
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        // Start is the rematch between rounds; the second player joins from
        // the pause menu on a controller (Select pauses), or with F2.
        shell->startIsTheGames = [this] { return m_phase == Phase::RoundOver || m_phase == Phase::MatchOver; };
        shell->addPauseItem("Two players", [this] { setTwoPlayers(true); }, [this] { return !m_twoPlayers && !m_allBots; });
        shell->addPauseItem("Back to the bot", [this] { setTwoPlayers(false); }, [this] { return m_twoPlayers; });
    }

    app.camera().farPlane = 120.0f;
    app.camera().fovDegrees = 50.0f;
    loadCharacter();
    {
        // The bots' tactics are data (data/boxer.yml or .json).
        const char* base = SDL_GetBasePath();
        std::string error;
        if (m_ai.loadSpecies(std::string(base ? base : "") + "data/boxer.yml", &error) == 0)
            kke::log::get(name())->error("data/boxer.yml: {} (the bot will stand still)", error);
    }
    buildArena();
    spawnFighters();
    buildHud();
    setTwoPlayers(false);
    startRound(true);
    kke::log::get(name())->info("{}: {} vs {} ({} bot){}", m_meleeClips ? "UAL 2 melee clips" : "UAL 1 punches only", m_fighters[0].name,
                                m_fighters[1].name, m_level, m_ragdolls ? ", Jolt knockdowns" : ", no ragdoll physics");
}

void DuelModule::buildArena() {
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
    // The gym floor, the raised ring (its canvas at y = 0) and its apron.
    appendBox({ 0.0f, -0.75f, 0.0f }, { 30.0f, 0.25f, 30.0f }, { 0.28f, 0.24f, 0.21f }, v, idx);
    solid({ 0.0f, -0.75f, 0.0f }, { 30.0f, 0.25f, 30.0f });
    // (Faces never share a plane: the apron's top sits under the canvas.)
    appendBox({ 0.0f, -0.29f, 0.0f }, { kRing + 0.6f, 0.23f, kRing + 0.6f }, { 0.12f, 0.16f, 0.3f }, v, idx);
    appendBox({ 0.0f, -0.03f, 0.0f }, { kRing + 0.5f, 0.03f, kRing + 0.5f }, { 0.78f, 0.78f, 0.74f }, v, idx);
    solid({ 0.0f, -0.25f, 0.0f }, { kRing + 0.6f, 0.25f, kRing + 0.6f });
    // The ring's centre logo: two squares.
    appendBox({ 0.0f, 0.003f, 0.0f }, { 0.9f, 0.003f, 0.9f }, { 0.2f, 0.35f, 0.75f }, v, idx);
    appendBox({ 0.0f, 0.009f, 0.0f }, { 0.6f, 0.003f, 0.6f }, { 0.75f, 0.25f, 0.2f }, v, idx);
    // Posts in the corners (blue, red, and two neutral white), three ropes.
    const glm::vec3 postColor[4] = { { 0.2f, 0.4f, 0.95f }, { 0.9f, 0.9f, 0.9f }, { 0.9f, 0.2f, 0.15f }, { 0.9f, 0.9f, 0.9f } };
    const glm::vec2 corners[4] = { { kRing, kRing }, { kRing, -kRing }, { -kRing, -kRing }, { -kRing, kRing } };
    for (int i = 0; i < 4; ++i) {
        const glm::vec3 p(corners[i].x + (corners[i].x > 0 ? 0.25f : -0.25f), 0.7f, corners[i].y + (corners[i].y > 0 ? 0.25f : -0.25f));
        appendBox(p, { 0.08f, 0.7f, 0.08f }, postColor[i], v, idx);
    }
    for (float y : { 0.45f, 0.8f, 1.15f }) {
        const float e = kRing + 0.25f;
        appendBox({ 0.0f, y, e }, { e, 0.02f, 0.02f }, { 0.85f, 0.15f, 0.15f }, v, idx);
        appendBox({ 0.0f, y, -e }, { e, 0.02f, 0.02f }, { 0.85f, 0.15f, 0.15f }, v, idx);
        appendBox({ e, y, 0.0f }, { 0.02f, 0.02f, e }, { 0.85f, 0.85f, 0.85f }, v, idx);
        appendBox({ -e, y, 0.0f }, { 0.02f, 0.02f, e }, { 0.85f, 0.85f, 0.85f }, v, idx);
    }
    // The ropes stop fighters and ragdolls alike.
    const float e = kRing + 0.3f;
    solid({ 0.0f, 0.8f, e }, { e, 0.8f, 0.05f });
    solid({ 0.0f, 0.8f, -e }, { e, 0.8f, 0.05f });
    solid({ e, 0.8f, 0.0f }, { 0.05f, 0.8f, e });
    solid({ -e, 0.8f, 0.0f }, { 0.05f, 0.8f, e });
    // Around the ring: benches and a back wall, so it reads as a gym.
    appendBox({ 0.0f, 3.0f, -12.0f }, { 14.0f, 3.5f, 0.2f }, { 0.36f, 0.33f, 0.3f }, v, idx);
    for (int i = -2; i <= 2; ++i) {
        appendBox({ static_cast<float>(i) * 3.0f, -0.3f, -7.8f }, { 1.2f, 0.2f, 0.3f }, { 0.4f, 0.26f, 0.16f }, v, idx);
        appendBox({ -7.8f, -0.3f, static_cast<float>(i) * 3.0f }, { 0.3f, 0.2f, 1.2f }, { 0.4f, 0.26f, 0.16f }, v, idx);
    }
    m_arena = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_arena->upload(v, idx);

    // Without the mannequin: a block per fighter.
    std::vector<kke::Vertex> bv;
    std::vector<uint32_t> bi;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.28f, 0.9f, 0.2f }, { 0.85f, 0.85f, 0.9f }, bv, bi);
    appendBox({ 0.0f, 1.55f, -0.2f }, { 0.2f, 0.08f, 0.03f }, { 0.1f, 0.1f, 0.15f }, bv, bi);
    m_block = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_block->upload(bv, bi);
}

void DuelModule::spawnFighters() {
    kke::RigidWorld& w = m_rigid->world();
    for (int i = 0; i < 2; ++i) {
        Fighter& f = m_fighters[i];
        f.corner = i;
        f.player = i;
        f.bot = i == 1 || m_allBots;
        f.tint = i == 0 ? glm::vec3(0.35f, 0.6f, 1.25f) : glm::vec3(1.25f, 0.4f, 0.32f);
        f.name = i == 0 ? (m_allBots ? "Blue (bot)" : "You") : "Red";
        f.id = m_combat.add(i, kke::CombatStats::fighter());
        kke::RigidWorld::CharacterDesc cd;
        cd.radius = 0.3f;
        cd.height = 1.8f;
        f.body = w.addCharacter(cd);
        if (f.bot) f.brain = std::make_unique<SparringBot>(m_seed * 7919u + static_cast<uint32_t>(i), SparringBot::skillFor(m_level));
        setupBody(f);
    }
}

void DuelModule::setTwoPlayers(bool on) {
    m_twoPlayers = on && !m_allBots;
    Fighter& red = m_fighters[1];
    red.bot = !m_twoPlayers;
    red.name = m_twoPlayers ? "Player 2" : "Red";
    if (red.bot && !red.brain) red.brain = std::make_unique<SparringBot>(m_seed * 7919u + 1u, SparringBot::skillFor(m_level));
    if (!red.bot) red.brain.reset();
    syncAi();
    // Devices: the keyboard serves both (different keys); with two
    // controllers each player has one, with one it's player 2's.
    std::vector<uint32_t> pads, rest;
    for (const auto& d : m_input->devices().devices()) {
        if (!d.connected) continue;
        if (d.kind == kke::InputDevices::Kind::Gamepad) pads.push_back(d.ref);
        else rest.push_back(d.ref);
    }
    if (!m_twoPlayers) {
        m_input->assignDevices(0, {});
        m_input->assignDevices(1, {});
        return;
    }
    std::vector<uint32_t> one = rest, two = rest;
    if (pads.size() >= 2) {
        one.push_back(pads[0]);
        two.push_back(pads[1]);
    } else if (pads.size() == 1) {
        two.push_back(pads[0]);
    }
    m_input->assignDevices(0, one);
    m_input->assignDevices(1, two);
}

void DuelModule::syncAi() {
    // Agents for the bots, actors (seen, not steered) for players.
    // Re-added each round, so nothing is remembered across rounds.
    const SparringBot::Skill skill = SparringBot::skillFor(m_level);
    for (int i = 0; i < 2; ++i) {
        const Fighter& f = m_fighters[i];
        const kke::ai::AgentId id = kke::ai::AgentId(i + 1);
        m_ai.remove(id);
        const glm::vec3 at = m_rigid->world().characterPosition(f.body);
        const bool ok = f.bot ? m_ai.addAgent(id, "boxer", at, i == 0 ? 180.0f : 0.0f) : m_ai.addActor(id, "boxer", at);
        if (!ok) continue;
        m_ai.setTeam(id, uint32_t(i + 1));
        if (f.bot) m_ai.setMood(id, { 0.9f, 0.0f, skill.aggression, 0.0f });
    }
}

void DuelModule::thinkAi(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    for (int i = 0; i < 2; ++i) {
        Fighter& f = m_fighters[i];
        const kke::ai::AgentId id = kke::ai::AgentId(i + 1);
        const float yaw = glm::degrees(std::atan2(f.facing.x, f.facing.z));
        m_ai.setTransform(id, w.characterPosition(f.body), w.characterVelocity(f.body), yaw);
        if (f.bot && f.brain) f.brain->sense(m_ai, id, m_combat.get(f.id), m_combat.get(m_fighters[1 - i].id), dt);
    }
    m_ai.update(dt);
    for (const kke::ai::AiEvent& e : m_ai.takeEvents()) {
        if (e.kind != kke::ai::AiEvent::Kind::Attack || e.who < 1 || e.who > 2) continue;
        Fighter& f = m_fighters[e.who - 1];
        if (f.bot && f.brain) f.brain->strike();
    }
}

void DuelModule::startRound(bool newMatch) {
    if (newMatch) {
        m_round = 1;
        for (Fighter& f : m_fighters) f.wins = 0;
        for (Tally& t : m_tally) t = Tally{};
    }
    kke::RigidWorld& w = m_rigid->world();
    for (int i = 0; i < 2; ++i) {
        Fighter& f = m_fighters[i];
        if (f.ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(f.ragdoll);
        f.ragdoll = 0;
        f.getUpAge = -1.0f;
        f.getUpFrom.clear();
        if (f.model) m_models->setBoneWorldOverride(f.model, {});
        m_combat.get(f.id).reset();
        const float z = i == 0 ? kStart : -kStart;
        w.teleportCharacter(f.body, glm::vec3(0.0f, 0.02f, z));
        w.setCharacterVelocity(f.body, glm::vec3(0.0f));
        f.facing = glm::vec3(0.0f, 0.0f, i == 0 ? -1.0f : 1.0f);
        f.push = glm::vec3(0.0f);
        f.lastState = -1;
        f.intent = Intent{};
        if (f.anim && m_st.idle >= 0) f.anim->play(m_st.idle, 0.0f, true);
    }
    m_phase = Phase::Intro;
    m_phaseTime = 0.0f;
    m_roundWinner.clear();
    syncAi();
}

void DuelModule::onEvent(const SDL_Event&) {}

DuelModule::Intent DuelModule::readPlayer(Fighter& f) {
    kke::InputMap& in = m_input->map(f.player);
    Intent it;
    // The stick is in screen space: turn it into the fighter's own frame
    // (x = to its right, y = toward the opponent).
    const kke::Camera& cam = m_app->camera();
    const glm::vec3 camFwd = flat(cam.target - cam.position);
    const glm::vec3 camRight = glm::normalize(glm::cross(camFwd, glm::vec3(0, 1, 0)));
    const glm::vec2 m = in.axis2("move");
    const glm::vec3 world = camRight * m.x + camFwd * m.y;
    const glm::vec3 right = glm::normalize(glm::cross(f.facing, glm::vec3(0, 1, 0)));
    it.move = glm::vec2(glm::dot(world, right), glm::dot(world, f.facing));
    it.light = in.pressed("duel.light");
    it.heavy = in.pressed("duel.heavy");
    it.kick = in.pressed("duel.kick");
    it.dodge = in.pressed("duel.dodge");
    it.block = in.held("duel.block");
    return it;
}

void DuelModule::updateFighter(Fighter& f, Fighter& other, float dt) {
    kke::RigidWorld& w = m_rigid->world();
    kke::Combatant& c = m_combat.get(f.id);
    const kke::Combatant& oc = m_combat.get(other.id);
    const glm::vec3 feet = w.characterPosition(f.body);
    const glm::vec3 otherFeet = w.characterPosition(other.body);
    const float distance = glm::length(glm::vec2(otherFeet.x - feet.x, otherFeet.z - feet.z));

    Intent in;
    if (m_phase == Phase::Fight) in = f.bot ? f.brain->think(m_ai, kke::ai::AgentId(f.corner + 1), c, oc, f.facing, distance, dt) : readPlayer(f);
    f.intent = in;

    using S = kke::Combatant::State;
    // Always square up to the opponent (lock-on), but not mid-swing: a
    // committed punch goes where it was aimed.
    const bool committed = c.state() == S::Active || c.state() == S::Recovery;
    if (c.state() != S::Knockdown && c.state() != S::Dead && !committed) {
        const glm::vec3 want = flat(otherFeet - feet);
        const float k = 1.0f - std::exp(-(c.state() == S::Windup ? 6.0f : 14.0f) * dt);
        f.facing = flat(f.facing + (want - f.facing) * k);
    }
    c.place(feet, f.facing);

    throwStrike(f, c, distance, dt);
    if (in.dodge && c.dodge()) f.dodgeSide = std::abs(in.move.x) > 0.3f ? (in.move.x > 0.0f ? 1.0f : -1.0f) : 0.0f;
    c.setBlocking(in.block);

    // Footwork.
    const glm::vec3 right = glm::normalize(glm::cross(f.facing, glm::vec3(0, 1, 0)));
    glm::vec3 wish = right * in.move.x + f.facing * in.move.y;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    glm::vec3 vel(0.0f);
    switch (c.state()) {
    case S::Idle: vel = wish * (c.blocking() ? kBlockWalk : kWalk); break;
    case S::Windup: vel = f.facing * 0.5f + wish * 0.4f; break;  // stepping into it
    case S::Active: vel = f.facing * 1.2f; break;
    case S::Recovery: vel = wish * 0.4f; break;
    case S::Dodging: {
        // Away from the opponent, or to the side the stick says.
        glm::vec3 d = glm::length(wish) > 0.2f ? wish : -f.facing;
        if (glm::dot(d, f.facing) > 0.3f) d = right * (in.move.x >= 0.0f ? 1.0f : -1.0f); // no dodging forward
        vel = glm::normalize(d) * kDodgeSpeed * (1.0f - c.stateTime() / std::max(0.01f, c.stateLength()) * 0.6f);
        break;
    }
    default: break; // stunned, down: only the knockback moves them
    }
    vel += f.push;
    f.push *= std::exp(-7.0f * dt);
    // Never through each other: take out any closing speed at the minimum gap.
    const glm::vec3 toOther = flat(otherFeet - feet);
    const float closing = glm::dot(vel, toOther);
    if (distance < kMinGap && closing > 0.0f && oc.state() != S::Knockdown && oc.state() != S::Dead) vel -= toOther * closing;
    if (distance < kMinGap * 0.9f) vel -= toOther * (kMinGap - distance) * 6.0f;

    const bool down = f.ragdoll != 0;
    if (!down) {
        kke::RigidWorld::CharacterInput ci;
        ci.move = vel;
        w.setCharacterInput(f.body, ci);
    } else {
        w.setCharacterInput(f.body, kke::RigidWorld::CharacterInput{});
    }

    // On the ground: the ragdoll lies, then the fighter gets up (a knockout stays).
    if (down) {
        f.downTime += dt;
        if (c.alive() && f.downTime >= kRagdollTime) getUp(f);
    }
}

void DuelModule::onHit(const kke::HitEvent& e) {
    Fighter& target = fighter(e.target);
    Fighter& attacker = fighter(e.attacker);
    Tally& t = m_tally[attacker.corner];
    target.lastHit = e.attack;
    using O = kke::HitOutcome;
    switch (e.outcome) {
    case O::Hit: ++t.hits; target.push += e.push; break;
    case O::Blocked: ++m_tally[target.corner].blocks; target.push += e.push; break;
    case O::GuardBroke: ++t.guardBreaks; target.push += e.push; break;
    case O::Parried: ++m_tally[target.corner].parries; attacker.push -= attacker.facing * 1.5f; break;
    case O::Knockdown:
        ++t.hits;
        ++t.knockdowns;
        knockDown(target, e.push * 1.6f + glm::vec3(0.0f, 1.0f, 0.0f));
        break;
    case O::Killed:
        ++t.hits;
        knockDown(target, e.push * 2.0f + glm::vec3(0.0f, 1.5f, 0.0f));
        if (m_phase == Phase::Fight) {
            ++attacker.wins;
            m_roundWinner = attacker.name;
            m_phase = attacker.wins >= 2 ? Phase::MatchOver : Phase::RoundOver;
            m_phaseTime = 0.0f;
            kke::log::get(name())->info("round {}: {} knocked out {} ({} - {})", m_round, attacker.name, target.name, m_fighters[0].wins,
                                        m_fighters[1].wins);
        }
        break;
    }
}

void DuelModule::knockDown(Fighter& f, const glm::vec3& push) {
    f.downTime = 0.0f;
    f.push = glm::vec3(0.0f);
    if (!m_ragdolls || !f.model || f.ragdoll) return;
    const kke::ModelData* d = m_models->model(m_charModel);
    const glm::mat4 instance = m_models->transform(f.model);
    std::vector<glm::mat4> world = m_models->boneWorld(f.model);
    for (glm::mat4& b : world) b = instance * b;
    std::string missing;
    f.ragdollDesc = kke::buildHumanoidRagdoll(m_rigData, world, 75.0f, &missing);
    if (f.ragdollDesc.bodies.empty() || !d) {
        kke::log::get(name())->warn("no knockdown ragdoll: the skeleton has no '{}' bone", missing);
        return;
    }
    f.binding = kke::bindSkeletonToRagdoll(m_rigData, world, f.ragdollDesc);
    f.ragdoll = m_ragdolls->createRagdoll(f.ragdollDesc, push * 0.3f);
    if (!f.ragdoll) return;
    // The head and chest take the blow; the legs go out from under.
    for (const char* b : { "torso", "head" }) m_ragdolls->pushRagdollBody(f.ragdoll, f.ragdollDesc.findBody(b), push);
    m_ragdolls->pushRagdollBody(f.ragdoll, f.ragdollDesc.findBody("pelvis"), push * 0.3f);
    f.getUpAge = -1.0f;
}

void DuelModule::getUp(Fighter& f) {
    if (!f.ragdoll) return;
    std::vector<glm::mat4> bodies;
    const int pelvis = f.ragdollDesc.findBody("pelvis");
    kke::RigidWorld& w = m_rigid->world();
    if (pelvis >= 0 && m_ragdolls->ragdollBodyTransforms(f.ragdoll, bodies)) {
        // Up where the body lies, inside the ropes.
        glm::vec3 p(bodies[static_cast<size_t>(pelvis)][3]);
        p.x = std::clamp(p.x, -kRing + 0.5f, kRing - 0.5f);
        p.z = std::clamp(p.z, -kRing + 0.5f, kRing - 0.5f);
        w.teleportCharacter(f.body, glm::vec3(p.x, 0.02f, p.z));
        const float yaw = m_modelYaw - glm::degrees(std::atan2(f.facing.x, -f.facing.z));
        const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(p.x, 0.0f, p.z)), glm::radians(yaw), glm::vec3(0, 1, 0));
        m_models->setTransform(f.model, xf);
        f.getUpFrom = kke::poseFromRagdoll(m_rigData, f.binding, bodies, glm::inverse(xf));
        f.getUpAge = 0.0f;
    }
    m_ragdolls->destroyRagdoll(f.ragdoll);
    f.ragdoll = 0;
    if (f.anim && m_st.getUp >= 0) f.anim->play(m_st.getUp, 0.0f, true);
}

void DuelModule::updateCamera(float dt) {
    // A fighting-game camera: side on to the line between the fighters,
    // far enough back to keep both in frame.
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 a = w.characterPosition(m_fighters[0].body), b = w.characterPosition(m_fighters[1].body);
    const glm::vec3 mid = (a + b) * 0.5f + glm::vec3(0.0f, 1.05f, 0.0f);
    const glm::vec3 axis = flat(b - a);
    glm::vec3 side = glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0))) * m_camSide;
    // Stay on whichever side the camera already is (no flip when they cross).
    if (m_camInit && glm::dot(side, flat(m_app->camera().position - m_camMid)) < 0.0f) {
        m_camSide = -m_camSide;
        side = -side;
    }
    const float sep = glm::length(glm::vec2(b.x - a.x, b.z - a.z));
    const float dist = std::max(4.2f, sep * 1.25f + 2.6f);
    const float k = m_camInit ? 1.0f - std::exp(-4.0f * dt) : 1.0f;
    m_camMid += (mid - m_camMid) * k;
    m_camDist += (dist - m_camDist) * k;
    kke::Camera& cam = m_app->camera();
    const glm::vec3 wantPos = m_camMid + side * m_camDist + glm::vec3(0.0f, 0.75f, 0.0f);
    cam.position = m_camInit ? cam.position + (wantPos - cam.position) * k : wantPos;
    cam.target = m_camMid;
    m_camInit = true;
}

void DuelModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    kke::InputMap& p1 = m_input->map(0);
    if (p1.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    if (p1.pressed("duel.two") || m_input->map(1).pressed("duel.two")) setTwoPlayers(!m_twoPlayers);
    if (m_twoPlayers) setTwoPlayers(true); // controllers come and go

    m_phaseTime += dt;
    switch (m_phase) {
    case Phase::Intro:
        if (m_phaseTime > 2.0f) {
            m_phase = Phase::Fight;
            m_phaseTime = 0.0f;
        }
        break;
    case Phase::Fight:
        if (m_phaseTime > kRoundTime) {
            // Time: the healthier fighter takes the round.
            const float h0 = m_combat.get(m_fighters[0].id).health(), h1 = m_combat.get(m_fighters[1].id).health();
            Fighter& winner = h0 >= h1 ? m_fighters[0] : m_fighters[1];
            ++winner.wins;
            m_roundWinner = winner.name;
            m_phase = winner.wins >= 2 ? Phase::MatchOver : Phase::RoundOver;
            m_phaseTime = 0.0f;
        }
        break;
    case Phase::RoundOver:
        if (m_phaseTime > 3.5f || (m_phaseTime > 1.0f && p1.pressed("duel.again"))) {
            ++m_round;
            startRound(false);
        }
        break;
    case Phase::MatchOver:
        if (p1.pressed("duel.again") || m_input->map(1).pressed("duel.again") || (m_allBots && m_phaseTime > 4.0f)) startRound(true);
        break;
    }

    if (m_phase == Phase::Fight) thinkAi(dt);
    updateFighter(m_fighters[0], m_fighters[1], dt);
    updateFighter(m_fighters[1], m_fighters[0], dt);
    for (const kke::HitEvent& e : m_combat.step(dt)) onHit(e);
    for (int i = 0; i < 2; ++i) animateBody(m_fighters[i], m_fighters[1 - i], dt);
    updateCamera(dt);
    updateHud();

    m_clock += dt;
    if (m_quitAfter > 0.0f) {
        if (m_clock >= m_reportAt) {
            m_reportAt += 10.0f;
            for (int i = 0; i < 2; ++i) {
                const kke::Combatant& c = m_combat.get(m_fighters[i].id);
                const Tally& t = m_tally[i];
                kke::log::get(name())->info("t {:.0f} s: {} health {:.0f} stamina {:.0f} wins {}; landed {}, blocked {}, parried {}, guard breaks {}, "
                                            "knockdowns {}{}{}",
                                            m_clock, m_fighters[i].name, c.health(), c.stamina(), m_fighters[i].wins, t.hits, t.blocks, t.parries,
                                            t.guardBreaks, t.knockdowns, m_fighters[i].brain ? "; tactic " : "",
                                            m_fighters[i].brain ? m_fighters[i].brain->tactic() : std::string());
                std::string thrown;
                for (const auto& [strike, n] : t.thrown) thrown += (thrown.empty() ? "" : ", ") + strike + " " + std::to_string(n);
                if (!thrown.empty()) kke::log::get(name())->info("  {} threw: {}", m_fighters[i].name, thrown);
            }
        }
        if (m_clock >= m_quitAfter) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void DuelModule::render(const kke::RenderContext& ctx) {
    m_arena->draw(ctx, glm::mat4(1.0f), 0.0f, 0.85f);
    if (m_charModel) return;
    kke::RigidWorld& w = m_rigid->world();
    for (const Fighter& f : m_fighters) {
        const float yaw = glm::degrees(std::atan2(f.facing.x, -f.facing.z));
        m_block->draw(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), w.characterPosition(f.body)), glm::radians(-yaw), glm::vec3(0, 1, 0)), 0.0f, 0.6f);
    }
}

void DuelModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_arena->drawShadow(ctx, glm::mat4(1.0f));
    if (m_charModel) return;
    kke::RigidWorld& w = m_rigid->world();
    for (const Fighter& f : m_fighters) {
        const float yaw = glm::degrees(std::atan2(f.facing.x, -f.facing.z));
        m_block->drawShadow(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), w.characterPosition(f.body)), glm::radians(-yaw), glm::vec3(0, 1, 0)));
    }
}

} // namespace duel
