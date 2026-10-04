// The flow of a game of Goblin Horde: the start menu, the waves, the
// arena, the cameras (one per player at this screen: split screen), the
// headless report. See HordeModule.h and README.md.

#include "HordeModule.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/Viewports.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <typeindex>

namespace horde {

namespace {

constexpr float kFort = 13.0f;    // half the width of the ruined fort's walls
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

const glm::vec3 kSeatColors[] = { { 0.95f, 0.72f, 0.25f }, { 0.35f, 0.65f, 1.0f }, { 0.4f, 0.85f, 0.4f }, { 0.9f, 0.4f, 0.75f } };

} // namespace

HordeModule::HordeModule() = default;
HordeModule::~HordeModule() = default;

std::vector<kke::ModuleDependency> HordeModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the ruins, the players' bodies and the goblins' ragdolls (Jolt)" },
             { std::type_index(typeid(kke::InputModule)), true, "the controls, rebindable" },
             { std::type_index(typeid(kke::ModelModule)), true, "the players and the goblins" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join on any controller and pick who they are" },
             { std::type_index(typeid(kke::NetModule)), false, "playing online" } };
}

void HordeModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    m_net = app.getModule<kke::NetModule>();
    m_ragdolls = kke::bestRagdollPhysics(app.findCapability<kke::IRagdollPhysics>());

    if (const char* b = std::getenv("KKE_HORDE_BOT"); b && *b == '1') m_bot = true;
    m_quitAfter = envFloat("KKE_HORDE_QUIT", -1.0f);
    m_maxAlive = std::max(1, static_cast<int>(envFloat("KKE_HORDE_MAX", 60.0f)));
    m_startWave = std::max(1, static_cast<int>(envFloat("KKE_HORDE_WAVE", 1.0f)));
    m_ragdollCap = std::max(0, static_cast<int>(envFloat("KKE_HORDE_RAGDOLLS", 12.0f)));
    m_forceBoss = envFloat("KKE_HORDE_BOSS", 0.0f) > 0.5f;
    m_lineup = envFloat("KKE_HORDE_LINEUP", 0.0f) > 0.5f;
    if (const char* pose = std::getenv("KKE_HORDE_POSE")) m_poseClip = pose;
    if (const char* w = std::getenv("KKE_HORDE_WEAPON")) m_startWeapon = w;

    defineActions();
    app.window().setQuitOnEscape(false); // Esc lets go of the mouse, or pauses
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        // Start is "try again" once the horde has won; otherwise it pauses.
        shell->startIsTheGames = [this] { return m_phase == Phase::Overrun; };
        shell->onMainMenu = [this] {
            if (m_phase != Phase::Lobby) backToLobby();
        };
        shell->addPauseItem("Inventory", [this, shell] {
            shell->closeMenu();
            openInventory(0);
        }, [this] { return m_phase != Phase::Lobby && !m_heroes.empty(); });
        shell->addPauseItem("Start over", [this] { restart(); }, [this] { return m_phase != Phase::Lobby && !netClient(); });
    }

    app.camera().farPlane = 200.0f;
    loadData();

    // The art (only the packs and clips the data names).
    std::set<std::string> clips;
    std::set<std::string> packs;
    auto addMove = [&](const Move& m) {
        clips.insert(m.clips.begin(), m.clips.end());
        clips.insert(m.aimClips.begin(), m.aimClips.end());
        clips.insert(m.roarClips.begin(), m.roarClips.end());
    };
    auto addArt = [&](const ArtRef& a) {
        if (!a.pack.empty()) packs.insert(a.pack);
    };
    for (const auto* list : { &m_roster.types, &m_roster.bosses })
        for (const FoeType& t : *list) {
            for (const Move& m : t.moves) addMove(m);
            for (const auto* arts : { &t.models, &t.weapons, &t.offhands })
                for (const ArtRef& a : *arts) addArt(a);
            addArt(t.back);
        }
    for (const Weapon& w : m_roster.weapons) {
        for (const Move& m : w.combo) addMove(m);
        for (const Move* m : { &w.heavy, &w.spin, &w.kick }) addMove(*m);
        addArt(w.prop);
        addArt(w.arrow);
    }
    for (const HeroCharacter& c : m_roster.characters) addArt(c.art);
    for (const Accessory& a : m_roster.accessories) addArt(a.art);
    m_art = std::make_unique<Art>(*m_models);
    m_art->init(clips, std::vector<std::string>(packs.begin(), packs.end()));
    if (const Weapon* bow = m_roster.weapons.empty() ? nullptr : &m_roster.weapons.front(); bow)
        for (const Weapon& w : m_roster.weapons)
            if (w.ranged() && !m_arrowModel) m_arrowModel = m_art->prop(w.arrow);

    {
        // The minds (data/goblin.yml or .json).
        const char* base = SDL_GetBasePath();
        std::string error;
        if (m_ai.loadSpecies(std::string(base ? base : "") + "data/goblin.yml", &error) == 0)
            kke::log::get(name())->error("data/goblin.yml: {} (goblins will stand still)", error);
    }
    buildArena();
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_fx = std::make_unique<kke::ParticleEffects>(app);
    m_marks = std::make_unique<kke::DynamicMeshRenderer>(app);
    setupLobby();
    setupNet();
    buildHud();
    buildHeroes(wantedHeroes());
    restart();
    m_phase = Phase::Lobby;
    // The bot fights straight away (headless checks); KKE_HORDE_LOBBY=0 too.
    const char* lobby = std::getenv("KKE_HORDE_LOBBY");
    if (!m_lobby || !m_lobby->isOpen() || m_bot || m_lineup || (lobby && *lobby == '0')) startFromLobby();

    size_t looks = 0;
    for (const FoeType& t : m_roster.types) looks += t.models.size();
    kke::log::get(name())->info("{} goblin types ({} looks), {} bosses, {} characters, {} weapons; up to {} goblins alive, {} ragdolls",
                                m_roster.types.size(), looks, m_roster.bosses.size(), m_roster.characters.size(), m_roster.weapons.size(), m_maxAlive,
                                m_ragdollCap);
}

void HordeModule::defineActions() {
    using IM = kke::InputModule;
    // Every seat's map: the lobby gives each player their own devices.
    for (int p = 0; p < 4; ++p) {
        m_input->setPlayers(p + 1);
        kke::InputMap& in = m_input->map(p);
        kke::InputModule::defineCharacterActions(in);
        for (const char* a : { "jump", "sprint", "walk", "crouch", "fire", "aim", "interact", "camera.toggle", "camera.zoom", "voice.talk" })
            in.clearBindings(a);
        in.defineAction({ "horde.attack", "Attack (combo) / draw the bow / shoot", "Fight", "game" });
        in.defineAction({ "horde.heavy", "Heavy swing (hold: spin) / kick", "Fight", "game" });
        in.defineAction({ "horde.block", "Block (just in time: parry)", "Fight", "game" });
        in.defineAction({ "horde.aim", "Aim (bow and crossbow)", "Fight", "game" });
        in.defineAction({ "horde.roll", "Roll", "Fight", "game" });
        in.defineAction({ "horde.swap", "Next weapon", "Fight", "game" });
        in.defineAction({ "horde.inventory", "Inventory", "Game", "game" });
        in.defineAction({ "horde.again", "Try again", "Game", "game" });
        in.defineAction({ "horde.up", "Menu up", "Menus", "game" });
        in.defineAction({ "horde.down", "Menu down", "Menus", "game" });
        in.defineAction({ "horde.accept", "Menu choose", "Menus", "game" });
        in.defineAction({ "horde.back", "Menu back", "Menus", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        // Keyboard and mouse: left mouse attacks (hold to draw a bow),
        // right mouse aims a bow (or swings heavy), F heavy, Shift block,
        // Space roll, Q next weapon, Tab inventory.
        in.addBinding(IM::bind("horde.attack", IM::mouse(SDL_BUTTON_LEFT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.attack", IM::key(SDL_SCANCODE_J), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.heavy", IM::key(SDL_SCANCODE_F), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.heavy", IM::key(SDL_SCANCODE_K), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.heavy", IM::mouse(SDL_BUTTON_MIDDLE), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.aim", IM::mouse(SDL_BUTTON_RIGHT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.block", IM::key(SDL_SCANCODE_LSHIFT), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.roll", IM::key(SDL_SCANCODE_SPACE)));
        in.addBinding(IM::bind("horde.swap", IM::key(SDL_SCANCODE_Q)));
        in.addBinding(IM::bind("horde.inventory", IM::key(SDL_SCANCODE_TAB)));
        in.addBinding(IM::bind("horde.inventory", IM::key(SDL_SCANCODE_I)));
        in.addBinding(IM::bind("horde.again", IM::key(SDL_SCANCODE_R)));
        in.addBinding(IM::bind("horde.up", IM::key(SDL_SCANCODE_UP)));
        in.addBinding(IM::bind("horde.up", IM::key(SDL_SCANCODE_W)));
        in.addBinding(IM::bind("horde.down", IM::key(SDL_SCANCODE_DOWN)));
        in.addBinding(IM::bind("horde.down", IM::key(SDL_SCANCODE_S)));
        in.addBinding(IM::bind("horde.accept", IM::key(SDL_SCANCODE_RETURN)));
        in.addBinding(IM::bind("horde.accept", IM::key(SDL_SCANCODE_E)));
        in.addBinding(IM::bind("horde.back", IM::key(SDL_SCANCODE_BACKSPACE)));
        // A controller: X attack, Y heavy (hold: spin), RT also attacks
        // (shoots), LT aims, RB blocks, A rolls, d-pad right swaps,
        // d-pad up opens the inventory.
        in.addBinding(IM::bind("horde.attack", IM::pad(SDL_GAMEPAD_BUTTON_WEST), kke::Trigger::Continuous));
        kke::Binding rt = IM::bind("horde.attack", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), kke::Trigger::Continuous);
        rt.threshold = 0.35f;
        in.addBinding(rt);
        in.addBinding(IM::bind("horde.heavy", IM::pad(SDL_GAMEPAD_BUTTON_NORTH), kke::Trigger::Continuous));
        kke::Binding lt = IM::bind("horde.aim", IM::padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 1), kke::Trigger::Continuous);
        lt.threshold = 0.3f;
        in.addBinding(lt);
        in.addBinding(IM::bind("horde.block", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), kke::Trigger::Continuous));
        in.addBinding(IM::bind("horde.roll", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("horde.swap", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)));
        in.addBinding(IM::bind("horde.inventory", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
        in.addBinding(IM::bind("horde.again", IM::pad(SDL_GAMEPAD_BUTTON_START)));
        in.addBinding(IM::bind("horde.up", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
        in.addBinding(IM::bind("horde.down", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)));
        in.addBinding(IM::bind("horde.accept", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
        in.addBinding(IM::bind("horde.back", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
    }
    m_input->setPlayers(1);
    kke::TouchLayoutOptions touch; // touch: the fight's buttons first
    touch.buttons = { "horde.attack", "horde.roll", "horde.heavy", "horde.block", "horde.aim", "horde.swap" };
    m_input->setTouchLayout(touch);
    m_input->commitDefaults();
}

void HordeModule::loadData() {
    auto log = kke::log::get(name());
    const char* base = SDL_GetBasePath();
    const std::string dir = std::string(base ? base : "") + "data/";
    auto read = [&](const char* file, auto&& apply) {
        nlohmann::json j;
        std::string error;
        if (!kke::datafile::loadPath(dir + file, j, &error)) {
            log->error("data/{}: {}", file, error);
            return;
        }
        if (!apply(j, &error)) log->error("data/{}: {}", file, error);
    };
    read("foes.yml", [&](const nlohmann::json& j, std::string* e) { return m_roster.readFoes(j, e); });
    read("heroes.yml", [&](const nlohmann::json& j, std::string* e) { return m_roster.readHeroes(j, e); });
    read("waves.yml", [&](const nlohmann::json& j, std::string* e) { return m_roster.readWaves(j, e); });
    if (m_roster.characters.empty()) m_roster.characters.push_back({ "mannequin", "Mannequin", {} });
    if (m_roster.weapons.empty()) {
        Weapon w;
        w.id = "fists";
        w.label = "Fists";
        Move m;
        m.hit = kke::AttackDesc::light();
        m.clips = { "Punch_Jab" };
        w.combo = { m, m, m };
        w.heavy = w.spin = w.kick = m;
        m_roster.weapons.push_back(w);
    }
    if (m_roster.waves.empty()) m_roster.waves.push_back(Wave{});
}

void HordeModule::setupLobby() {
    if (!m_lobby) return;
    kke::Lobby& l = m_lobby->lobby();
    std::vector<std::string> chars, skins, accs, weapons;
    for (const HeroCharacter& c : m_roster.characters) chars.push_back(c.label);
    for (int i = 0; i < 6; ++i) skins.push_back(i == 0 ? "As made" : "Colours " + std::to_string(i + 1));
    for (const Accessory& a : m_roster.accessories) accs.push_back(a.label);
    for (const Weapon& w : m_roster.weapons) weapons.push_back(w.label);
    l.addLookField({ "name", "Name", { "Aldric", "Brenna", "Corin", "Dagny", "Edda", "Finn", "Greer", "Hale" }, {} });
    l.addLookField({ "character", "Character", chars, {} });
    l.addLookField({ "skin", "Skin", skins, {} });
    l.addLookField({ "accessory", "Accessory", accs, {} });
    l.addLookField({ "weapon", "Weapon", weapons, {} });
    l.addLookField({ "colour", "Colour", { "Gold", "Sky", "Moss", "Rose" }, { kSeatColors[0], kSeatColors[1], kSeatColors[2], kSeatColors[3] } });
    l.setMaxCpus(0); // the horde is the CPU
    std::vector<std::string> waves;
    for (int w : { 1, 3, 5, 8 }) waves.push_back("Wave " + std::to_string(w));
    l.addOption({ "start", "Start at", waves, 0, true, {}, {} });
    m_lobby->load();
    // Different seats default to different looks.
    for (int s = 1; s < kke::Lobby::kMaxSeats; ++s) {
        const kke::Lobby::Seat& seat = l.seat(s);
        if (seat.look.size() >= 6 && seat.look[0] == 0 && seat.look[1] == 0) {
            l.setLook(s, 0, s);
            l.setLook(s, 1, s % static_cast<int>(chars.size()));
            l.setLook(s, 4, s % static_cast<int>(weapons.size()));
            l.setLook(s, 5, s % 4);
        }
    }
    m_lobby->setTitle("GOBLIN HORDE", "Hold the ruins together. Another controller? Press {a} on it to join.");
}

void HordeModule::startFromLobby() {
    if (netClient()) {
        if (m_lobby) m_lobby->lobby().toast("The host starts the fight", 3.0f);
        return;
    }
    if (m_lobby && m_lobby->isOpen()) {
        m_lobby->save();
        m_lobby->close();
    }
    if (m_lobby) {
        if (const kke::Lobby::Option* o = m_lobby->lobby().option("start")) m_startWave = std::max(m_startWave, std::array<int, 4>{ 1, 3, 5, 8 }[static_cast<size_t>(std::clamp(o->value, 0, 3))]);
        m_lobby->applyInput();
    }
    if (netHost()) syncNetPlayers();
    buildHeroes(wantedHeroes());
    m_phase = Phase::Intro;
    restart();
    int here = 0, online = 0;
    for (const auto& h : m_heroes) (h->remote ? online : here) += 1;
    kke::log::get(name())->info("the fight starts at wave {}: {} playing here, {} online", m_startWave, here, online);
}

void HordeModule::backToLobby() {
    for (auto& f : m_foes) releaseFoe(*f);
    m_foes.clear();
    m_shots.clear();
    m_markList.clear();
    m_blasts.clear();
    m_phase = Phase::Lobby;
    m_app->views().clear();
    if (m_lobby) m_lobby->open();
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
            appendBox(base + glm::vec3(0.0f, hgt * (0.45f + 0.2f * static_cast<float>(k)), 0.0f), { s, hgt * 0.13f, s },
                      { 0.09f, 0.2f + 0.03f * static_cast<float>(k), 0.12f }, v, idx);
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

    // Stand-in for anyone without art (no animation library at all).
    std::vector<kke::Vertex> bv;
    std::vector<uint32_t> bi;
    appendBox({ 0.0f, 0.9f, 0.0f }, { 0.3f, 0.9f, 0.22f }, { 1.0f, 1.0f, 1.0f }, bv, bi);
    m_block = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_block->upload(bv, bi);

    // A crossbow's stock (no pack has a crossbow: a short bow laid flat on
    // this), along the prop's +z, the grip near the back.
    std::vector<kke::Vertex> sv;
    std::vector<uint32_t> si;
    appendBox({ 0.0f, -0.03f, 0.05f }, { 0.025f, 0.03f, 0.3f }, { 0.36f, 0.22f, 0.12f }, sv, si);
    appendBox({ 0.0f, -0.09f, -0.12f }, { 0.02f, 0.05f, 0.025f }, { 0.3f, 0.18f, 0.1f }, sv, si);
    m_stock = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_stock->upload(sv, si);
}

float HordeModule::groundAt(const glm::vec3& p) const {
    const auto hit = m_rigid->world().raycast(p + glm::vec3(0.0f, 3.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 10.0f);
    return hit.hit ? p.y + 3.0f - hit.distance : 0.0f;
}

bool HordeModule::lineBlocked(const glm::vec3& a, const glm::vec3& b) const {
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-3f) return false;
    const auto hit = m_rigid->world().raycast(a, d / len, len);
    return hit.hit;
}

void HordeModule::restart() {
    for (auto& f : m_foes) releaseFoe(*f);
    m_foes.clear();
    m_ragdolled.clear();
    for (Shot& s : m_shots)
        if (s.model) {
            m_models->setVisible(s.model, false);
            m_arrowPool.push_back(s.model);
        }
    m_shots.clear();
    m_markList.clear();
    m_blasts.clear();
    if (m_fx) m_fx->clear();
    int n = 0;
    const int count = std::max(1, static_cast<int>(m_heroes.size()));
    for (auto& hp : m_heroes) {
        Hero& h = *hp;
        const float a = 6.283f * static_cast<float>(n++) / static_cast<float>(count);
        const glm::vec3 at = count == 1 ? glm::vec3(0.0f, 0.05f, 2.0f) : glm::vec3(std::sin(a) * 1.6f, 0.05f, 2.0f + std::cos(a) * 1.6f);
        if (h.id) m_combat.get(h.id).reset();
        h.downTime = 0.0f;
        h.kills = 0;
        h.combo = 0;
        h.queued = false;
        h.heavyHeld = -1.0f;
        h.spin = 0.0f;
        h.draw = h.reload = 0.0f;
        h.drawing = false;
        h.look.lastState = -1;
        h.facing = glm::vec3(0.0f, 0.0f, 1.0f);
        h.push = glm::vec3(0.0f);
        if (!h.remote && h.body) {
            m_rigid->world().teleportCharacter(h.body, at);
            m_rigid->world().setCharacterVelocity(h.body, glm::vec3(0.0f));
        }
        h.rig.yaw = 180.0f;
        h.rig.pitch = -18.0f;
        if (h.ik) h.ik->reset();
    }
    m_kills = 0;
    ++m_round;
    startWave(m_startWave);
}

void HordeModule::startWave(int n) {
    m_wave = n;
    const Wave w = m_roster.waveAt(n);
    const float extra = 1.0f + m_roster.perPlayer * static_cast<float>(std::max(0, static_cast<int>(m_heroes.size()) - 1));
    m_toSpawn = static_cast<int>(std::round(static_cast<float>(w.goblins) * extra));
    m_waveSize = m_toSpawn;
    m_bossToSpawn = m_roster.bossOf(n);
    if (m_forceBoss && n == m_startWave && m_bossToSpawn < 0 && !m_roster.bosses.empty()) m_bossToSpawn = 0;
    m_waveKills = 0;
    m_spawnTimer = 1.5f;
    if (m_lineup && m_phase != Phase::Lobby) {
        // Every goblin type and boss in a row, facing the player, standing
        // still and showing its moves in turn (checking the art).
        m_toSpawn = 0;
        m_bossToSpawn = -1;
        for (size_t i = 0; i < m_roster.types.size(); ++i)
            spawnFoe(static_cast<int>(i), false, glm::vec3((static_cast<float>(i) - 0.5f * static_cast<float>(m_roster.types.size() - 1)) * 1.9f, 0.0f, 6.0f));
        for (size_t i = 0; i < m_roster.bosses.size(); ++i)
            spawnFoe(static_cast<int>(i), true, glm::vec3((static_cast<float>(i) - 0.5f * static_cast<float>(m_roster.bosses.size() - 1)) * 4.5f, 0.0f, 12.0f));
    }
    m_morale = 1.0f;
    if (m_phase != Phase::Lobby) {
        m_phase = Phase::Intro;
        m_phaseTime = 0.0f;
    }
    // Whoever went down is back for the new wave.
    for (auto& h : m_heroes) {
        if (h->remote || !h->id) continue;
        kke::Combatant& c = m_combat.get(h->id);
        if (!c.alive()) {
            c.reset();
            c.heal(c.stats().maxHealth);
            h->downTime = 0.0f;
            h->look.lastState = -1;
        }
    }
}

int HordeModule::alivePlayers() const {
    int n = 0;
    for (const auto& h : m_heroes) {
        if (h->remote) n += h->netState != 7 ? 1 : 0;
        else if (h->id && m_combat.get(h->id).alive()) ++n;
    }
    return n;
}

int HordeModule::aliveFoes() const {
    int n = 0;
    for (const auto& f : m_foes) n += f->dead ? 0 : 1;
    return n;
}

void HordeModule::updateWaves(float dt) {
    m_phaseTime += dt;
    const int alive = aliveFoes();
    switch (m_phase) {
    case Phase::Lobby:
        break;
    case Phase::Intro:
        if (m_phaseTime > 2.5f) {
            m_phase = Phase::Fighting;
            m_phaseTime = 0.0f;
        }
        [[fallthrough]];
    case Phase::Fighting: {
        if (netClient()) break; // the host's horde
        // Feed the wave in through the gates.
        const Wave w = m_roster.waveAt(m_wave);
        m_spawnTimer -= dt;
        const int cap = std::min(w.atOnce + static_cast<int>(m_heroes.size() - 1) * 6, m_maxAlive);
        int now = alive;
        if (m_bossToSpawn >= 0 && m_phaseTime > 1.0f) {
            const glm::vec3& gate = m_gates[std::uniform_int_distribution<size_t>(0, m_gates.size() - 1)(m_rng)];
            spawnFoe(m_bossToSpawn, true, gate * 22.0f);
            m_bossToSpawn = -1;
            ++now;
        }
        while (m_toSpawn > 0 && now < cap && m_spawnTimer <= 0.0f) {
            const glm::vec3& gate = m_gates[std::uniform_int_distribution<size_t>(0, m_gates.size() - 1)(m_rng)];
            const glm::vec3 side(gate.z, 0.0f, -gate.x);
            const float spread = std::uniform_real_distribution<float>(-5.0f, 5.0f)(m_rng);
            const float out = std::uniform_real_distribution<float>(20.0f, 26.0f)(m_rng);
            spawnFoe(pickType(), false, gate * out + side * spread);
            --m_toSpawn;
            ++now;
            m_spawnTimer = 0.12f;
        }
        if (m_toSpawn <= 0 && m_bossToSpawn < 0 && alive == 0 && m_phase == Phase::Fighting) {
            m_phase = Phase::Cleared;
            m_phaseTime = 0.0f;
            for (auto& h : m_heroes) // a breather
                if (!h->remote && h->id && m_combat.get(h->id).alive()) m_combat.get(h->id).heal(m_combat.get(h->id).stats().maxHealth * 0.4f);
        }
        if (!m_heroes.empty() && alivePlayers() == 0) {
            m_phase = Phase::Overrun;
            m_phaseTime = 0.0f;
        }
        break;
    }
    case Phase::Cleared:
        if (!netClient() && m_phaseTime > m_roster.breather) startWave(m_wave + 1);
        break;
    case Phase::Overrun: {
        bool again = m_bot && m_phaseTime > 5.0f;
        for (auto& h : m_heroes)
            if (!h->remote && m_phaseTime > 1.5f && m_input->map(h->player).pressed("horde.again")) again = true;
        if (again && !netClient()) restart();
        break;
    }
    }
    // Morale: kills knock it down, it comes back; the last few of a wave lose heart.
    m_morale = std::min(1.0f, m_morale + dt * 0.04f);
    bool bossUp = false;
    for (const auto& f : m_foes) bossUp = bossUp || (!f->dead && f->type && f->type->boss);
    if (!bossUp && m_toSpawn <= 0 && alive > 0 && alive <= std::max(2, m_waveSize / 8)) m_morale = std::min(m_morale, 0.1f);
}

void HordeModule::onEvent(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && m_phase != Phase::Lobby && !ImGui::GetIO().WantCaptureMouse && !m_app->uiCapturesMouse() &&
        e.button.button == SDL_BUTTON_LEFT) {
        m_captured = true;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), true);
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE && m_captured) {
        m_captured = false;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    }
}

void HordeModule::update(const kke::UpdateContext& ctx) {
    const auto frameStart = std::chrono::steady_clock::now();
    const float dt = std::min(ctx.dt, 0.05f);
    if (m_input->map(0).pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    if (m_captured && (m_phase == Phase::Lobby || m_app->uiCapturesMouse())) {
        m_captured = false;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    }
    updateNet(dt);
    if (m_phase == Phase::Lobby && (!m_lobby || !m_lobby->isOpen()) && !netClient()) startFromLobby();
    if (m_phase == Phase::Lobby && m_lobby && m_lobby->lobby().takeStart()) startFromLobby();

    updateWaves(dt);
    for (auto& h : m_heroes) updateHero(*h, dt);
    const auto aiStart = std::chrono::steady_clock::now();
    updateFoes(dt);
    const auto aiEnd = std::chrono::steady_clock::now();
    for (const kke::HitEvent& e : m_combat.step(dt)) onHit(e);
    updateShots(dt);
    updateMarks(dt);
    for (auto& h : m_heroes) animateHero(*h, dt);
    for (auto& f : m_foes) animateFoe(*f, dt);
    const auto animEnd = std::chrono::steady_clock::now();
    if (m_fx) m_fx->update(dt);
    updateCameras(dt);
    updateInventory();
    updateHud();
    sendNet(dt);

    m_frameMs += static_cast<double>(ctx.dt) * 1000.0;
    m_worstMs = std::max(m_worstMs, static_cast<double>(ctx.dt) * 1000.0);
    m_aiMs += std::chrono::duration<double, std::milli>(aiEnd - aiStart).count();
    m_animMs += std::chrono::duration<double, std::milli>(animEnd - aiEnd).count();
    ++m_frames;
    (void)frameStart;
    report(ctx.dt);
}

void HordeModule::report(float dt) {
    m_clock += dt;
    if (m_quitAfter <= 0.0f) return;
    if (m_clock >= m_reportAt) {
        m_reportAt += 10.0f;
        const double n = std::max(1, m_frames);
        std::map<std::string, int> byType;
        bool boss = false;
        for (const auto& f : m_foes)
            if (!f->dead && f->type) {
                ++byType[f->type->id];
                boss = boss || f->type->boss;
            }
        std::string mix;
        for (const auto& [id, count] : byType) mix += (mix.empty() ? "" : ", ") + std::to_string(count) + " " + id;
        float health = 0.0f;
        for (const auto& h : m_heroes)
            if (!h->remote && h->id) health += m_combat.get(h->id).health();
        kke::log::get(name())->info("t {:.0f} s: wave {}, {} alive ({}){}, {} kills, players' health {:.0f}, {} shots, {} hits by players; frame {:.1f} ms avg "
                                    "({:.1f} worst), horde minds+moves {:.2f} ms, animation {:.2f} ms",
                                    m_clock, m_wave, aliveFoes(), mix, boss ? ", a boss" : "", m_kills, health, m_shotsFired, m_heroHits, m_frameMs / n, m_worstMs,
                                    m_aiMs / n, m_animMs / n);
        m_frameMs = m_worstMs = m_aiMs = m_animMs = 0.0;
        m_frames = 0;
    }
    if (m_clock >= m_quitAfter) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        m_quitAfter = -1.0f;
    }
}

void HordeModule::updateCameras(float dt) {
    std::vector<Hero*> views;
    for (auto& h : m_heroes)
        if (!h->remote) views.push_back(h.get());
    std::sort(views.begin(), views.end(), [](const Hero* a, const Hero* b) { return a->slot < b->slot; });
    kke::RigidWorld& world = m_rigid->world();
    auto probe = [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = world.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    };
    for (Hero* h : views) {
        kke::InputMap& in = m_input->map(h->player);
        const bool inMenu = m_inventoryHero >= 0 && m_heroes[static_cast<size_t>(m_inventoryHero)].get() == h;
        if (!inMenu && m_phase != Phase::Lobby) {
            if (h->slot == 0 && m_captured) {
                const glm::vec2 look = in.axis2("look");
                const float s = h->aiming ? m_mouseSensitivity * 0.55f : m_mouseSensitivity;
                h->rig.addLook(look.x * s, look.y * s);
            }
            const glm::vec2 rate = in.axis2("look.rate");
            const float k = h->aiming ? 0.5f : 1.0f;
            h->rig.addLook(rate.x * m_stickSpeed * k * dt, rate.y * m_stickSpeed * 0.7f * k * dt);
        }
        if (h->bot) {
            // Follow behind, slowly.
            const float want = glm::degrees(std::atan2(-h->facing.x, -h->facing.z)) + 180.0f;
            const float d = std::fmod(want - h->rig.yaw + 540.0f, 360.0f) - 180.0f;
            h->rig.yaw += d * (1.0f - std::exp(-0.8f * dt));
            h->rig.pitch = -24.0f;
        }
        // Aiming: over the right shoulder, closer, narrower.
        const float k = 1.0f - std::exp(-10.0f * dt);
        const bool close = h->aiming;
        h->rig.mode = kke::CameraRig::Mode::ThirdPerson;
        h->rig.settings.armLength += ((close ? 2.2f : 4.6f) - h->rig.settings.armLength) * k;
        h->rig.settings.shoulderOffset += ((close ? 0.65f : 0.0f) - h->rig.settings.shoulderOffset) * k;
        h->rig.settings.fovDegrees += ((close ? 48.0f : 60.0f) - h->rig.settings.fovDegrees) * k;
        h->rig.settings.pivotHeight = 1.7f;
        kke::Camera cam = m_app->camera();
        // Follows the player where it's drawn (between physics steps).
        h->rig.update(dt, heroFeet(*h), probe, cam);
        cam.farPlane = 200.0f;
        h->camera = cam;
    }
    std::vector<kke::Application::View>& appViews = m_app->views();
    appViews.clear();
    if (m_phase == Phase::Lobby) {
        // Behind the menu: round the fort, slowly.
        kke::Camera& cam = m_app->camera();
        const float a = m_clock * 0.08f;
        cam.position = glm::vec3(std::sin(a) * 19.0f, 7.5f, std::cos(a) * 19.0f);
        cam.target = glm::vec3(0.0f, 1.0f, 0.0f);
        cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
        return;
    }
    if (views.size() == 1) {
        m_app->camera() = views[0]->camera;
    } else if (views.size() > 1) {
        const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(views.size()), true);
        for (size_t i = 0; i < views.size(); ++i) appViews.push_back({ views[i]->camera, rects[i] });
        m_app->camera() = views[0]->camera; // the ears
        if (views.size() == 3) {
            // The empty quarter: the fort from above.
            kke::Camera over = views[0]->camera;
            over.position = glm::vec3(0.0f, 34.0f, 22.0f);
            over.target = glm::vec3(0.0f, 0.0f, 0.0f);
            over.up = glm::vec3(0.0f, 1.0f, 0.0f);
            over.fovDegrees = 60.0f;
            appViews.push_back({ over, kke::splitScreen(4, true)[3] });
        }
    }
}

void HordeModule::render(const kke::RenderContext& ctx) {
    m_arena->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    // Anyone without art at all: a block in their colour.
    for (const auto& h : m_heroes)
        if (!h->look.model) {
            const float yaw = glm::degrees(std::atan2(h->facing.x, h->facing.z));
            m_block->draw(ctx, glm::rotate(glm::translate(glm::mat4(1.0f), heroFeet(*h)), glm::radians(yaw), glm::vec3(0, 1, 0)), 0.3f, 0.5f);
        }
    for (const auto& f : m_foes)
        if (!f->look.model && !f->dead)
            m_block->draw(ctx, glm::scale(glm::rotate(glm::translate(glm::mat4(1.0f), f->position), glm::radians(f->yaw), glm::vec3(0, 1, 0)), glm::vec3(f->scale)),
                          0.0f, 0.8f);
    for (const auto& h : m_heroes)
        if (h->look.right && weaponOf(*h).kind == Weapon::Kind::Crossbow) m_stock->draw(ctx, h->look.rightAt, 0.0f, 0.7f);
    if (m_marks && m_markVerts) m_marks->draw(ctx, glm::mat4(1.0f), 0.0f, 1.0f);
    // Fireballs, zaps, boulders, the glow of a charged spin.
    std::vector<kke::SphereImpostorRenderer::Sphere> spheres;
    for (const Shot& s : m_shots) {
        if (s.model) continue;
        if (s.kind == "fireball") spheres.push_back({ s.position, 0.28f, { 1.0f, 0.45f, 0.1f }, 0.45f, 0.6f });
        else if (s.kind == "zap") spheres.push_back({ s.position, 0.14f, { 0.55f, 0.75f, 1.0f }, 0.6f, 0.3f });
        else if (s.kind == "boulder") spheres.push_back({ s.position, 0.7f, { 0.45f, 0.42f, 0.38f }, 0.0f, 0.95f });
        else if (s.kind == "heal") spheres.push_back({ s.position, 0.16f, { 0.4f, 1.0f, 0.45f }, 0.5f, 0.3f });
        else spheres.push_back({ s.position, 0.06f, { 0.5f, 0.35f, 0.2f }, 0.0f, 0.8f });
    }
    for (const auto& h : m_heroes)
        if (h->charged && h->look.model) spheres.push_back({ h->look.handPos, 0.12f, { 1.0f, 0.85f, 0.4f }, 0.6f, 0.2f });
    if (!spheres.empty()) m_spheres->draw(ctx, spheres);
}

void HordeModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_fx) m_fx->draw(ctx);
}

void HordeModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_arena->drawShadow(ctx, glm::mat4(1.0f));
    for (const auto& h : m_heroes)
        if (!h->look.model) m_block->drawShadow(ctx, glm::translate(glm::mat4(1.0f), heroFeet(*h)));
}

void HordeModule::shutdown() {
    for (auto& f : m_foes)
        if (f->ragdoll && m_ragdolls) m_ragdolls->destroyRagdoll(f->ragdoll);
    m_foes.clear();
    m_heroes.clear();
    m_fx.reset();
    m_ai.setNavMesh(nullptr);
}

} // namespace horde
