// The show: rounds of minigames, the level each one builds (Arena), and
// the frame: controls, beans, the minigame's rules, cameras, the HUD.

#include "PartyModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"
#include "kke/modules/VoiceModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace party {

namespace {

// How long each screen between rounds stays up (s).
constexpr float kIntroTime = 5.0f, kCountdownTime = 3.0f, kRoundOverTime = 2.5f, kResultsTime = 7.0f;

float envFloat(const char* name, float fallback) {
    const char* v = kke::dev::env(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}

} // namespace

PartyModule::PartyModule() = default;
PartyModule::~PartyModule() = default;

std::vector<kke::ModuleDependency> PartyModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the levels, the beans' bodies, glass and tiles that fall (Jolt)" },
             { std::type_index(typeid(kke::InputModule)), true, "run, jump and dive, rebindable, one set per player" },
             { std::type_index(typeid(kke::UiModule)), false, "the HUD, the round cards, the results and the podium" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join, dress their bean, set the CPU beans" },
             { std::type_index(typeid(kke::NetModule)), false, "online parties: Host / Join in the start menu" },
             { std::type_index(typeid(kke::VoiceModule)), false, "voice chat with everyone in the party" },
             { std::type_index(typeid(kke::AudioModule)), false, "bumps, glass, fire and the show's tones, synthesised" } };
}

kke::RigidWorld& PartyModule::world() { return m_rigid->world(); }

bool PartyModule::authority() const { return !netClient(); }

void PartyModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    m_net = app.getModule<kke::NetModule>();
    m_audio = app.getModule<kke::AudioModule>();
    m_voice = app.getModule<kke::VoiceModule>();

    m_autopilot = kke::dev::flag("KKE_PARTY_AUTOPILOT");
    m_defaultCpus = std::clamp(static_cast<int>(envFloat("KKE_PARTY_CPUS", 5.0f)), 0, kke::Lobby::kMaxCpus);
    m_quitAfter = envFloat("KKE_PARTY_QUIT", -1.0f);
    m_startAfter = envFloat("KKE_PARTY_START", -1.0f);
    m_forcedRounds = static_cast<int>(envFloat("KKE_PARTY_ROUNDS", 0.0f));
    m_seed = static_cast<uint32_t>(envFloat("KKE_PARTY_SEED", static_cast<float>(SDL_GetTicks() % 100000 + 1)));
    if (const char* g = kke::dev::env("KKE_PARTY_GAME")) m_onlyGame = g;

    m_games = makeMinigames();
    if (!m_onlyGame.empty() &&
        std::none_of(m_games.begin(), m_games.end(), [this](const std::unique_ptr<Minigame>& g) { return m_onlyGame == g->id(); })) {
        kke::log::get(name())->warn("KKE_PARTY_GAME: no minigame called '{}' (playing them all)", m_onlyGame);
        m_onlyGame.clear();
    }
    defineControls();
    m_level = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_stageMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_particleMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    app.camera().farPlane = 300.0f;
    app.window().setQuitOnEscape(false); // Esc frees the mouse
    buildStage();
    setupLobby();
    setupNet();
    buildBeans(wantedRoster());
    buildHud();
    if (!m_lobby || !m_lobby->isOpen()) startParty();
    else m_app->setMood(m_mood = "sunset");
}

void PartyModule::shutdown() {
    for (Bean& b : m_beans) removeBean(b);
    m_beans.clear();
    clearLevel();
}

// ---- The level -------------------------------------------------------

kke::RigidWorld::BodyId PartyModule::staticBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& color, const glm::quat& rot,
                                               float friction) {
    levelMesh().box(center, half, color, glm::mat3_cast(rot));
    kke::RigidWorld::BodyDesc d;
    d.motion = kke::RigidWorld::Motion::Static;
    d.halfExtents = half;
    d.position = center;
    d.rotation = rot;
    d.friction = friction;
    const kke::RigidWorld::BodyId id = world().add(d);
    m_statics.push_back(id);
    return id;
}

int PartyModule::addPart(const kke::RigidWorld::BodyDesc& desc, std::vector<kke::Vertex> v, std::vector<uint32_t> idx) {
    Part p;
    p.body = world().add(desc);
    p.mesh = std::make_shared<kke::DynamicMeshRenderer>(*m_app);
    p.mesh->upload(v, idx);
    m_parts.push_back(std::move(p));
    return static_cast<int>(m_parts.size()) - 1;
}

int PartyModule::addVisual(std::vector<kke::Vertex> v, std::vector<uint32_t> idx) {
    Part p;
    p.mesh = std::make_shared<kke::DynamicMeshRenderer>(*m_app);
    p.mesh->upload(v, idx);
    m_parts.push_back(std::move(p));
    return static_cast<int>(m_parts.size()) - 1;
}

void PartyModule::removePart(int index) {
    if (index < 0 || index >= static_cast<int>(m_parts.size())) return;
    Part& p = m_parts[static_cast<size_t>(index)];
    if (!p.alive) return;
    if (p.body != kke::RigidWorld::kNoBody) world().remove(p.body);
    p.body = kke::RigidWorld::kNoBody;
    p.alive = false;
    p.visible = false;
    // Frames still in flight draw its mesh: it goes once they're done.
    if (p.mesh) m_app->renderer().retire(std::move(p.mesh));
}

void PartyModule::movePart(int index, const glm::vec3& pos, const glm::quat& rot, float dt) {
    Part& p = part(index);
    if (!p.alive) return;
    if (p.body == kke::RigidWorld::kNoBody) {
        p.transform = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot);
        return;
    }
    world().moveKinematic(p.body, pos, rot, std::max(dt, 1e-4f));
}

void PartyModule::clearLevel() {
    if (!m_rigid) return;
    for (kke::RigidWorld::BodyId b : m_statics) world().remove(b);
    m_statics.clear();
    for (int i = 0; i < static_cast<int>(m_parts.size()); ++i) removePart(i);
    m_parts.clear();
    m_levelV.clear();
    m_levelI.clear();
    m_level->upload(m_levelV, m_levelI);
    m_particles.clear();
}

// The lobby's stage (and the podium's): a round floor in front of a bright
// backdrop, far from where the minigames build.
void PartyModule::buildStage() {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    MeshBuilder mb{ v, idx };
    const glm::vec3 at(0.0f, 0.0f, 400.0f);
    mb.cylinder(at + glm::vec3(0.0f, -0.5f, 0.0f), 16.0f, 16.0f, 0.5f, glm::vec3(0.95f, 0.8f, 0.9f), 48);
    mb.cylinder(at + glm::vec3(0.0f, -0.52f, 0.0f), 17.0f, 17.0f, 0.5f, glm::vec3(0.55f, 0.35f, 0.85f), 48);
    for (int i = 0; i < 9; ++i) {
        const float x = -16.0f + 4.0f * static_cast<float>(i);
        const glm::vec3 c = beanColour(i);
        mb.box(at + glm::vec3(x, 4.0f, -10.0f), { 1.9f, 4.0f, 0.3f }, c);
        mb.ellipsoid(at + glm::vec3(x, 8.6f, -10.0f), glm::vec3(1.3f), glm::mix(c, glm::vec3(1.0f), 0.4f), 16, 10);
    }
    // The podium: three steps, gold in the middle.
    mb.box(at + glm::vec3(0.0f, 0.9f, -4.0f), { 1.2f, 0.9f, 1.0f }, glm::vec3(1.0f, 0.82f, 0.25f));
    mb.box(at + glm::vec3(-2.5f, 0.6f, -4.0f), { 1.2f, 0.6f, 1.0f }, glm::vec3(0.8f, 0.82f, 0.88f));
    mb.box(at + glm::vec3(2.5f, 0.35f, -4.0f), { 1.2f, 0.35f, 1.0f }, glm::vec3(0.85f, 0.55f, 0.3f));
    m_stageMesh->upload(v, idx);
    kke::RigidWorld& w = world();
    auto body = [&](const glm::vec3& c, const glm::vec3& h) {
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = h;
        d.position = c;
        m_stage.push_back(w.add(d));
    };
    body(at + glm::vec3(0.0f, -0.5f, 0.0f), { 17.0f, 0.5f, 17.0f });
    body(at + glm::vec3(0.0f, 0.9f, -4.0f), { 1.2f, 0.9f, 1.0f });
    body(at + glm::vec3(-2.5f, 0.6f, -4.0f), { 1.2f, 0.6f, 1.0f });
    body(at + glm::vec3(2.5f, 0.35f, -4.0f), { 1.2f, 0.35f, 1.0f });
}

// ---- The show --------------------------------------------------------

std::vector<std::string> PartyModule::chosenGames() const {
    std::vector<std::string> out;
    if (!m_onlyGame.empty()) return { m_onlyGame };
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("games") : nullptr;
    const int pick = o ? o->value : 0;
    // 0 = every minigame, shuffled; then one each (practice).
    if (pick > 0 && pick <= static_cast<int>(m_games.size())) return { m_games[static_cast<size_t>(pick - 1)]->id() };
    for (const auto& g : m_games) out.push_back(g->id());
    return out;
}

int PartyModule::chosenRounds() const {
    if (m_forcedRounds > 0) return m_forcedRounds;
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("rounds") : nullptr;
    static constexpr int kRounds[] = { 3, 5, 8 };
    return o ? kRounds[std::clamp(o->value, 0, 2)] : 5;
}

void PartyModule::startParty() {
    if (netClient()) {
        if (m_lobby) m_lobby->lobby().toast("The host starts the party", 3.0f);
        return;
    }
    if (m_lobby && m_lobby->isOpen()) m_lobby->save();
    if (m_lobby) m_lobby->close();
    if (netHost()) syncNetPlayers();
    buildBeans(netHost() ? onlineRoster() : wantedRoster());
    if (m_lobby) {
        m_lobby->applyInput();
        for (Bean& b : m_beans)
            if (b.seat >= 0) b.player = std::max(0, m_lobby->playerOf(b.seat));
    } else {
        m_input->setPlayers(1);
    }
    m_rosterChanged = false;
    const std::vector<std::string> games = chosenGames();
    ++m_seed;
    m_show.start(games, chosenRounds(), static_cast<int>(m_beans.size()), m_seed, games.size() > 1);
    kke::log::get(name())->info("party: {} beans ({} playing here), {} rounds: {}", m_beans.size(),
                                std::count_if(m_beans.begin(), m_beans.end(), [](const Bean& b) { return b.seat >= 0; }), m_show.rounds,
                                fmt::join(m_show.playlist, ", "));
    loadRound();
}

void PartyModule::loadRound() {
    buildRound(m_show.current(), m_seed * 7919u + static_cast<uint32_t>(m_show.round) * 104729u + 17u);
    if (netHost()) sendRound();
}

// The level of `game` from `seed` (the same on every machine), everyone
// at the start, the round card up.
void PartyModule::buildRound(const std::string& game, uint32_t seed) {
    clearLevel();
    m_game = nullptr;
    for (const auto& g : m_games)
        if (g->id() == game) m_game = g.get();
    if (!m_game) {
        kke::log::get(name())->warn("no minigame called '{}': playing {}", game, m_games.front()->id());
        m_game = m_games.front().get();
    }
    m_roundSeed = seed;
    ++m_netRound;
    m_rng = Rng(m_roundSeed);
    m_botRng = Rng(m_roundSeed ^ 0x5bd1e995u ^ static_cast<uint32_t>(SDL_GetTicks()));
    if (m_mood != m_game->mood()) {
        m_mood = m_game->mood();
        m_app->setMood(m_mood);
    }
    m_game->build(*this);
    m_level->upload(m_levelV, m_levelI);
    m_finishOrder = m_outOrder = 0;
    m_playTime = 0.0f;
    m_flashTime = 0.0f;
    const int count = static_cast<int>(m_beans.size());
    for (int i = 0; i < count; ++i) {
        Bean& b = m_beans[static_cast<size_t>(i)];
        b.result = RoundResult{};
        b.active = true;
        b.finished = b.out = b.hidden = false;
        b.stun = b.dive = b.frozen = 0.0f;
        b.push = b.velocity = glm::vec3(0.0f);
        b.a = b.b = b.c = 0.0f;
        b.i = b.j = 0;
        b.v = glm::vec3(0.0f);
        b.botTimer = 0.0f;
        b.watching = -1;
        glm::vec3 feet(0.0f);
        float yaw = 0.0f;
        m_game->spawn(*this, i, count, feet, yaw);
        b.checkpoint = feet;
        b.checkpointYaw = yaw;
        if (!b.remote) place(b, feet, yaw);
        b.rig.yaw = m_game->followYaw();
        b.rig.pitch = -16.0f;
    }
    m_phase = Phase::Intro;
    m_phaseTime = 0.0f;
    kke::log::get(name())->info("round {} of {}: {} (seed {})", m_show.round + 1, m_show.rounds, m_game->title(), m_roundSeed);
}

void PartyModule::startPlay() {
    m_phase = Phase::Play;
    m_phaseTime = 0.0f;
    m_playTime = 0.0f;
    m_game->start(*this);
    tone(static_cast<int>(kke::Earcon::Activate), 0.8f);
}

std::vector<RoundResult> PartyModule::roundResults() const {
    std::vector<RoundResult> r;
    for (const Bean& b : m_beans) r.push_back(b.result);
    return r;
}

void PartyModule::endRound() {
    m_phase = Phase::RoundOver;
    m_phaseTime = 0.0f;
    if (m_game) m_game->timeUp(*this);
    m_roundPoints = m_show.score(roundResults());
    const std::vector<int> places = placesOf(roundResults());
    for (size_t i = 0; i < m_beans.size(); ++i)
        kke::log::get(name())->info("  {}. {}: +{} ({} in all){}{}", places[i] + 1, m_beans[i].name, m_roundPoints[i], m_show.points[i],
                                    m_beans[i].result.finished ? ", finished" : "", m_beans[i].result.out ? ", out" : "");
    tone(static_cast<int>(kke::Earcon::ToggleOn), 0.8f);
}

void PartyModule::nextRound() {
    m_show.next();
    if (m_show.over()) showPodium();
    else loadRound();
}

void PartyModule::showPodium() {
    clearLevel();
    m_game = nullptr;
    m_phase = Phase::Podium;
    m_phaseTime = 0.0f;
    if (m_mood != "sunset") m_app->setMood(m_mood = "sunset");
    const std::vector<int> order = m_show.standings();
    const std::vector<int> places = m_show.standingPlaces();
    const glm::vec3 at(0.0f, 0.0f, 400.0f);
    // The top three on the steps (gold in the middle), everyone else in front.
    const glm::vec3 steps[3] = { at + glm::vec3(0.0f, 1.8f, -4.0f), at + glm::vec3(-2.5f, 1.2f, -4.0f), at + glm::vec3(2.5f, 0.7f, -4.0f) };
    int front = 0;
    const int rest = std::max(1, static_cast<int>(order.size()) - 3);
    for (size_t k = 0; k < order.size(); ++k) {
        Bean& b = m_beans[static_cast<size_t>(order[k])];
        b.active = true;
        b.hidden = b.out = b.finished = false;
        b.stun = b.dive = 0.0f;
        b.push = b.velocity = glm::vec3(0.0f);
        const int p = places[static_cast<size_t>(order[k])];
        glm::vec3 feet = p < 3 && k < 3 ? steps[k] : at + glm::vec3((static_cast<float>(front) - static_cast<float>(rest - 1) * 0.5f) * 1.6f, 0.0f, 0.5f);
        if (!(p < 3 && k < 3)) ++front;
        if (!b.remote) place(b, feet, 0.0f);
    }
    for (int k = 0; k < 3 && k < static_cast<int>(order.size()); ++k) burst(steps[k] + glm::vec3(0.0f, 1.5f, 0.0f), glm::vec3(1.0f, 0.85f, 0.3f), 60, 6.0f);
    const Bean& winner = m_beans[static_cast<size_t>(order.front())];
    kke::log::get(name())->info("party over: {} wins with {} points", winner.name, m_show.points[static_cast<size_t>(order.front())]);
    tone(static_cast<int>(kke::Earcon::Activate), 0.9f);
}

void PartyModule::backToLobby() {
    clearLevel();
    m_game = nullptr;
    m_phase = Phase::Lobby;
    m_phaseTime = 0.0f;
    m_captured = false;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    m_app->views().clear();
    if (m_mood != "sunset") m_app->setMood(m_mood = "sunset");
    for (Bean& b : m_beans) {
        b.active = true;
        b.hidden = b.out = b.finished = false;
    }
    if (m_lobby) m_lobby->open();
}

// ---- The rules (Arena) ----------------------------------------------

void PartyModule::finish(Bean& b) {
    if (b.finished || b.out || !b.active) return;
    b.finished = true;
    b.active = false;
    b.result.finished = true;
    b.result.finishOrder = m_finishOrder++;
    const kke::RigidWorld& w = world();
    burst(b.feet(w) + glm::vec3(0.0f, 1.0f, 0.0f), beanColour(b.look.colour), 40, 5.0f);
    if (b.seat >= 0) tone(static_cast<int>(kke::Earcon::ToggleOn), 0.8f);
    kke::log::get(name())->info("{} finished {} at {:.1f} s", b.name, b.result.finishOrder + 1, m_playTime);
    if (online() && !b.remote) sendResult(netparty::kResultFinish, b);
}

void PartyModule::eliminate(Bean& b, const std::string& why) {
    if (b.finished || b.out || !b.active) return;
    // Online, only the machine that plays a bean decides it's out (it
    // sees exactly where it is); the host puts the outs in order.
    if (b.remote) return;
    b.out = true;
    b.active = false;
    b.result.out = true;
    b.result.outOrder = m_outOrder++;
    const glm::vec3 at = b.feet(world());
    burst(at + glm::vec3(0.0f, 0.8f, 0.0f), beanColour(b.look.colour), 30, 4.0f);
    sound(at, kke::AudioMaterialTable::Rubber, 0.7f);
    if (b.seat >= 0) tone(static_cast<int>(kke::Earcon::Error), 0.6f);
    flash(b.name + " is out!", 1.4f);
    kke::log::get(name())->info("{} is out ({}) at {:.1f} s", b.name, why, m_playTime);
    if (online()) sendResult(netparty::kResultOut, b);
}

void PartyModule::knock(Bean& b, const glm::vec3& velocity, float stun) {
    if (b.remote || !b.active) return;
    b.push += glm::vec3(velocity.x, 0.0f, velocity.z);
    b.launch = std::max(b.launch, velocity.y); // applied by moveBean
    if (stun > 0.0f) {
        b.stun = std::max(b.stun, stun);
        b.dive = 0.0f;
    }
}

void PartyModule::place(Bean& b, const glm::vec3& feet, float yaw) {
    kke::RigidWorld& w = world();
    w.setCharacterKinematic(b.id, false);
    w.teleportCharacter(b.id, feet);
    b.yaw = yaw;
    b.velocity = b.push = glm::vec3(0.0f);
    b.stun = b.dive = 0.0f;
    b.hidden = false;
}

void PartyModule::respawn(Bean& b) {
    place(b, b.checkpoint, b.checkpointYaw);
    b.frozen = 0.4f;
    burst(b.checkpoint + glm::vec3(0.0f, 0.7f, 0.0f), glm::vec3(1.0f), 16, 2.5f);
}

int PartyModule::finishedCount() const {
    return static_cast<int>(std::count_if(m_beans.begin(), m_beans.end(), [](const Bean& b) { return b.finished; }));
}
int PartyModule::outCount() const {
    return static_cast<int>(std::count_if(m_beans.begin(), m_beans.end(), [](const Bean& b) { return b.out; }));
}
int PartyModule::activeCount() const {
    return static_cast<int>(std::count_if(m_beans.begin(), m_beans.end(), [](const Bean& b) { return b.active; }));
}

void PartyModule::sound(const glm::vec3& at, uint32_t material, float intensity) {
    if (m_audio) m_audio->playImpact(at, material, std::clamp(intensity, 0.0f, 1.0f));
}

void PartyModule::tone(int earcon, float gain) {
    if (m_audio) m_audio->playEarcon(static_cast<kke::Earcon>(earcon), gain);
}

void PartyModule::flash(const std::string& text, float seconds) {
    m_flash = text;
    m_flashTime = seconds;
}

// ---- The frame ------------------------------------------------------

void PartyModule::onEvent(const SDL_Event& e) {
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

void PartyModule::checkFalls() {
    if (!m_game) return;
    const kke::RigidWorld& w = world();
    for (Bean& b : m_beans) {
        if (b.remote || !b.active || b.hidden) continue;
        if (b.feet(w).y < m_game->killY()) {
            if (m_phase == Phase::Play) m_game->fell(*this, b);
            else respawn(b); // before GO: back to the start
            if (b.out) park(b);
        }
    }
}

void PartyModule::update(const kke::UpdateContext& ctx) {
    const float dt = std::min(ctx.dt, 0.05f);
    m_clock += ctx.dt;
    kke::InputMap& p1 = m_input->map(0);
    if (p1.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    updateNet(dt);
    m_status.clear();
    m_phaseTime += dt;
    m_flashTime = std::max(0.0f, m_flashTime - dt);

    if (m_phase == Phase::Lobby) {
        updateLobby(dt);
        updateParticles(dt);
        updateHud(dt);
        return;
    }

    // Any player: skip a screen (jump), the menu (back).
    bool skip = false, menu = false;
    for (int p = 0; p < m_input->players(); ++p) {
        kke::InputMap& in = m_input->map(p);
        skip = skip || in.pressed("jump");
        menu = menu || in.pressed("menu");
    }
    if (menu && m_lobby && !netClient()) {
        backToLobby();
        updateHud(dt);
        return;
    }
    const bool host = !netClient(); // online, the host moves the show on
    switch (m_phase) {
    case Phase::Intro:
        if (host && (m_phaseTime > kIntroTime || (skip && m_phaseTime > 0.8f))) {
            m_phase = Phase::Countdown;
            m_phaseTime = 0.0f;
            if (netHost()) sendRound(); // with the phase
        }
        break;
    case Phase::Countdown: {
        const int before = static_cast<int>(std::ceil(kCountdownTime - (m_phaseTime - dt)));
        const int now = static_cast<int>(std::ceil(kCountdownTime - m_phaseTime));
        if (now != before && now > 0) tone(static_cast<int>(kke::Earcon::Tick), 0.6f);
        if (host && m_phaseTime >= kCountdownTime) {
            startPlay();
            if (netHost()) sendRound();
        }
        break;
    }
    case Phase::Play:
        m_playTime += dt;
        if (host && m_game && (m_game->over(*this) || m_playTime >= m_game->timeLimit())) {
            if (m_playTime >= m_game->timeLimit()) flash("Time's up!", 2.0f);
            endRound();
            if (netHost()) sendRound();
        }
        break;
    case Phase::RoundOver:
        if (host && m_phaseTime > kRoundOverTime) {
            m_phase = Phase::Results;
            m_phaseTime = 0.0f;
            if (netHost()) sendRound();
        }
        break;
    case Phase::Results:
        if (host && (m_phaseTime > kResultsTime || (skip && m_phaseTime > 1.0f))) {
            if (m_rosterChanged && !m_show.lastRound()) m_rosterChanged = false; // joiners wait for the next party
            nextRound();
        }
        break;
    case Phase::Podium:
        if (host && skip && m_phaseTime > 2.0f) {
            if (m_lobby) backToLobby();
            else startParty();
        }
        break;
    case Phase::Lobby: break;
    }

    // The beans: this screen's players read their controls, the CPU beans
    // think, then everyone moves (the minigame's rules may stop them).
    for (Bean& b : m_beans) {
        if (b.remote) continue;
        b.input = BeanInput{};
        const bool canMove = b.active && (m_phase == Phase::Play || m_phase == Phase::Intro || m_phase == Phase::Podium) && b.frozen <= 0.0f;
        if (b.bot) {
            if (canMove && m_phase == Phase::Play && m_game) b.input = m_game->bot(*this, b, dt);
        } else {
            BeanInput in = readPlayer(b, dt);
            if (canMove) b.input = in;
        }
        b.frozen = std::max(0.0f, b.frozen - dt);
    }
    if (m_game && m_phase != Phase::Lobby && m_phase != Phase::Podium) m_game->update(*this, dt);
    for (Bean& b : m_beans)
        if (!b.remote) moveBean(b, dt);
    bumpBeans(dt);
    checkFalls();
    for (Bean& b : m_beans) animateBean(b, dt);
    updateParticles(dt);
    sendNet();
    updateCameras(dt);
    updateHud(dt);

    if (m_quitAfter > 0.0f && m_clock >= m_quitAfter) {
        kke::log::get(name())->info("quitting after {:.0f} s (KKE_PARTY_QUIT): round {} of {}, {}", m_clock, m_show.round + 1, m_show.rounds,
                                    m_game ? m_game->title() : "podium");
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
        m_quitAfter = -1.0f;
    }
}

void PartyModule::render(const kke::RenderContext& ctx) {
    if (m_phase == Phase::Lobby || m_phase == Phase::Podium) m_stageMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.7f);
    m_level->draw(ctx, glm::mat4(1.0f), 0.0f, 0.75f);
    kke::RigidWorld& w = world();
    for (const Part& p : m_parts) {
        if (!p.alive || !p.visible || p.translucent || !p.mesh) continue;
        p.mesh->draw(ctx, p.body != kke::RigidWorld::kNoBody ? w.transform(p.body) : p.transform, p.metallic, p.roughness);
    }
    if (m_game) m_game->render(*this, ctx);
    for (const Bean& b : m_beans) drawBean(b, &ctx, nullptr);
    m_particleMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.5f);
    // Glass last: what's behind it is drawn.
    for (const Part& p : m_parts) {
        if (!p.alive || !p.visible || !p.translucent || !p.mesh) continue;
        p.mesh->drawTranslucent(ctx, p.body != kke::RigidWorld::kNoBody ? w.transform(p.body) : p.transform, p.roughness);
    }
}

void PartyModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_phase == Phase::Lobby || m_phase == Phase::Podium) m_stageMesh->drawShadow(ctx);
    m_level->drawShadow(ctx);
    kke::RigidWorld& w = world();
    for (const Part& p : m_parts) {
        if (!p.alive || !p.visible || !p.shadow || !p.mesh) continue;
        p.mesh->drawShadow(ctx, p.body != kke::RigidWorld::kNoBody ? w.transform(p.body) : p.transform);
    }
    for (const Bean& b : m_beans) drawBean(b, nullptr, &ctx);
}

} // namespace party
