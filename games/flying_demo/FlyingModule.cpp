// The flight (README.md "How it works"): the start, the countdown, every
// plane's step, the rings and laps, the stunts, crashing and coming
// back, the results; the cameras (chase, cockpit, far chase, one per
// player in split screen) and the engines' sound.

#include "FlyingModule.h"

#include "kke/Application.h"
#include "kke/AudioMixer.h"
#include "kke/DevTools.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/Mood.h"
#include "kke/ParticleEffects.h"
#include "kke/SphereImpostors.h"
#include "kke/Viewports.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/UiModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>
#include <string>
#include <string_view>

namespace flying {

namespace {

constexpr float kCountdown = 3.0f;      // s of 3, 2, 1
constexpr float kRespawn = 2.5f;        // s down after a crash
constexpr float kLost = 45.0f;          // s without a ring: a CPU pilot is put back on the course
constexpr float kFarOut = 4200.0f;      // m from the middle: turned back to the island
constexpr size_t kMaxParticles = 16000; // six planes' smoke trails (7 s each) and the explosions
constexpr float kSafeUntil = 10.0f;     // m up: a plane from the runway can be hit (and hit others) from here
constexpr float kMaxStep = 1.0f / 120.0f;
constexpr int kSampleRate = 48000;

float yawToward(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, -d.z)); }

float envFloat(const char* name, float fallback) {
    const char* v = kke::dev::env(name);
    return v && *v ? static_cast<float>(std::atof(v)) : fallback;
}

} // namespace

FlyingModule::FlyingModule() = default;
FlyingModule::~FlyingModule() = default;

std::vector<kke::ModuleDependency> FlyingModule::dependencies() const {
    return { { std::type_index(typeid(kke::InputModule)), true, "the flying controls: gamepad, keyboard and mouse, flight sticks" },
             { std::type_index(typeid(kke::ModelModule)), false, "the Synty stunt plane (a built plane without it)" },
             { std::type_index(typeid(kke::UiModule)), false, "the HUD and the pause menu" },
             { std::type_index(typeid(kke::LobbyModule)), false, "the start menu: players join on any controller, pick a plane" },
             { std::type_index(typeid(kke::NetModule)), false, "online flights: Host / Join in the start menu" },
             { std::type_index(typeid(kke::AudioModule)), false, "the engines, the rings, crashes" } };
}

void FlyingModule::init(kke::Application& app) {
    m_app = &app;
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_lobby = app.getModule<kke::LobbyModule>();
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        // In the air the flight's own pause menu has Start, Select and Esc
        // (it knows who pressed and can hand them another controller); its
        // Settings row opens the shared menus. On the start menu the shared
        // ones answer Select.
        shell->blockPause = [this] { return m_phase != Phase::Lobby; };
        shell->onMainMenu = [this] {
            if (m_phase != Phase::Lobby) backToLobby();
        };
    }
    m_net = app.getModule<kke::NetModule>();
    m_audio = app.getModule<kke::AudioModule>();

    m_autopilot = kke::dev::flag("KKE_FLY_AUTOPILOT");
    m_quitAfter = envFloat("KKE_FLY_QUIT", -1.0f);
    m_defaultCpus = std::clamp(static_cast<int>(envFloat("KKE_FLY_CPUS", 3.0f)), 0, kke::Lobby::kMaxCpus);
    m_stuntTime = std::max(10.0f, envFloat("KKE_FLY_STUNT_TIME", m_stuntTime));
    m_netWait = std::max(0, static_cast<int>(envFloat("KKE_FLY_WAIT", 0.0f)));
    if (const char* cam = kke::dev::env("KKE_FLY_CAMERA")) m_startCamera = std::string_view(cam) == "cockpit" ? 1 : std::string_view(cam) == "far" ? 2 : 0;
    if (const char* bench = kke::dev::env("KKE_FLY_BENCH")) m_pileup = std::string_view(bench) == "pileup";

    defineActions();
    kke::Camera& cam = app.camera();
    cam.fovDegrees = 65.0f;
    cam.nearPlane = 0.3f;
    cam.farPlane = 20000.0f; // the sea to the horizon
    app.window().setQuitOnEscape(false); // Esc pauses
    if (m_lobby) m_lobby->setFlightSticks(true); // a flight stick's trigger joins too

    loadArt();
    m_fx = std::make_unique<kke::ParticleEffects>(app, kMaxParticles);
    setupLobby();
    setupNet();
    readSettings();
    m_mood = moodName();
    if (app.mood().name != m_mood) app.setMood(m_mood);
    buildHud();

    // The engines: one live stream, every local plane's engine mixed in.
    if (m_audio) {
        m_engine = std::make_shared<kke::AudioStream>(kSampleRate / 2);
        kke::VoiceDesc d;
        d.stream = m_engine;
        d.spatial = false;
        d.gain = 0.55f;
        d.category = kke::SoundCategory::Ambient;
        d.loop = false;
        m_engineVoice = m_audio->play(d);
    }

    buildPilots(wantedRoster());
    newFlight(); // the island behind the start menu
    if (!m_lobby || !m_lobby->isOpen()) startFromLobby();
}

void FlyingModule::shutdown() {
    if (m_engine) m_engine->close();
    if (m_audio && m_engineVoice) m_audio->mixer().stop(m_engineVoice);
    for (Pilot& p : m_pilots) removeArt(p);
    m_pilots.clear();
    for (kke::ModelModule::InstanceId h : m_houses) m_models->remove(h);
    m_houses.clear();
    m_chunkMeshes.clear();
    m_fx.reset();
}

void FlyingModule::readSettings() {
    m_mode = mode();
    m_seed = islandSeed();
    if (m_lobby) {
        kke::Lobby& l = m_lobby->lobby();
        if (const kke::Lobby::Option* o = l.option("laps")) m_laps = o->value + 1;
        if (const kke::Lobby::Option* o = l.option("rings")) m_ringRadius = o->value == 0 ? 18.0f : o->value == 2 ? 10.0f : 14.0f;
        if (const kke::Lobby::Option* o = l.option("kills")) m_killsToWin = o->value == 0 ? 5 : o->value == 2 ? 20 : 10;
    }
    m_ringCount = 10;
}

// The lobby's line-up as planes (CPU pilots included): new ones get their
// art, the ones that left lose theirs.
void FlyingModule::buildPilots(const std::vector<Entry>& roster) {
    for (Pilot& p : m_pilots) removeArt(p);
    m_pilots.clear();
    int slot = 0;
    for (const Entry& e : roster) {
        Pilot p;
        p.seat = e.seat;
        p.cpu = e.cpu;
        p.skill = e.skill;
        p.slot = slot++;
        p.name = e.name;
        p.tint = e.tint;
        p.livery = e.livery;
        p.netId = e.netId;
        p.remote = e.remote;
        p.autopilotOn = m_autopilot && e.seat >= 0;
        p.cameraMode = m_startCamera;
        if (m_lobby && e.seat >= 0) p.player = std::max(0, m_lobby->playerOf(e.seat));
        m_pilots.push_back(std::move(p));
    }
    for (Pilot& p : m_pilots) spawnArt(p);
}

// A fresh flight on the settings now: the island (built again only when
// it changed), the rings, everyone at the start, the countdown.
void FlyingModule::newFlight() {
    if (m_island.seed() != m_seed || !m_terrain) {
        m_island = Island(m_seed);
        buildWorld();
    }
    // The rings before the town: the Mega City leaves an avenue along them.
    if (m_ringSeed != m_seed || m_builtRadius != m_ringRadius || m_builtRings != m_ringCount) {
        m_ringSeed = m_seed;
        m_builtRadius = m_ringRadius;
        m_builtRings = m_ringCount;
        buildRings();
    }
    if (!m_townBuilt || m_townSeed != m_seed || m_townDistrict != (m_mode == Mode::Dogfight) || m_townRings != m_rings.size()) buildTown();
    for (Pilot& p : m_pilots) {
        p.nextRing = p.lap = 0;
        p.finished = false;
        p.finishTime = 0.0f;
        p.sinceRing = 0.0f;
        p.respawnIn = 0.0f;
        p.crashes = 0;
        p.stunts.reset();
        p.trick.clear();
        p.trickTime = 0.0f;
        p.autopilot.reset();
        p.controls = Controls{};
        p.mouseStick = glm::vec2(0.0f);
        p.stickThrottle = -2.0f;
        p.eyeSet = false;
        p.smoke = m_mode == Mode::Stunts;
        p.health = 100.0f;
        p.safe = false;
        p.bumpCooldown = 0.0f;
        p.lastBy = -1;
        p.downCause = 0;
        p.kills = p.deaths = 0;
        p.firing = false;
        p.gunCooldown = 0.0f;
        p.target = -1;
        p.owed = 0.0f;
        p.wasDown = false;
        clearDents(p);
        if (!p.remote) placeAtStart(p);
        p.previous = p.remote ? p.net.position : p.plane.position;
    }
    m_bullets.clear();
    m_fireballs.clear();
    m_chunks.clear();
    if (m_fx) m_fx->clear();
    m_clock = 0.0f;
    m_countdown = kCountdown;
    m_pileupIn = 0.0f;
    m_pileups = 0;
    m_phase = m_phase == Phase::Lobby ? Phase::Lobby : Phase::Countdown;
    if (m_phase != Phase::Lobby) warmUpExplosions();
    m_flash.clear();
    m_flashTime = 0.0f;
    ++m_round;
}

// Every mode starts on the runway, two by two, engines idling: full
// throttle, and at take-off speed the plane lifts off by itself. Behind
// the start menu the planes are already up, flying round the island.
void FlyingModule::placeAtStart(Pilot& p) {
    p.climbOut = false;
    p.safe = false;
    if (m_phase != Phase::Lobby) {
        const Runway& r = m_island.runway();
        const int row = p.slot / 2, side = p.slot % 2 == 0 ? -1 : 1;
        const float apart = std::max(7.0f, m_shape.bound + 1.0f); // wing tips clear of the next plane's
        const glm::vec3 at = r.start + glm::vec3(static_cast<float>(side) * apart, 0.0f, -18.0f - static_cast<float>(row) * 28.0f);
        p.plane = parked(at, 0.0f, m_flight);
        p.controls.throttle = 0.0f;
        p.climbOut = true;
        p.safe = true; // on the runway: nothing can hit it until it flies
        return;
    }
    const float a = static_cast<float>(p.slot) / 6.0f * glm::two_pi<float>();
    glm::vec3 at(std::cos(a) * 700.0f, 0.0f, std::sin(a) * 700.0f);
    at.y = std::max(260.0f, m_island.surface(at.x, at.z) + 200.0f);
    p.plane = airborne(at, yawToward(glm::vec3(-at.z, 0.0f, at.x)), 55.0f);
    p.controls.throttle = 0.8f;
}

// Back after a crash: on the runway, safe from bullets and other planes
// until it is in the air again. Put back for other reasons (a lost CPU
// pilot, too far out to sea): in the air at the last ring passed (Race),
// or over the island.
void FlyingModule::respawn(Pilot& p, bool crashed) {
    p.respawnIn = 0.0f;
    p.health = 100.0f;
    p.bumpCooldown = 0.0f;
    p.lastBy = -1;
    p.downCause = 0;
    p.firing = false;
    p.climbOut = false;
    p.inLane = false;
    p.target = -1;
    clearDents(p);
    p.autopilot.reset();
    p.controls = Controls{};
    p.controls.throttle = 0.8f;
    p.mouseStick = glm::vec2(0.0f);
    p.eyeSet = false;
    p.teleported = true;
    if (m_pileup) {
        placePileup(p);
        return;
    }
    if (crashed || (m_mode == Mode::Race && p.nextRing == 0 && p.lap == 0)) {
        placeAtStart(p);
        p.sinceRing = 0.0f;
        p.previous = p.plane.position;
        return;
    }
    if (m_mode == Mode::Race && !m_rings.empty()) {
        const int n = static_cast<int>(m_rings.size());
        const Ring& last = m_rings[static_cast<size_t>((p.nextRing + n - 1) % n)];
        const Ring& next = m_rings[static_cast<size_t>(p.nextRing % n)];
        glm::vec3 at = last.center + last.normal * 10.0f;
        at.y = std::max(at.y, m_island.surface(at.x, at.z) + 40.0f);
        p.plane = airborne(at, yawToward(next.center - at), 50.0f);
        p.sinceRing = 0.0f;
        p.previous = p.plane.position;
        return;
    }
    if (m_mode == Mode::Dogfight) {
        // Round the town, high, where the nearest enemy is furthest away.
        const glm::vec3 c = m_town.centre();
        glm::vec3 best = c + glm::vec3(900.0f, 120.0f, 0.0f);
        float bestGap = -1.0f;
        for (int k = 0; k < 8; ++k) {
            const float a = static_cast<float>(k) / 8.0f * glm::two_pi<float>();
            glm::vec3 at = c + glm::vec3(std::cos(a) * 850.0f, 0.0f, std::sin(a) * 850.0f);
            at.y = std::max(c.y + 120.0f, m_island.surface(at.x, at.z) + 150.0f);
            float gap = 1e9f;
            for (const Pilot& o : m_pilots)
                if (&o != &p && !down(o) && present(o)) gap = std::min(gap, glm::length((o.remote ? o.net.position : o.plane.position) - at));
            if (gap > bestGap) {
                bestGap = gap;
                best = at;
            }
        }
        p.plane = airborne(best, yawToward(c - best), 55.0f);
        p.previous = p.plane.position;
        return;
    }
    glm::vec3 at = p.plane.position;
    const float out = glm::length(glm::vec2(at.x, at.z));
    if (out > kFarOut * 0.8f) at *= (kFarOut * 0.6f) / out;
    at.y = std::max(at.y, m_island.surface(at.x, at.z) + 180.0f);
    p.plane = airborne(at, yawToward(-at), 50.0f);
    p.previous = p.plane.position;
}

// The crash benchmark: every plane on a circle 300 m out, nose to the
// middle at full speed, so they all meet there about 4 s later.
void FlyingModule::placePileup(Pilot& p) {
    const Runway& r = m_island.runway();
    glm::vec3 c = m_mode == Mode::Dogfight ? m_town.centre() : glm::vec3(r.start.x, 0.0f, r.start.z - r.length * 0.5f);
    c.y = m_island.surface(c.x, c.z) + 250.0f;
    const float speed = m_flight.maxSpeed * 0.85f;
    const float a = static_cast<float>(p.slot) / static_cast<float>(std::max<size_t>(m_pilots.size(), 1)) * glm::two_pi<float>();
    const glm::vec3 at = c + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * (speed * 4.0f);
    p.plane = airborne(at, yawToward(c - at), speed);
    p.controls = Controls{};
    p.controls.throttle = 1.0f;
    p.climbOut = false;
    p.previous = p.plane.position;
}

void FlyingModule::updatePileup(float dt) {
    constexpr float kEvery = 7.0f; // s: 4 to meet, 2.5 down, a moment to spare
    m_pileupIn -= dt;
    if (m_pileupIn > 0.0f) return;
    if (m_pileups > 0) {
        int down = 0, planes = 0;
        for (const Pilot& p : m_pilots) {
            if (p.remote) continue;
            ++planes;
            if (p.respawnIn > 0.0f) ++down;
        }
        kke::log::get(name())->info("pile-up {}: {} of {} planes exploded", m_pileups, down, planes);
    }
    ++m_pileups;
    m_pileupIn = kEvery;
    for (Pilot& p : m_pilots)
        if (!p.remote) respawn(p);
}

void FlyingModule::crash(Pilot& p) {
    if (p.respawnIn > 0.0f) return;
    ++p.crashes;
    p.respawnIn = kRespawn;
    p.smoke = m_mode == Mode::Stunts ? p.smoke : false;
    if (m_mode == Mode::Stunts) p.stunts.crashed();
    p.trick.clear();
    p.health = 0.0f;
    p.firing = false;
    explode(p.plane.position, p.plane.velocity, p.tint);
    if (!p.cpu) kke::log::get(name())->info("{} {} at ({:.0f}, {:.0f}, {:.0f}), {:.0f} m/s", p.name,
                                            p.downCause == 1 ? "was shot down" : p.downCause == 2 ? "collided" : "crashed", p.plane.position.x,
                                            p.plane.position.y, p.plane.position.z, glm::length(p.plane.velocity));
    wentDown(p);
}

void FlyingModule::passRings(Pilot& p, const glm::vec3& from) {
    if (m_mode != Mode::Race || m_rings.empty() || p.finished) return;
    const int n = static_cast<int>(m_rings.size());
    const Ring& ring = m_rings[static_cast<size_t>(p.nextRing % n)];
    if (!throughRing(ring, from, p.plane.position)) return;
    p.sinceRing = 0.0f;
    p.autopilot.reset();
    ++p.nextRing;
    const bool local = p.seat >= 0;
    if (p.nextRing >= n) {
        p.nextRing = 0;
        ++p.lap;
        if (p.lap >= m_laps) {
            p.finished = true;
            p.finishTime = m_clock;
            kke::log::get(name())->info("{} finished in {:.2f} s ({} crashes)", p.name, m_clock, p.crashes);
            if (local && m_audio) m_audio->playEarcon(kke::Earcon::ToggleOn, 0.9f);
            return;
        }
        if (local) {
            m_flash = p.lap + 1 == m_laps ? "Last lap!" : "Lap " + std::to_string(p.lap + 1);
            m_flashTime = 2.0f;
        }
    }
    if (local && m_audio) m_audio->playEarcon(kke::Earcon::Activate, 0.7f);
}

bool FlyingModule::down(const Pilot& p) const { return p.remote ? p.net.crashed : (p.plane.crashed || p.respawnIn > 0.0f); }

int FlyingModule::score(const Pilot& p) const { return p.remote ? static_cast<int>(p.net.score) : p.stunts.score(); }

float FlyingModule::progress(const Pilot& p) const {
    if (m_mode == Mode::Stunts) return static_cast<float>(score(p));
    if (m_mode == Mode::Dogfight) {
        const int kills = p.remote ? p.net.kills : p.kills, deaths = p.remote ? p.net.deaths : p.deaths;
        return static_cast<float>(kills) * 1000.0f - static_cast<float>(deaths);
    }
    const int n = std::max(1, static_cast<int>(m_rings.size()));
    const bool finished = p.remote ? p.net.finished : p.finished;
    if (finished) return 1e9f - (p.remote ? p.net.finishTime : p.finishTime);
    const int lap = p.remote ? p.net.lap : p.lap, next = p.remote ? p.net.nextRing : p.nextRing;
    const glm::vec3 at = p.remote ? p.net.position : p.plane.position;
    const float toGo = m_rings.empty() ? 0.0f : glm::length(m_rings[static_cast<size_t>(next % n)].center - at);
    return static_cast<float>(lap * n + next) * 10000.0f - std::min(toGo, 9999.0f);
}

int FlyingModule::place(const Pilot& p) const {
    const float mine = progress(p);
    int ahead = 0;
    for (const Pilot& o : m_pilots)
        if (&o != &p && progress(o) > mine) ++ahead;
    return ahead + 1;
}

// Race: every plane at this screen home (or everyone, with nobody here).
// Stunts: the time is up.
bool FlyingModule::everyoneDone() const {
    if (m_mode == Mode::Stunts) return m_clock >= m_stuntTime;
    if (m_mode == Mode::Dogfight) {
        if (m_clock >= m_dogfightTime) return true;
        for (const Pilot& p : m_pilots)
            if ((p.remote ? p.net.kills : p.kills) >= m_killsToWin) return true;
        return false;
    }
    if (m_mode != Mode::Race) return false;
    bool anyLocal = false, localDone = true, allDone = true;
    for (const Pilot& p : m_pilots) {
        const bool done = p.remote ? p.net.finished : p.finished;
        allDone = allDone && done;
        if (p.seat >= 0 && !p.remote) {
            anyLocal = true;
            localDone = localDone && done;
        }
    }
    return anyLocal ? localDone : allDone;
}

void FlyingModule::updatePilot(Pilot& p, float dt) {
    if (p.remote) return; // Net.cpp moves it
    if (p.respawnIn > 0.0f) {
        if (m_pileup) return; // back with everyone at the next pile-up
        p.respawnIn -= dt;
        if (p.respawnIn <= 0.0f) respawn(p, true);
        return;
    }
    p.previous = p.plane.position;
    const bool frozen = m_phase == Phase::Countdown;
    const bool paused = m_pauseSeat >= 0 && p.seat == m_pauseSeat;
    // In the pause menu online (the flight goes on), the CPU pilot flies
    // your plane until you're back.
    if (m_pileup && m_phase != Phase::Lobby) {
        // Straight and level into the middle, whoever flies it.
    } else if (m_phase == Phase::Lobby || p.cpu || p.autopilotOn || paused) {
        p.controls = readCpu(p);
    } else {
        p.controls = readPlayer(p, dt);
    }
    if (frozen) {
        // Waiting for "go": held where they are, engines running.
        p.plane.velocity = p.plane.forward() * (p.plane.onGround ? 0.0f : 52.0f);
        return;
    }
    const Ground g = m_island.ground();
    const int steps = std::max(1, static_cast<int>(std::ceil(dt / kMaxStep)));
    for (int i = 0; i < steps; ++i) {
        const glm::vec3 before = p.plane.position;
        step(p.plane, p.controls, m_flight, g, dt / static_cast<float>(steps));
        passRings(p, before);
        if (p.plane.crashed) break;
    }
    const float clearance = p.plane.position.y - m_island.surface(p.plane.position.x, p.plane.position.z);
    if (m_mode == Mode::Stunts && m_phase == Phase::Flying) {
        const StuntTracker::Trick t = p.stunts.update(p.plane, clearance, dt);
        if (!t.name.empty()) {
            p.trick = t.chain > 1 ? t.name + " x" + std::to_string(t.chain) : t.name;
            p.trick += "  +" + std::to_string(t.points);
            p.trickTime = 2.5f;
            if (p.seat >= 0 && m_audio) m_audio->playEarcon(kke::Earcon::Tick, 0.6f);
        }
    }
    p.trickTime = std::max(0.0f, p.trickTime - dt);
    if (p.plane.crashed) {
        crash(p);
        return;
    }
    if (p.climbOut && !p.plane.onGround && clearance > 60.0f) p.climbOut = false;
    if (p.safe && !p.plane.onGround && clearance > kSafeUntil) p.safe = false;
    p.sinceRing += dt;
    // A CPU pilot lost for too long (a ring it keeps missing): back on the course.
    // The canyon's first ring is a long way round the gorge from the runway.
    const float lost = m_island.map() == Map::Canyon ? kLost * 2.0f : kLost;
    if (p.cpu && m_mode == Mode::Race && !p.finished && p.sinceRing > lost) {
        kke::log::get(name())->info("{} lost the course: back at its last ring", p.name);
        respawn(p);
    }
    // Too far out to sea: back over the island.
    if (glm::length(glm::vec2(p.plane.position.x, p.plane.position.z)) > kFarOut) {
        if (p.seat >= 0) {
            m_flash = "Too far out: back to the island";
            m_flashTime = 2.5f;
        }
        respawn(p);
    }
}

// Chase (behind and above, leaning a little into a bank), cockpit, or far
// chase. The look (right stick, hat, the mouse with its right button)
// turns the view around the plane.
void FlyingModule::updateCamera(Pilot& p, float dt) {
    kke::Camera& cam = p.camera;
    const kke::Camera& base = m_app->camera();
    cam.fovDegrees = 65.0f;
    cam.farPlane = base.farPlane;
    const glm::vec3 pos = p.remote ? p.net.position : p.plane.position;
    const glm::quat rot = p.remote ? p.drawnRotation : p.plane.rotation;
    const glm::quat look = glm::angleAxis(glm::radians(-p.look.x), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(p.look.y), glm::vec3(1, 0, 0));
    const float k = 1.0f - std::exp(-dt * 7.0f);
    if (down(p) && p.eyeSet) {
        // Watching the crash from where the camera was.
        cam.target = glm::mix(cam.target, pos, k);
        return;
    }
    if (p.cameraMode == 1) {
        cam.nearPlane = 0.15f;
        const glm::vec3 eye = pos + rot * (m_art.loaded ? m_art.eye : Art{}.eye);
        cam.position = eye;
        cam.target = eye + rot * (look * glm::vec3(0.0f, 0.0f, -10.0f));
        cam.up = rot * glm::vec3(0.0f, 1.0f, 0.0f);
        p.eyeSet = false;
        return;
    }
    cam.nearPlane = 0.5f;
    const float back = p.cameraMode == 2 ? 36.0f : 13.0f, high = p.cameraMode == 2 ? 9.0f : 3.6f;
    // The camera leans with the plane, but only partly: the horizon still
    // tells you which way is up.
    const glm::vec3 planeUp = rot * glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 upWant = glm::normalize(glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), planeUp, 0.55f));
    if (glm::length(glm::mix(glm::vec3(0.0f, 1.0f, 0.0f), planeUp, 0.55f)) < 0.2f) upWant = planeUp; // upside down: follow the plane
    const glm::vec3 offsetWant = rot * (look * glm::vec3(0.0f, high, back));
    if (!p.eyeSet) {
        p.eye = offsetWant;
        p.eyeUp = upWant;
        p.eyeSet = true;
    }
    p.eye = glm::mix(p.eye, offsetWant, k);
    p.eyeUp = glm::normalize(glm::mix(p.eyeUp, upWant, 1.0f - std::exp(-dt * 3.0f)));
    cam.position = pos + p.eye;
    // Never under the ground or the sea.
    const float floor = m_island.surface(cam.position.x, cam.position.z) + 1.5f;
    cam.position.y = std::max(cam.position.y, floor);
    cam.target = pos + rot * (look * glm::vec3(0.0f, 1.2f, -18.0f)) * 0.6f;
    const glm::vec3 view = glm::normalize(cam.target - cam.position);
    cam.up = std::abs(glm::dot(view, p.eyeUp)) > 0.98f ? planeUp : p.eyeUp;
}

// The engines: a buzzing note per local plane (a propeller's two blades
// chopping the air at the engine's revs), higher with the throttle and
// the speed, mixed into one stream a little ahead of the speakers.
void FlyingModule::updateEngineSound(float) {
    if (!m_engine) return;
    struct Voice {
        float freq, gain;
    };
    std::vector<Voice> voices;
    for (const Pilot& p : m_pilots) {
        if (p.remote || down(p)) continue;
        if (p.seat < 0 && !(m_phase == Phase::Lobby && voices.empty())) continue;
        const float speed = glm::length(p.plane.velocity);
        voices.push_back({ 42.0f + 58.0f * p.controls.throttle + 0.35f * speed, (0.35f + 0.65f * p.controls.throttle) * (m_phase == Phase::Lobby ? 0.3f : 1.0f) });
        if (voices.size() == 4) break;
    }
    const size_t want = kSampleRate / 10; // 0.1 s ahead
    const size_t have = m_engine->buffered();
    if (have >= want) return;
    const size_t count = want - have;
    std::vector<float> buf(count, 0.0f);
    const float norm = voices.empty() ? 0.0f : 0.22f / std::sqrt(static_cast<float>(voices.size()));
    for (size_t v = 0; v < voices.size(); ++v) {
        double& phase = m_enginePhase[v];
        const double step = static_cast<double>(voices[v].freq) / kSampleRate;
        for (size_t i = 0; i < count; ++i) {
            phase += step;
            if (phase >= 1.0) phase -= 1.0;
            const float t = static_cast<float>(phase) * glm::two_pi<float>();
            const float s = 0.55f * std::sin(t) + 0.25f * std::sin(2.0f * t) + 0.12f * std::sin(3.0f * t) + (std::sin(t) > 0.0f ? 0.08f : -0.08f);
            buf[i] += s * voices[v].gain * norm;
        }
    }
    m_engine->push(buf.data(), buf.size());
}

void FlyingModule::updateLobby(float dt) {
    // Someone joined or left, a look changed: the line-up again.
    const std::vector<Entry> roster = wantedRoster();
    bool same = roster.size() == m_pilots.size();
    for (size_t i = 0; same && i < roster.size(); ++i)
        same = roster[i].seat == m_pilots[i].seat && roster[i].tint == m_pilots[i].tint && roster[i].livery == m_pilots[i].livery &&
               roster[i].name == m_pilots[i].name && roster[i].skill == m_pilots[i].skill;
    readSettings();
    if (!same || m_island.seed() != m_seed || m_builtRadius != m_ringRadius || m_townDistrict != (m_mode == Mode::Dogfight)) {
        if (!same) buildPilots(roster);
        newFlight();
    }
    // Behind the menu: the planes fly round the island, and the camera
    // follows the first one from alongside.
    for (Pilot& p : m_pilots) {
        updatePilot(p, dt);
        poseArt(p, dt);
        updateTrail(p, dt);
    }
    m_app->views().clear();
    if (!m_pilots.empty()) {
        const Pilot& lead = m_pilots.front();
        kke::Camera& cam = m_app->camera();
        const glm::vec3 fwd = lead.plane.forward();
        const glm::vec3 side = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)) + glm::vec3(0.0f, 0.001f, 0.0f));
        const glm::vec3 want = lead.plane.position - side * 28.0f - fwd * 22.0f + glm::vec3(0.0f, 6.0f, 0.0f);
        cam.position = glm::mix(cam.position, want, 1.0f - std::exp(-dt * 2.0f));
        cam.position.y = std::max(cam.position.y, m_island.surface(cam.position.x, cam.position.z) + 3.0f);
        cam.target = lead.plane.position;
        cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
        cam.nearPlane = 0.5f;
    }
    if (m_lobby->lobby().takeStart()) startFromLobby();
}

void FlyingModule::startFromLobby() {
    if (netClient()) {
        if (m_lobby) m_lobby->lobby().toast("The host starts the flight", 3.0f);
        return;
    }
    if (m_lobby && m_lobby->isOpen()) {
        m_lobby->save();
        m_lobby->close();
    }
    readSettings();
    if (m_mood != moodName()) {
        m_mood = moodName();
        m_app->setMood(m_mood);
    }
    if (netHost()) syncNetPlayers();
    buildPilots(netHost() ? onlineRoster() : wantedRoster());
    if (m_lobby) {
        m_lobby->applyInput();
        for (Pilot& p : m_pilots)
            if (p.seat >= 0) p.player = std::max(0, m_lobby->playerOf(p.seat));
    } else {
        m_input->setPlayers(1);
    }
    m_phase = Phase::Countdown;
    newFlight();
    m_phase = Phase::Countdown;
    const char* modes[] = { "race", "stunts", "free flight", "dogfight" };
    int here = 0, online = 0, cpus = 0;
    for (const Pilot& p : m_pilots) {
        if (p.remote) ++online;
        else if (p.cpu) ++cpus;
        else ++here;
    }
    kke::log::get(name())->info("{} on island {} ({} rings of {:.0f} m, {} laps): {} playing here, {} online, {} CPU", modes[static_cast<int>(m_mode)],
                                m_seed, m_rings.size(), m_ringRadius, m_laps, here, online, cpus);
}

void FlyingModule::backToLobby() {
    closePause();
    m_phase = Phase::Lobby;
    m_randomSeed = 0; // Random: another island next time
    m_app->views().clear();
    if (m_lobby) m_lobby->open();
    newFlight();
}

void FlyingModule::update(const kke::UpdateContext& ctx) {
    const float dt = std::min(ctx.dt, 0.1f);
    m_runTime += ctx.dt;
    if (m_input->players() > 0 && m_input->map(0).pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    updateNet(dt);
    if (m_phase == Phase::Lobby && (!m_lobby || !m_lobby->isOpen()) && !netClient()) startFromLobby();
    if (m_phase == Phase::Lobby) {
        // The start menu (or, online, waiting for the host's flight).
        if (m_lobby) updateLobby(dt);
        updateEffects(dt);
        rebuildTrails();
        updateEngineSound(dt);
        updateHud(dt);
        return;
    }

    // Pause: whoever presses it runs the menu with their controller.
    if (m_pauseSeat < 0 && m_phase != Phase::Results) {
        for (const Pilot& p : m_pilots)
            if (p.seat >= 0 && !p.remote && pressedBy(p.player, "fly.pause")) {
                openPause(p.seat);
                if (m_audio) m_audio->playEarcon(kke::Earcon::Focus, 0.6f);
                break;
            }
    } else if (m_pauseSeat >= 0) {
        updatePause(dt);
    }
    if (m_phase == Phase::Lobby) { // the pause menu's "Start menu"
        updateHud(dt);
        return;
    }
    const bool online = m_net && m_net->connected();
    const bool frozen = m_pauseSeat >= 0 && !online; // offline, the pause stops everything

    if (!frozen) {
        if (m_phase == Phase::Countdown) {
            const int before = static_cast<int>(std::ceil(m_countdown));
            m_countdown -= dt;
            const int after = static_cast<int>(std::ceil(m_countdown));
            if (after != before && m_audio) m_audio->playEarcon(after > 0 ? kke::Earcon::Tick : kke::Earcon::Activate, 0.8f);
            if (m_countdown <= 0.0f) {
                m_phase = Phase::Flying;
                m_clock = 0.0f;
            }
        } else {
            m_clock += dt;
            if (m_pileup) updatePileup(dt);
        }
        for (Pilot& p : m_pilots) updatePilot(p, dt);
        collide(dt);
        m_gunsHere = false;
        for (Pilot& p : m_pilots) {
            if (p.remote) p.firing = p.net.firing;
            fireGuns(p, dt);
        }
        updateBullets(dt);
        sendOwed(dt);
        updateEffects(dt);
        if (m_phase == Phase::Flying && everyoneDone()) {
            m_phase = Phase::Results;
            if (m_audio) m_audio->playEarcon(kke::Earcon::ToggleOn, 0.8f);
            if (m_mode == Mode::Stunts)
                for (const Pilot& p : m_pilots) kke::log::get(name())->info("{}: {} points", p.name, score(p));
            if (m_mode == Mode::Dogfight)
                for (const Pilot& p : m_pilots)
                    kke::log::get(name())->info("{}: {} kills, {} deaths", p.name, p.remote ? p.net.kills : p.kills, p.remote ? p.net.deaths : p.deaths);
        }
        m_flashTime = std::max(0.0f, m_flashTime - dt);
    }
    // Results: again, or the menu.
    if (m_phase == Phase::Results && m_pauseSeat < 0) {
        bool again = false, menu = false;
        for (const Pilot& p : m_pilots)
            if (p.seat >= 0 && !p.remote) {
                again = again || pressedBy(p.player, "fly.again");
                menu = menu || pressedBy(p.player, "fly.menu");
            }
        if (menu && m_lobby) {
            backToLobby();
            updateHud(dt);
            return;
        }
        if (again && !netClient()) {
            newFlight();
            m_phase = Phase::Countdown;
            if (netHost()) sendSetup();
        }
    }
    for (Pilot& p : m_pilots) {
        poseArt(p, frozen ? 0.0f : dt);
        if (!frozen) updateTrail(p, dt);
    }
    rebuildTrails();
    sendNet();

    // Cameras: one per player here (split screen), sorted by player.
    std::vector<Pilot*> views;
    for (Pilot& p : m_pilots)
        if (p.seat >= 0 && !p.remote) views.push_back(&p);
    std::sort(views.begin(), views.end(), [](const Pilot* a, const Pilot* b) { return a->player < b->player; });
    std::vector<kke::Application::View>& appViews = m_app->views();
    appViews.clear();
    if (views.empty() && !m_pilots.empty()) views.push_back(&m_pilots.front()); // nobody here: watch the first plane
    for (Pilot* p : views) updateCamera(*p, dt);
    if (views.size() == 1) {
        m_app->camera() = views[0]->camera;
    } else if (views.size() > 1) {
        const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(views.size()), true);
        for (size_t i = 0; i < views.size(); ++i) appViews.push_back({ views[i]->camera, rects[i] });
        m_app->camera() = views[0]->camera; // the ears, and what the engine's own tools look through
        if (views.size() == 3) {
            // The empty quarter: the leader, from high above and behind.
            const Pilot* lead = &m_pilots.front();
            for (const Pilot& p : m_pilots)
                if (place(p) == 1) lead = &p;
            const glm::vec3 at = lead->remote ? lead->net.position : lead->plane.position;
            m_overview = m_app->camera();
            m_overview.nearPlane = 1.0f;
            const glm::vec3 want = at + glm::vec3(0.0f, 120.0f, 0.0f) - glm::normalize(glm::vec3(at.x, 0.0f, at.z) + glm::vec3(0.001f)) * 220.0f;
            m_overview.position = want;
            m_overview.target = at;
            m_overview.up = glm::vec3(0.0f, 1.0f, 0.0f);
            appViews.push_back({ m_overview, kke::splitScreen(4, true)[3] });
        }
    }
    updateEngineSound(dt);
    updateHud(dt);

    // Headless runs: every plane's progress now and then, and quit.
    if (m_quitAfter > 0.0f) {
        if (m_runTime >= m_reportAt) {
            m_reportAt += 5.0f;
            for (const Pilot& p : m_pilots) {
                const glm::vec3 at = p.remote ? p.net.position : p.plane.position;
                kke::log::get(name())->info("t {:.0f} s: {} at ({:.0f}, {:.0f}, {:.0f}) {:.0f} m/s, {}", m_runTime, p.name, at.x, at.y, at.z,
                                            glm::length(p.remote ? p.net.velocity : p.plane.velocity),
                                            m_mode == Mode::Race ? "lap " + std::to_string((p.remote ? p.net.lap : p.lap) + 1) + " ring " +
                                                                       std::to_string((p.remote ? p.net.nextRing : p.nextRing) + 1) +
                                                                       ((p.remote ? p.net.finished : p.finished) ? " (finished)" : "") + (p.remote ? " (online)" : "")
                                            : m_mode == Mode::Stunts   ? std::to_string(score(p)) + " points"
                                            : m_mode == Mode::Dogfight ? std::to_string(p.remote ? p.net.kills : p.kills) + " kills, " +
                                                                             std::to_string(p.remote ? p.net.deaths : p.deaths) + " deaths, health " +
                                                                             std::to_string(static_cast<int>(p.remote ? p.net.health : p.health))
                                                                       : std::string(p.plane.onGround ? "on the ground" : "flying"));
            }
        }
        if (m_runTime >= m_quitAfter) {
            int crashes = 0;
            for (const Pilot& p : m_pilots) crashes += p.crashes;
            kke::log::get(name())->info("quitting after {:.0f} s: {} crashes in all", m_runTime, crashes);
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            m_quitAfter = -1.0f;
        }
    }
}

void FlyingModule::render(const kke::RenderContext& ctx) {
    if (m_sea) m_sea->draw(ctx, glm::mat4(1.0f), 0.1f, 0.15f);
    if (m_terrain) m_terrain->draw(ctx);
    if (m_townMesh) m_townMesh->draw(ctx, glm::mat4(1.0f), 0.05f, 0.7f);
    // Bits of planes that went down.
    for (const Chunk& c : m_chunks) {
        if (c.mesh < 0 || c.mesh >= static_cast<int>(m_chunkMeshes.size())) continue;
        const glm::mat4 m = glm::translate(glm::mat4(1.0f), c.position) * glm::rotate(glm::mat4(1.0f), c.angle, c.axis) * glm::scale(glm::mat4(1.0f), glm::vec3(c.size));
        m_chunkMeshes[static_cast<size_t>(c.mesh)]->draw(ctx, m, 0.3f, 0.6f);
    }
    if (m_mode == Mode::Race && m_phase != Phase::Lobby && m_ringMesh) {
        m_ringMesh->draw(ctx, glm::mat4(1.0f), 0.6f, 0.3f);
        // Each local player's next ring, gold.
        if (m_nextRingMesh && !m_rings.empty()) {
            std::vector<int> shown;
            for (const Pilot& p : m_pilots) {
                if (p.seat < 0 || p.remote || p.finished) continue;
                const int i = p.nextRing % static_cast<int>(m_rings.size());
                if (std::find(shown.begin(), shown.end(), i) != shown.end()) continue;
                shown.push_back(i);
                const Ring& r = m_rings[static_cast<size_t>(i)];
                const glm::mat4 m = glm::translate(glm::mat4(1.0f), r.center) * glm::mat4_cast(glm::quat(glm::vec3(0.0f, 0.0f, -1.0f), r.normal)) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(r.radius * 1.02f));
                m_nextRingMesh->draw(ctx, m, 0.9f, 0.25f);
            }
        }
    }
    for (const Pilot& p : m_pilots) {
        const glm::vec3 at = p.remote ? p.net.position : p.plane.position;
        const glm::quat rot = p.remote ? p.drawnRotation : p.plane.rotation;
        if (down(p) || !present(p)) continue; // in bits (m_chunks), or not heard from yet
        if (p.blockPlane) p.blockPlane->draw(ctx, glm::translate(glm::mat4(1.0f), at) * glm::mat4_cast(rot), 0.2f, 0.45f);
    }
    if (m_smoke && !m_puffs.empty()) m_smoke->draw(ctx, m_puffs);
}

void FlyingModule::renderTranslucent(const kke::RenderContext& ctx) {
    if (m_fx) m_fx->draw(ctx);
}

void FlyingModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_terrain) m_terrain->drawShadow(ctx);
    if (m_townMesh) m_townMesh->drawShadow(ctx);
    for (const Pilot& p : m_pilots) {
        if (!p.blockPlane || down(p) || !present(p)) continue;
        const glm::vec3 at = p.remote ? p.net.position : p.plane.position;
        const glm::quat rot = p.remote ? p.drawnRotation : p.plane.rotation;
        p.blockPlane->drawShadow(ctx, glm::translate(glm::mat4(1.0f), at) * glm::mat4_cast(rot));
    }
}

} // namespace flying
