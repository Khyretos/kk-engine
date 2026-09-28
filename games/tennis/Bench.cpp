// The busiest case (KKE_TENNIS_BENCH=singles or doubles): a showcase and a
// benchmark. A hundred people in the sport center: ten CPU matches, one on
// every court (twenty players in singles, forty in doubles), six or five
// people watching each match from the benches, and the rest walking the
// promenade. A camera flies from court to court and watches each match a
// few seconds. The same seed every run, so every run plays the same.
//
// What it loads: FEMFX with ten balls and a string bed per racket (twenty
// or forty), Jolt with a hundred characters, a hundred procedural bodies
// (swings, walking, sitting, cheering) and the CPU players' thinking.
// kke_benchmark runs both (benchmarks/suite.yaml).

#include "TennisModule.h"

#include "kke/DevTools.h"
#include "kke/Log.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <array>
#include <cmath>
#include <cstring>

namespace tennis {

namespace {
constexpr int kPeople = 100;
constexpr float kShot = 4.5f; // s the camera watches each court
// The courts in the order the camera visits them: along one row, back along the other.
constexpr std::array<int, SportCenter::kCourts> kTour{ 0, 1, 2, 3, 4, 9, 8, 7, 6, 5 };
} // namespace

void TennisModule::setupBench() {
    const char* b = kke::dev::env("KKE_TENNIS_BENCH");
    if (!b || !*b) return;
    m_bench = std::strcmp(b, "doubles") == 0 ? 2 : 1;
    if (std::strcmp(b, "singles") != 0 && m_bench == 1)
        kke::log::get(name())->warn("KKE_TENNIS_BENCH={}: not singles or doubles, playing singles", b);
    // Straight into the sport center, nobody at this screen, every court played.
    if (m_lobby) m_lobby->close();
    m_inMenu = false;
    m_allBots = true;
    m_where = 1;
    m_cpuMatches = true;
    m_crowd = kPeople - SportCenter::kCourts * (m_bench == 2 ? 4 : 2);
}

// Each court's spectators straight onto its benches, spread along both
// sides, watching until the run ends. Everyone else strolls.
void TennisModule::seatBenchCrowd() {
    const int perCourt = m_bench == 2 ? 5 : 6;
    kke::RigidWorld& world = m_rigid->world();
    int seated = 0;
    for (size_t wi = 0; wi < m_walkers.size() && seated < perCourt * SportCenter::kCourts; ++wi) {
        Walker& w = m_walkers[wi];
        if (!w.cpu) continue;
        const int court = seated / perCourt, j = seated % perCourt;
        const std::vector<SportCenter::Seat> seats = m_center.seats(court);
        // The benches are every other seat (a bench, then a standing place
        // behind it), nine a side: spread the spectators over all eighteen.
        const int bench = 2 * (j * 18 / perCourt + 1);
        const SportCenter::Seat& s = seats[static_cast<size_t>(bench)];
        m_seatTaken[static_cast<size_t>(court)][static_cast<size_t>(bench)] = static_cast<int>(wi);
        world.teleportCharacter(w.body, s.pos + glm::vec3(0.0f, 0.02f, 0.0f));
        w.court = court;
        w.seat = bench;
        w.doing = Walker::Doing::Watch;
        w.sitting = s.sitting;
        w.timer = 1e9f;
        const float yr = glm::radians(s.yawDegrees);
        w.facing = { std::sin(yr), 0.0f, std::cos(yr) };
        ++seated;
    }
    int players = 0;
    for (const auto& m : m_matches) players += static_cast<int>(m->players.size());
    kke::log::get(name())->info("benchmark ({}): {} matches, {} players, {} watching, {} walking; FEMFX: {} balls and {} string beds",
                                m_bench == 2 ? "doubles" : "singles", m_matches.size(), players, seated,
                                static_cast<int>(m_walkers.size()) - seated, m_matches.size(), players);
}

// The flying camera: high over each court's promenade end in turn,
// gliding across it while it watches the ball (both players, the
// spectators on both sides), then on along the promenade to the next.
void TennisModule::benchCamera(float dt, kke::Camera& cam) {
    m_benchClock += dt;
    const int leg = static_cast<int>(m_benchClock / kShot);
    const float u = m_benchClock / kShot - static_cast<float>(leg);
    const int court = kTour[static_cast<size_t>(leg) % kTour.size()];
    const CourtPlace& place = m_center.courts[static_cast<size_t>(court)];
    const float end = place.origin.z < 0.0f ? 1.0f : -1.0f; // the promenade is toward z = 0
    // Across the court the way the tour goes (along the first row, back along the second).
    const float way = (place.origin.z < 0.0f ? 1.0f : -1.0f) * (u * 2.0f - 1.0f);
    const glm::vec3 want = place.toWorld({ way * 7.0f, 10.0f, end * (kFenceHalfZ + 6.5f) });
    glm::vec3 ball(0.0f, 1.0f, 0.0f);
    if (const Match* m = matchOn(court); m && m->ball) ball = m->ball->position();
    const glm::vec3 look = place.toWorld({ ball.x * 0.3f, 0.0f, ball.z * 0.3f + end * 1.0f });
    if (!m_broadcastInit) {
        cam.position = want;
        cam.target = look;
        m_broadcastInit = true;
        return;
    }
    cam.position += (want - cam.position) * (1.0f - std::exp(-1.4f * dt));
    cam.target += (look - cam.target) * (1.0f - std::exp(-2.5f * dt));
}

} // namespace tennis
