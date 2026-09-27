// Party modes (DESIGN.md "Party modes"), picked in the start menu's Mode
// row (the host's, online):
//
//   Race         first over the summit wins.
//   Rockfall     rocks tumble down every face at its climber; a hit costs
//                a chunk of stamina (kke::Climber::knock). The higher the
//                leader, the more come.
//   Elimination  every 30 s the lowest climber still in is out: they let
//                go and watch. The last one in, or the first to the top, wins.
//
// Online, each machine drops the same rocks (same seed each race) and
// counts hits on its own climbers; the host alone decides who's out.

#include "ClimbRaceModule.h"

#include "kke/Application.h"
#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>
#include <cctype>

namespace climb_race {

namespace {

const char* const kModeNames[] = { "Race", "Rockfall", "Elimination" };
constexpr float kElimEvery = 30.0f;  // s between eliminations
constexpr float kRockHit = 35.0f;    // stamina a rock costs
constexpr float kRockLife = 9.0f;    // s before a rock is cleared away
constexpr size_t kMaxRocks = 40;
const glm::vec3 kRockHalf(0.24f, 0.19f, 0.21f);

bool sameWord(const char* a, const char* b) {
    for (; *a && *b; ++a, ++b)
        if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
    return *a == *b;
}

float unit(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<float>(state & 0xffffffu) / static_cast<float>(0x1000000);
}

} // namespace

void ClimbRaceModule::setupModes() {
    // The rock every Rockfall rock is drawn with: a box in rock colours.
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 normals[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int f = 0; f < 6; ++f) {
        const glm::vec3 n = normals[f];
        const glm::vec3 u = std::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 w = glm::cross(n, u);
        const glm::vec3 color = glm::vec3(0.46f, 0.42f, 0.38f) * (0.85f + 0.05f * static_cast<float>(f));
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
            v.push_back({ (n + u * k.x + w * k.y) * kRockHalf, color, n, glm::vec2(0.0f) });
        if (glm::dot(glm::cross(u, w), n) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
    m_rockMesh = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_rockMesh->upload(v, idx);

    if (!m_lobby) return;
    kke::Lobby::Option mode{ "mode", "Mode", {}, 0, true, {}, {} };
    for (const char* n : kModeNames) mode.choices.push_back(n);
    m_lobby->lobby().addOption(std::move(mode));
}

ClimbRaceModule::Mode ClimbRaceModule::chosenMode() const {
    if (const char* want = kke::dev::env("KKE_CLIMB_MODE")) {
        for (int i = 0; i < kModes; ++i)
            if (sameWord(want, kModeNames[i])) return static_cast<Mode>(i);
    }
    const kke::Lobby::Option* o = m_lobby ? m_lobby->lobby().option("mode") : nullptr;
    return o ? static_cast<Mode>(std::clamp(o->value, 0, kModes - 1)) : Mode::Race;
}

void ClimbRaceModule::clearRocks() {
    kke::RigidWorld& w = m_rigid->world();
    for (const Rock& r : m_rocks) w.remove(r.body);
    m_rocks.clear();
}

void ClimbRaceModule::startMode() {
    clearRocks();
    // The same rocks on every machine: online, seeded with the host's race number.
    m_rockRng = (netClient() ? m_netRound : m_round) * 2654435761u + m_mountain.desc.seed + 1u;
    if (m_rockRng == 0) m_rockRng = 1;
    m_rockTimers.assign(m_lanes.size(), 3.0f);
    m_elimTimer = kElimEvery;
    m_flash.clear();
    m_flashTime = 0.0f;
    for (Racer& r : m_racers) {
        r.out = false;
        r.hitCooldown = r.hitFlash = 0.0f;
    }
    kke::log::get(name())->info("mode: {}", kModeNames[static_cast<int>(m_mode)]);
}

void ClimbRaceModule::spawnRock(int lane, const glm::vec3& above) {
    if (m_rocks.size() >= kMaxRocks) {
        m_rigid->world().remove(m_rocks.front().body);
        m_rocks.erase(m_rocks.begin());
    }
    const Lane& l = *m_lanes[static_cast<size_t>(lane)];
    // A few metres above the climber (never above the summit), a little
    // to one side, just off the rock: it bounces down the face at them.
    const glm::vec3 wall = above - l.offset;
    const float x = wall.x + (unit(m_rockRng) - 0.5f) * 1.6f;
    const float y = std::min(l.wall->summitY() + 1.0f, wall.y + 6.5f + unit(m_rockRng) * 2.0f);
    const glm::vec3 at = l.wall->surfacePoint(x, y, 0.45f) + l.offset;
    kke::RigidWorld::BodyDesc b;
    b.motion = kke::RigidWorld::Motion::Dynamic;
    b.halfExtents = kRockHalf;
    b.position = at;
    b.density = 2600.0f;
    b.restitution = 0.3f;
    Rock rock;
    rock.body = m_rigid->world().add(b);
    m_rigid->world().setVelocity(rock.body, glm::vec3(0.0f, -1.5f, 0.4f));
    m_rigid->world().setAngularVelocity(rock.body, glm::vec3(unit(m_rockRng) * 6.0f - 3.0f, unit(m_rockRng) * 4.0f - 2.0f, 3.0f));
    m_rocks.push_back(rock);
}

void ClimbRaceModule::eliminate(Racer& r) {
    if (r.out || r.finished) return;
    r.out = true;
    m_flash = r.name + " is out!";
    m_flashTime = 2.5f;
    kke::log::get(name())->info("elimination: {} is out at {:.1f} m", r.name, m_rigid->world().characterPosition(r.id).y);
    if (netHost() && r.netId >= 0)
        m_net->sendEvent(netrace::kEventOut, netrace::encode(netrace::Finish{ static_cast<uint8_t>(r.netId), m_netRound, r.time }));
}

void ClimbRaceModule::updateMode(float dt) {
    m_flashTime = std::max(0.0f, m_flashTime - dt);
    for (Racer& r : m_racers) {
        r.hitCooldown = std::max(0.0f, r.hitCooldown - dt);
        r.hitFlash = std::max(0.0f, r.hitFlash - dt);
    }
    kke::RigidWorld& w = m_rigid->world();
    // Rocks age and go; they only count while falling fast.
    for (size_t i = 0; i < m_rocks.size();) {
        m_rocks[i].age += dt;
        if (m_rocks[i].age > kRockLife || w.position(m_rocks[i].body).y < -5.0f) {
            w.remove(m_rocks[i].body);
            m_rocks.erase(m_rocks.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    if (m_phase != Phase::Racing) return;

    if (m_mode == Mode::Rockfall) {
        float leader = 0.0f;
        for (const Racer& r : m_racers) leader = std::max(leader, w.characterPosition(r.id).y);
        const float high = std::clamp(leader / std::max(1.0f, m_mountain.desc.height), 0.0f, 1.0f);
        for (Racer& r : m_racers) {
            if (isDone(r) || r.lane >= static_cast<int>(m_rockTimers.size())) continue;
            const glm::vec3 hips = w.characterPosition(r.id) + glm::vec3(0.0f, 0.95f, 0.0f);
            float& timer = m_rockTimers[static_cast<size_t>(r.lane)];
            timer -= dt;
            if (timer <= 0.0f) {
                timer = (3.2f - 1.8f * high) * (0.8f + 0.4f * unit(m_rockRng)); // every 1.1 to 3.8 s
                if (hips.y > 1.5f) spawnRock(r.lane, hips); // on the ground there's nothing to hit
            }
            // A hit: a rock coming down fast, at the body. Our own climbers only.
            if (r.remote || r.hitCooldown > 0.0f || !r.climber->climbing()) continue;
            for (const Rock& rock : m_rocks) {
                if (glm::length(w.position(rock.body) - (hips + glm::vec3(0.0f, 0.35f, 0.0f))) > 0.75f || w.velocity(rock.body).y > -2.0f) continue;
                r.climber->knock(kRockHit);
                r.hitCooldown = 1.2f;
                r.hitFlash = 1.5f;
                kke::log::get(name())->info("{} was hit by a rock at {:.1f} m", r.name, hips.y);
                break;
            }
        }
    }

    if (m_mode == Mode::Elimination && !netClient()) {
        std::vector<Racer*> in;
        for (Racer& r : m_racers)
            if (!isDone(r)) in.push_back(&r);
        if (in.size() >= 2) {
            m_elimTimer -= dt;
            if (m_elimTimer <= 0.0f) {
                m_elimTimer = kElimEvery;
                Racer* lowest = *std::min_element(in.begin(), in.end(), [&w](const Racer* a, const Racer* b) {
                    return w.characterPosition(a->id).y < w.characterPosition(b->id).y;
                });
                eliminate(*lowest);
            }
        }
    }
    // Elimination: the last one in wins (unless someone already topped out).
    if (m_mode == Mode::Elimination && m_winner.empty()) {
        const Racer* last = nullptr;
        int left = 0;
        for (const Racer& r : m_racers)
            if (!isDone(r)) {
                ++left;
                last = &r;
            }
        if (left == 1 && m_racers.size() > 1) {
            m_winner = last->name;
            m_flash = last->name + " is the last one climbing!";
            m_flashTime = 3.0f;
            kke::log::get(name())->info("elimination: {} wins, the last one climbing", last->name);
            m_phase = Phase::Finished;
        }
    }
}

} // namespace climb_race
