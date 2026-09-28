// The sport center (README.md "The sport center"): everyone at this screen
// walks about the promenade between the ten courts. Stand at a court's
// gate and press a shot button to play there: a second person at the gate
// makes it a match (up to four, doubles), or Lob plays the CPU now. The
// CPU crowd walks from court to court, sits or stands by the ones with a
// match on and cheers the points; CPU players take the courts nobody
// wants. Every match won goes on the board.

#include "TennisModule.h"

#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/NetModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <algorithm>
#include <cmath>

namespace tennis {

namespace {

constexpr float kWalkSpeed = 4.2f;       // a person at this screen, m/s
constexpr float kCrowdSpeed = 1.5f;      // the crowd strolls
constexpr float kGateReach = 3.0f;       // m from a gate's middle that counts as at it
constexpr float kGateCountdown = 4.0f;   // s: two at a gate, the match starts (more may join)
constexpr float kCourtRest = 6.0f;       // s a court stays empty before CPU players take it
constexpr float kAfterMatch = 6.0f;      // s the winners cheer before everyone leaves the court

const char* const kCrowdNames[] = { "Ada",  "Bo",    "Cleo",  "Dev",  "Eli",   "Fay",  "Gus",  "Hana", "Ivo",  "Jade", "Kai",  "Lena",
                                    "Milo", "Nia",   "Omar",  "Pia",  "Quinn", "Rosa", "Sami", "Tess", "Uma",  "Vik",  "Wren", "Xia",
                                    "Yuri", "Zola",  "Abe",   "Bea",  "Cruz",  "Dina", "Ezra", "Flo",  "Gio",  "Ines", "Jon",  "Kira" };
const char* const kPlayerNames[] = { "Ace", "Deuce", "Volley", "Lobster", "Slice", "Spin", "Smash", "Rally", "Chip", "Drive" };

// A cheap, repeatable die per walker (xorshift).
float roll(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return static_cast<float>(s & 0xffffffu) / static_cast<float>(0x1000000u);
}

float yawOf(const glm::vec3& dir) { return glm::degrees(std::atan2(dir.x, dir.z)); }

} // namespace

glm::vec3 TennisModule::gatePoint(int court) const {
    // The court's end toward the promenade, just outside the fence.
    const CourtPlace& c = m_center.courts[static_cast<size_t>(court)];
    const float end = c.origin.z < 0.0f ? 1.0f : -1.0f;
    return c.toWorld({ 0.0f, 0.0f, end * (kFenceHalfZ + 1.4f) });
}

int TennisModule::gateNear(const glm::vec3& world) const {
    for (int c = 0; c < SportCenter::kCourts; ++c) {
        const glm::vec3 d = world - gatePoint(c);
        if (glm::length(glm::vec2(d.x, d.z)) < kGateReach) return c;
    }
    return -1;
}

TennisModule::Match* TennisModule::matchOn(int court) {
    for (auto& m : m_matches)
        if (m->court == court) return m.get();
    return nullptr;
}

void TennisModule::spawnWalker(const std::string& nm, const glm::vec3& tint, bool cpu, int input, const glm::vec3& at) {
    Walker w;
    w.name = nm;
    w.tint = tint;
    w.cpu = cpu;
    w.input = input;
    kke::RigidWorld::CharacterDesc cd;
    cd.radius = 0.3f;
    cd.height = 1.8f;
    cd.pushStrength = 0.0f;
    kke::RigidWorld& world = m_rigid->world();
    w.body = world.addCharacter(cd);
    world.teleportCharacter(w.body, at + glm::vec3(0.0f, 0.02f, 0.0f));
    w.look = std::make_unique<Body>(*m_rig, *m_models, tint, !cpu);
    w.dice = 0x9e3779b9u * static_cast<uint32_t>(m_walkers.size() + 1) ^ m_seed;
    w.timer = roll(w.dice) * 4.0f;
    w.facing = glm::vec3(0.0f, 0.0f, at.z > 0.0f ? -1.0f : 1.0f);
    w.camYaw = yawOf(w.facing);
    m_walkers.push_back(std::move(w));
}

void TennisModule::enterCenter() {
    clearPlayers();
    m_inCenter = true;
    for (int c = 0; c < SportCenter::kCourts; ++c) m_seatTaken[static_cast<size_t>(c)].assign(m_center.seats(c).size(), -1);
    m_courtRest.fill(0.0f);
    // The people at this screen, on the promenade's middle.
    int n = 0;
    for (const Entry& e : netEntries()) {
        if (e.cpu) continue;
        spawnWalker(e.name, e.tint, false, e.input, m_center.arrival(n++));
        m_walkers.back().netId = online() ? e.netId : -1;
    }
    // The crowd, spread along the promenade.
    uint32_t dice = m_seed * 2654435761u + 7u;
    const float halfX = m_center.hallMax.x - 3.0f;
    for (int i = 0; i < m_crowd; ++i) {
        const glm::vec3 at((roll(dice) * 2.0f - 1.0f) * halfX, 0.0f, (roll(dice) * 2.0f - 1.0f) * 5.5f);
        const glm::vec3 tint(0.45f + 0.6f * roll(dice), 0.45f + 0.6f * roll(dice), 0.45f + 0.6f * roll(dice));
        std::string nm = kCrowdNames[i % static_cast<int>(std::size(kCrowdNames))];
        if (i >= static_cast<int>(std::size(kCrowdNames))) nm += " " + std::to_string(i / static_cast<int>(std::size(kCrowdNames)) + 1);
        spawnWalker(nm, tint, true, -1, at);
    }
    kke::log::get(name())->info("sport center: {} people here, {} in the crowd, CPU players on the free courts: {}", n, m_crowd,
                                m_cpuMatches ? "yes" : "no");
    // CPU players start on the courts people don't walk up to first (the
    // two by the promenade's middle stay free while someone here plays).
    if (netHost()) m_boardDirty = true;
    if (m_cpuMatches && authority())
        for (int c = 0; c < SportCenter::kCourts; ++c)
            if (n == 0 || (c % SportCenter::kPerRow) != SportCenter::kPerRow / 2) startCpuMatch(c);
}

void TennisModule::startCpuMatch(int court) {
    if (matchOn(court)) return;
    uint32_t& dice = m_walkers.empty() ? m_seed : m_walkers.front().dice;
    const bool doubles = roll(dice) < 0.3f;
    std::vector<Entry> entries;
    const int want = doubles ? 4 : 2;
    for (int i = 0; i < want; ++i) {
        Entry e;
        e.cpu = true;
        e.level = std::clamp(static_cast<int>(roll(dice) * 4.0f), 0, 3);
        e.name = std::string(kPlayerNames[static_cast<size_t>(roll(dice) * 9.99f)]) + " (CPU)";
        e.tint = glm::vec3(0.5f + 0.7f * roll(dice), 0.5f + 0.7f * roll(dice), 0.5f + 0.7f * roll(dice));
        entries.push_back(std::move(e));
    }
    const int teams = m_teams;
    m_teams = 0; // across the net
    MatchRules rules = menuRules(want / 2);
    rules.gamesPerSet = std::min(rules.gamesPerSet, 2); // CPU matches are short: the court turns over
    rules.setsToWin = 1;
    Match* m = buildMatch(std::move(entries), rules, court);
    m_teams = teams;
    if (netHost()) sendSetup(*m);
}

// People at the gate play: those waiting, then CPU players to make a
// side each (or two).
void TennisModule::startCourt(int court) {
    Gate& g = m_gates[static_cast<size_t>(court)];
    std::vector<Entry> entries;
    for (int wi : g.waiting) {
        const Walker& w = m_walkers[static_cast<size_t>(wi)];
        Entry e;
        e.name = w.name;
        e.tint = w.tint;
        e.input = w.remote ? -1 : w.input;
        e.remote = w.remote;
        e.netId = w.netId;
        e.walker = wi;
        entries.push_back(std::move(e));
    }
    if (entries.empty() || matchOn(court)) return;
    const size_t want = entries.size() > 2 ? 4 : 2;
    int cpuNumber = 0;
    while (entries.size() < want) {
        Entry e;
        e.cpu = true;
        e.level = m_level;
        e.name = std::string(kPlayerNames[cpuNumber++]) + " (CPU)";
        e.tint = glm::vec3(0.8f, 0.8f, 0.85f);
        entries.push_back(std::move(e));
    }
    entries.resize(want);
    Match* m = buildMatch(std::move(entries), menuRules(static_cast<int>(want) / 2), court);
    walkersOn(*m);
    g = Gate{};
    if (netHost()) sendSetup(*m);
    m_boardDirty = true;
}

// The people in a match step off the promenade (their players take over).
void TennisModule::walkersOn(const Match& m) {
    kke::RigidWorld& world = m_rigid->world();
    for (int idx : m.players) {
        const Player& p = player(idx);
        if (p.walker < 0) continue;
        Walker& w = m_walkers[static_cast<size_t>(p.walker)];
        w.playing = idx;
        w.queued = -1;
        if (w.body) world.removeCharacter(w.body);
        w.body = 0;
        if (w.look) w.look->setVisible(false);
    }
}

void TennisModule::endCenterMatch(size_t matchIndex, int forfeitTeam) {
    Match& m = *m_matches[matchIndex];
    int winner = m.score.over() ? m.score.winner() : -1;
    if (forfeitTeam >= 0) winner = 1 - forfeitTeam;
    if (winner >= 0)
        for (int idx : m.players) {
            const Player& p = player(idx);
            if (p.team != winner) continue;
            auto it = std::find_if(m_wins.begin(), m_wins.end(), [&p](const auto& w) { return w.first == p.name; });
            if (it == m_wins.end()) m_wins.emplace_back(p.name, 1);
            else ++it->second;
        }
    std::stable_sort(m_wins.begin(), m_wins.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    if (netHost() && m.netId) m_net->sendEvent(net::kEventEnd, net::encode(net::End{ m.netId, {} }));
    m_boardDirty = true;
    closeMatch(matchIndex);
}

void TennisModule::closeMatch(size_t matchIndex) {
    Match& m = *m_matches[matchIndex];
    // Whoever walked on walks off, at the gate.
    kke::RigidWorld& world = m_rigid->world();
    const glm::vec3 gate = gatePoint(m.court);
    int k = 0;
    for (int idx : m.players) {
        const Player& p = player(idx);
        if (p.walker >= 0 && p.walker < static_cast<int>(m_walkers.size()) && !m_walkers[static_cast<size_t>(p.walker)].gone) {
            Walker& w = m_walkers[static_cast<size_t>(p.walker)];
            kke::RigidWorld::CharacterDesc cd;
            cd.radius = 0.3f;
            cd.height = 1.8f;
            cd.pushStrength = 0.0f;
            w.body = world.addCharacter(cd);
            world.teleportCharacter(w.body, gate + glm::vec3(static_cast<float>(k++) * 1.0f - 1.5f, 0.02f, 0.0f));
            if (w.remote) world.setCharacterKinematic(w.body, true); // moved by what their machine sends
            w.playing = -1;
            w.cameraInit = false;
            if (w.look) w.look->setVisible(true);
        }
        freePlayer(idx);
    }
    // The crowd watching it finds another court.
    for (Walker& w : m_walkers)
        if (w.cpu && w.court == m.court && w.doing == Walker::Doing::Watch) w.timer = std::min(w.timer, 1.0f + roll(w.dice) * 4.0f);
    m_courtRest[static_cast<size_t>(m.court)] = 0.0f;
    m_matches.erase(m_matches.begin() + static_cast<std::ptrdiff_t>(matchIndex));
}

void TennisModule::gateJoin(int walker, int court) {
    Walker& w = m_walkers[static_cast<size_t>(walker)];
    if (court < 0 || court >= SportCenter::kCourts || w.queued >= 0 || w.playing >= 0) return;
    Gate& g = m_gates[static_cast<size_t>(court)];
    // A court people play on is taken; CPU players make way.
    if (const Match* on = matchOn(court))
        for (int idx : on->players)
            if (player(idx).walker >= 0) return;
    if (g.waiting.size() >= 4) return;
    g.waiting.push_back(walker);
    w.queued = court;
    m_boardDirty = true;
}

void TennisModule::gateLeave(int walker) {
    Walker& w = m_walkers[static_cast<size_t>(walker)];
    if (w.queued < 0) return;
    std::erase(m_gates[static_cast<size_t>(w.queued)].waiting, walker);
    w.queued = -1;
    m_boardDirty = true;
}

void TennisModule::readWalker(Walker& w) {
    if (w.input < 0 || w.input >= m_input->players()) return;
    kke::InputMap& in = m_input->map(w.input);
    w.stick = in.axis2("move");
    w.play = w.play || in.pressed("tennis.topspin") || in.pressed("tennis.flat");
    w.cpuNow = w.cpuNow || in.pressed("tennis.lob");
    w.leave = w.leave || in.pressed("tennis.slice");
}

void TennisModule::stepCenter(float dt) {
    kke::RigidWorld& world = m_rigid->world();
    // Matches that are done: everyone off; CPU players give way to people.
    // (Online, the host decides; a client hears it as End.)
    for (size_t i = authority() ? m_matches.size() : 0; i-- > 0;) {
        Match& m = *m_matches[i];
        bool people = false;
        for (int idx : m.players) people = people || player(idx).walker >= 0;
        const bool wanted = !m_gates[static_cast<size_t>(m.court)].waiting.empty();
        if ((m.phase == Match::Phase::MatchOver && m.phaseTime > kAfterMatch) ||
            (!people && wanted && (m.phase == Match::Phase::PointOver || m.phase == Match::Phase::Warmup)))
            endCenterMatch(i, -1);
    }
    for (size_t wi = 0; wi < m_walkers.size(); ++wi) {
        Walker& w = m_walkers[wi];
        if (w.playing >= 0 || !w.body || w.gone || w.remote) continue;
        if (w.cheer > 0.0f) w.cheer -= dt;
        if (w.cpu) {
            stepCrowd(w, dt);
            continue;
        }
        // A person: the stick walks, as the camera looks.
        const float cy = glm::radians(w.camYaw);
        const glm::vec3 fwd(std::sin(cy), 0.0f, std::cos(cy)), right(-std::cos(cy), 0.0f, std::sin(cy));
        if (m_autoplay) {
            // Tests (KKE_TENNIS_AUTOPLAY): walk to the free court's gate and play the CPU.
            // (Online, by network id: two screens' first people go to different courts.)
            const int court = SportCenter::kPerRow / 2 + ((w.netId >= 0 ? w.netId : static_cast<int>(wi)) % 2) * SportCenter::kPerRow;
            glm::vec3 to = gatePoint(court) - world.characterPosition(w.body);
            to.y = 0.0f;
            const float d = glm::length(to);
            w.stick = d > 1.0f ? glm::vec2(glm::dot(to, right), glm::dot(to, fwd)) / d : glm::vec2(0.0f);
            if (d < 2.0f) {
                w.play = w.queued < 0;
                w.cpuNow = w.queued >= 0;
            }
        }
        glm::vec3 move = right * w.stick.x + fwd * w.stick.y;
        if (glm::length(move) > 1.0f) move = glm::normalize(move);
        kke::RigidWorld::CharacterInput ci;
        ci.move = move * kWalkSpeed;
        world.setCharacterInput(w.body, ci);
        if (glm::length(move) > 0.2f) {
            const float k = 1.0f - std::exp(-10.0f * dt);
            w.facing = glm::normalize(w.facing + (glm::normalize(move) - w.facing) * k + glm::vec3(1e-4f, 0.0f, 0.0f));
        }
        const glm::vec3 feet = world.characterPosition(w.body);
        const int gate = gateNear(feet);
        const int me = static_cast<int>(wi);
        // Online, a client asks the host, who keeps the queues.
        auto ask = [&](int court, uint8_t action) {
            m_net->sendEvent(net::kEventGate, net::encode(net::Gate{ static_cast<uint8_t>(court), static_cast<uint8_t>(std::max(0, w.netId)), action }));
        };
        if (w.queued >= 0 && (gate != w.queued || w.leave)) {
            if (netClient()) {
                ask(w.queued, net::Gate::Leave);
                w.queued = -1;
            } else {
                gateLeave(me);
            }
        }
        if (w.play && gate >= 0 && w.queued < 0) {
            if (netClient()) {
                ask(gate, net::Gate::Join);
                w.queued = gate;
            } else {
                gateJoin(me, gate);
            }
        }
        if (w.cpuNow && w.queued >= 0) {
            if (netClient()) ask(w.queued, net::Gate::CpuNow);
            else if (!matchOn(w.queued)) startCourt(w.queued);
        }
        w.play = w.cpuNow = w.leave = false;
    }
    if (!authority()) return;
    // Two or more at a gate: a countdown, then they play.
    for (int c = 0; c < SportCenter::kCourts; ++c) {
        Gate& g = m_gates[static_cast<size_t>(c)];
        if (g.waiting.size() < 2) {
            g.countdown = -1.0f;
        } else {
            if (g.countdown < 0.0f) g.countdown = kGateCountdown;
            g.countdown -= dt;
            if ((g.countdown <= 0.0f || g.waiting.size() >= 4) && !matchOn(c)) startCourt(c);
        }
        // Courts nobody plays on or waits for get CPU players.
        float& rest = m_courtRest[static_cast<size_t>(c)];
        if (matchOn(c) || !g.waiting.empty()) {
            rest = 0.0f;
        } else if (m_cpuMatches) {
            rest += dt;
            bool person = false;
            for (const Walker& w : m_walkers) person = person || (!w.cpu && !w.gone);
            const bool keptFree = person && (c % SportCenter::kPerRow) == SportCenter::kPerRow / 2;
            if (rest > kCourtRest && !keptFree) startCpuMatch(c);
        }
    }
}

// One of the crowd: stroll, pick a court with a match on, take a free
// seat (sitting or standing) and watch a while, cheering the points.
void TennisModule::stepCrowd(Walker& w, float dt) {
    kke::RigidWorld& world = m_rigid->world();
    const glm::vec3 feet = world.characterPosition(w.body);
    const int me = static_cast<int>(&w - m_walkers.data());
    glm::vec3 move(0.0f);
    w.timer -= dt;
    auto freeSeat = [&]() {
        if (w.court >= 0 && w.seat >= 0) {
            // From beside a court, back out the way they came in.
            const CourtPlace& c = m_center.courts[static_cast<size_t>(w.court)];
            const glm::vec3 local = c.toLocal(feet);
            if (std::abs(local.x) > kFenceHalfX) {
                w.exit = c.toWorld({ local.x, 0.0f, (c.origin.z < 0.0f ? 1.0f : -1.0f) * (kFenceHalfZ + 1.0f) });
                w.exiting = true;
            }
            auto& taken = m_seatTaken[static_cast<size_t>(w.court)];
            if (w.seat < static_cast<int>(taken.size()) && taken[static_cast<size_t>(w.seat)] == me) taken[static_cast<size_t>(w.seat)] = -1;
        }
        w.seat = -1;
        w.sitting = false;
    };
    if (w.exiting) {
        const glm::vec3 to(w.exit.x - feet.x, 0.0f, w.exit.z - feet.z);
        const float d = glm::length(to);
        if (d < 0.5f) w.exiting = false;
        else move = to / d * kCrowdSpeed;
    }
    if (!w.exiting) switch (w.doing) {
    case Walker::Doing::Wander: {
        if (w.timer <= 0.0f) {
            // Most go and watch a match; some keep strolling.
            std::vector<int> courts;
            for (const auto& m : m_matches) courts.push_back(m->court);
            if (!courts.empty() && roll(w.dice) < 0.8f) {
                const int c = courts[static_cast<size_t>(roll(w.dice) * static_cast<float>(courts.size())) % courts.size()];
                auto& taken = m_seatTaken[static_cast<size_t>(c)];
                const std::vector<SportCenter::Seat> seats = m_center.seats(c);
                std::vector<int> free;
                for (size_t s = 0; s < taken.size(); ++s)
                    if (taken[s] < 0) free.push_back(static_cast<int>(s));
                if (!free.empty()) {
                    w.court = c;
                    w.seat = free[static_cast<size_t>(roll(w.dice) * static_cast<float>(free.size())) % free.size()];
                    taken[static_cast<size_t>(w.seat)] = me;
                    w.doing = Walker::Doing::ToSeat;
                    w.timer = 80.0f; // gives up if it can't get there
                    break;
                }
            }
            const float halfX = m_center.hallMax.x - 3.0f;
            w.goal = { (roll(w.dice) * 2.0f - 1.0f) * halfX, 0.0f, (roll(w.dice) * 2.0f - 1.0f) * 5.5f };
            w.timer = 6.0f + roll(w.dice) * 10.0f;
        }
        const glm::vec3 to(w.goal.x - feet.x, 0.0f, w.goal.z - feet.z);
        if (glm::length(to) > 0.5f) move = glm::normalize(to) * kCrowdSpeed;
        break;
    }
    case Walker::Doing::ToSeat: {
        const std::vector<SportCenter::Seat> seats = m_center.seats(w.court);
        if (w.seat < 0 || w.seat >= static_cast<int>(seats.size()) || w.timer <= 0.0f) {
            freeSeat();
            w.doing = Walker::Doing::Wander;
            w.timer = 0.0f;
            break;
        }
        const SportCenter::Seat& s = seats[static_cast<size_t>(w.seat)];
        // Seats beside a court are in the gap between two fences: go to the
        // gap's mouth on the promenade first, then in.
        const CourtPlace& c = m_center.courts[static_cast<size_t>(w.court)];
        const glm::vec3 local = c.toLocal(s.pos), here = c.toLocal(feet);
        const float end = c.origin.z < 0.0f ? 1.0f : -1.0f;
        glm::vec3 goal = s.pos;
        const bool beside = std::abs(local.x) > kFenceHalfX;
        // In the gap once past its mouth (the waypoint is 1 m out from the fence line).
        const bool inGap = std::abs(here.x) > kFenceHalfX + 0.3f && here.z * end < kFenceHalfZ + 1.6f;
        if (beside && !inGap) goal = c.toWorld({ local.x, 0.0f, end * (kFenceHalfZ + 1.0f) });
        const glm::vec3 to(goal.x - feet.x, 0.0f, goal.z - feet.z);
        const float d = glm::length(to);
        if (goal == s.pos && d < 0.35f) {
            world.teleportCharacter(w.body, s.pos + glm::vec3(0.0f, 0.02f, 0.0f));
            w.doing = Walker::Doing::Watch;
            w.sitting = s.sitting;
            w.timer = 25.0f + roll(w.dice) * 50.0f;
            const float yr = glm::radians(s.yawDegrees);
            w.facing = { std::sin(yr), 0.0f, std::cos(yr) };
        } else if (d > 1e-3f) {
            move = to / d * std::min(kCrowdSpeed, d * 3.0f);
        }
        break;
    }
    case Walker::Doing::Watch:
        if (w.timer <= 0.0f || !matchOn(w.court)) {
            freeSeat();
            w.doing = Walker::Doing::Wander;
            w.timer = roll(w.dice) * 3.0f;
        }
        break;
    }
    if (glm::length(move) > 0.1f) {
        const float k = 1.0f - std::exp(-8.0f * dt);
        w.facing = glm::normalize(w.facing + (glm::normalize(move) - w.facing) * k + glm::vec3(1e-4f, 0.0f, 0.0f));
    }
    kke::RigidWorld::CharacterInput ci;
    ci.move = move;
    world.setCharacterInput(w.body, ci);
}

void TennisModule::onPointForCrowd(const Match& m) {
    if (!m_inCenter) return;
    for (Walker& w : m_walkers) {
        if (!w.cpu || w.court != m.court || w.doing != Walker::Doing::Watch) continue;
        w.cheer = 1.2f + roll(w.dice) * 1.2f;
        w.happy = roll(w.dice) < 0.75f;
    }
}

void TennisModule::updateWalkerBodies(float dt) {
    kke::RigidWorld& world = m_rigid->world();
    for (Walker& w : m_walkers) {
        if (!w.look || w.playing >= 0 || !w.body || w.gone) continue;
        Body::Mood mood = Body::Mood::Stand;
        if (w.sitting) mood = Body::Mood::Sit;
        if (w.cheer > 0.0f) mood = w.happy ? Body::Mood::Cheer : Body::Mood::Groan;
        w.look->update(world.characterPosition(w.body), yawOf(w.facing), world.characterVelocity(w.body), SwingPose{}, mood, dt);
    }
}

// A person out of a match: behind them, a little above, turning slowly
// the way they walk.
void TennisModule::updateWalkerCameras(float dt, std::vector<kke::Camera*>& cams) {
    kke::RigidWorld& world = m_rigid->world();
    for (Walker& w : m_walkers) {
        if (w.cpu || w.remote || w.gone || w.playing >= 0 || !w.body) continue;
        const glm::vec3 feet = world.characterPosition(w.body);
        const float want = yawOf(w.facing);
        float diff = std::remainder(want - w.camYaw, 360.0f);
        const float moving = glm::length(w.stick) > 0.2f ? 1.0f : 0.0f;
        // Walking toward the camera doesn't spin it round.
        if (std::abs(diff) > 150.0f) diff = 0.0f;
        w.camYaw += diff * (1.0f - std::exp(-1.5f * moving * dt));
        const float cy = glm::radians(w.camYaw);
        const glm::vec3 back(-std::sin(cy), 0.0f, -std::cos(cy));
        glm::vec3 pos = feet + back * 6.5f + glm::vec3(0.0f, 3.2f, 0.0f);
        // Never inside a pole or a fence: in front of whatever is between.
        const glm::vec3 head = feet + glm::vec3(0.0f, 1.6f, 0.0f);
        const glm::vec3 ray = pos - head;
        const float len = glm::length(ray);
        const kke::RigidWorld::RayHit hit = world.raycast(head, ray / len, len + 0.3f);
        if (hit.hit) pos = head + ray / len * std::max(0.4f, hit.distance - 0.35f);
        const glm::vec3 look = feet + glm::vec3(0.0f, 1.3f, 0.0f) - back * 3.0f;
        const float k = 1.0f - std::exp(-6.0f * dt);
        w.camera.position = w.cameraInit ? w.camera.position + (pos - w.camera.position) * k : pos;
        w.camera.target = w.cameraInit ? w.camera.target + (look - w.camera.target) * k : look;
        w.cameraInit = true;
        cams.push_back(&w.camera);
    }
}

std::string TennisModule::centerHint() const {
    const Walker* me = nullptr;
    for (const Walker& w : m_walkers)
        if (!w.cpu && !w.remote && !w.gone && w.playing < 0) {
            me = &w;
            break;
        }
    if (!me || !me->body) return {};
    const glm::vec3 feet = m_rigid->world().characterPosition(me->body);
    const int gate = gateNear(feet);
    const std::string court = gate >= 0 ? "court " + std::to_string(gate + 1) : "";
    if (me->queued >= 0) {
        const Gate& g = m_gates[static_cast<size_t>(me->queued)];
        // Online, the host's queue.
        const float countdown = netClient() ? m_board.countdown[static_cast<size_t>(me->queued)] : g.countdown;
        const size_t waiting = netClient() ? m_board.waiting[static_cast<size_t>(me->queued)] : g.waiting.size();
        if (countdown >= 0.0f)
            return "Playing on " + court + " in " + std::to_string(static_cast<int>(std::ceil(countdown))) + " (" + std::to_string(waiting) +
                   " players)  {tennis.slice} leave";
        return "Waiting on " + court + " for an opponent  {tennis.lob} play the CPU now  {tennis.slice} leave";
    }
    if (gate >= 0) {
        bool people = false;
        for (const auto& m : m_matches)
            if (m->court == gate)
                for (int idx : m->players) people = people || m_players[static_cast<size_t>(idx)].walker >= 0;
        if (people) return "Court " + std::to_string(gate + 1) + " is taken: watch from beside it, or find another gate";
        return "{tennis.topspin} play on " + court;
    }
    return "{move} walk to a court's gate (the end by the promenade) to play; the stands are beside each court  {tennis.menu} menu";
}

} // namespace tennis
