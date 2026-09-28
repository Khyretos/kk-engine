#include "Rules.h"

#include "Court.h"

#include <cstdlib>

namespace tennis {

namespace {
const char* const kPointNames[] = { "0", "15", "30", "40" };
const char* const kCallNames[] = { "Love", "15", "30", "40" };
} // namespace

Score::Score(const MatchRules& rules) : m_rules(rules) {}

bool Score::deuceCourt() const { return (m_points[0] + m_points[1]) % 2 == 0; }

bool Score::pointTo(int team) {
    if (over() || team < 0 || team > 1) return false;
    m_switched = false;
    m_lastCall = team;
    const int other = 1 - team;
    ++m_points[team];
    if (m_tiebreak) {
        const int played = m_points[0] + m_points[1];
        if (m_points[team] >= 7 && m_points[team] - m_points[other] >= 2) {
            winGame(team);
            return true;
        }
        // The first point's server serves one, then two each.
        m_server = ((played + 1) / 2) % 2 == 0 ? m_tiebreakFirstServer : 1 - m_tiebreakFirstServer;
        if (m_rules.switchEnds && played % 6 == 0) {
            m_side0 = -m_side0;
            m_switched = true;
        }
        return false;
    }
    const bool won = m_rules.noAd ? m_points[team] >= 4 : (m_points[team] >= 4 && m_points[team] - m_points[other] >= 2);
    if (won) winGame(team);
    return won;
}

void Score::winGame(int team) {
    const int other = 1 - team;
    const bool wasTiebreak = m_tiebreak;
    ++m_games[team];
    m_points[0] = m_points[1] = 0;
    m_tiebreak = false;
    const int total = m_games[0] + m_games[1];
    const int n = m_rules.gamesPerSet;
    bool setOver = false;
    if (wasTiebreak) setOver = true;
    else if (m_games[team] >= n && m_games[team] - m_games[other] >= 2) setOver = true;
    else if (!m_rules.tiebreak && m_games[team] >= n && m_games[team] - m_games[other] >= 2) setOver = true;

    // The serve: after a tiebreak whoever received first in it serves
    // first in the next set; otherwise it goes across after every game.
    if (wasTiebreak) {
        m_server = 1 - m_tiebreakFirstServer;
        if (m_rules.teamSize > 1) m_serverPlayer[m_server] = 1 - m_serverPlayer[m_server];
    } else {
        nextServer();
    }
    if (m_rules.switchEnds && total % 2 == 1) {
        m_side0 = -m_side0;
        m_switched = true;
    }
    if (setOver) {
        winSet(team);
        return;
    }
    if (m_rules.tiebreak && m_games[0] == n && m_games[1] == n) {
        m_tiebreak = true;
        m_tiebreakFirstServer = m_server;
    }
}

void Score::nextServer() {
    m_server = 1 - m_server;
    // Doubles: a team's two players take its service games in turn.
    if (m_rules.teamSize > 1 && m_games[0] + m_games[1] >= 2) m_serverPlayer[m_server] = 1 - m_serverPlayer[m_server];
}

void Score::winSet(int team) {
    if (m_setsPlayed < kMaxSets) {
        m_history[m_setsPlayed][0] = m_games[0];
        m_history[m_setsPlayed][1] = m_games[1];
    }
    ++m_setsPlayed;
    ++m_sets[team];
    m_games[0] = m_games[1] = 0;
    if (m_sets[team] >= m_rules.setsToWin) m_winner = team;
}

std::string Score::pointText(int team) const {
    if (m_tiebreak) return std::to_string(m_points[team]);
    const int a = m_points[team], b = m_points[1 - team];
    if (a >= 3 && b >= 3) return a > b ? "AD" : (a == b ? "40" : "");
    return kPointNames[a < 4 ? a : 3];
}

std::string Score::callText() const {
    const int s = m_points[m_server], r = m_points[1 - m_server];
    if (m_tiebreak) return "Tiebreak " + std::to_string(s) + "-" + std::to_string(r);
    if (s + r == 0) return "Love all";
    if (s >= 3 && r >= 3) {
        if (s == r) return m_rules.noAd ? "Deciding point" : "Deuce";
        return s > r ? "Advantage server" : "Advantage receiver";
    }
    if (s == r) return std::string(kCallNames[s]) + "-all";
    return std::string(kCallNames[s < 4 ? s : 3]) + "-" + kCallNames[r < 4 ? r : 3];
}

std::string Score::setsText(int team) const {
    std::string out;
    for (int i = 0; i < m_setsPlayed && i < kMaxSets; ++i) {
        if (!out.empty()) out += " ";
        out += std::to_string(m_history[i][team]);
    }
    if (!over()) {
        if (!out.empty()) out += " ";
        out += std::to_string(m_games[team]);
    }
    return out;
}

// --- Rally

void Rally::newPoint(int server, int serverSide, bool deuceCourt, bool doubles) {
    m_server = server;
    m_serverSide = serverSide;
    m_deuce = deuceCourt;
    m_doubles = doubles;
    m_faults = 0;
    serveAgain();
    m_call.clear();
}

void Rally::serveAgain() {
    m_phase = Phase::Serve;
    m_lastHitter = -1;
    m_bounces = 0;
    m_bounceSide = 0;
    m_netTouched = false;
}

bool Rally::mayHit(int team) const {
    switch (m_phase) {
    case Phase::Serve: return team == m_server;
    case Phase::ServeFlight: return false; // the return waits for the bounce
    case Phase::Rally: return team != m_lastHitter && m_bounces <= 1 && (m_bounces == 0 || m_bounceSide == sideOfTeam(team));
    case Phase::Over: return false;
    }
    return false;
}

Rally::Result Rally::pointTo(int team, const char* call) {
    m_phase = Phase::Over;
    m_call = call;
    return team == 0 ? Result::PointTo0 : Result::PointTo1;
}

Rally::Result Rally::onHit(int team) {
    switch (m_phase) {
    case Phase::Serve:
        if (team != m_server) return Result::None;
        m_phase = Phase::ServeFlight;
        break;
    case Phase::ServeFlight:
        // Hitting the serve before it bounces loses the point.
        if (team != m_server) return pointTo(m_server, "Hit before the bounce");
        return Result::None;
    case Phase::Rally:
        if (team == m_lastHitter) return pointTo(1 - team, "Hit twice");
        if (m_bounces >= 2) return Result::None; // already decided
        break;
    case Phase::Over: return Result::None;
    }
    m_lastHitter = team;
    m_bounces = 0;
    m_netTouched = false;
    return Result::None;
}

Rally::Result Rally::onBounce(float x, float z) {
    const int side = sideOf(z);
    switch (m_phase) {
    case Phase::Serve:
    case Phase::Over: return Result::None;
    case Phase::ServeFlight:
        if (inServiceBox(x, z, m_serverSide, m_deuce)) {
            if (m_netTouched) {
                m_call = "Let";
                m_phase = Phase::Over;
                return Result::Let;
            }
            m_phase = Phase::Rally;
            m_bounces = 1;
            m_bounceSide = side;
            return Result::None;
        }
        return fault(m_netTouched && side == m_serverSide ? "Net" : "Fault");
    case Phase::Rally: break;
    }
    ++m_bounces;
    m_bounceSide = side;
    if (m_bounces == 1) {
        const int target = -sideOfTeam(m_lastHitter);
        if (side != target) return pointTo(1 - m_lastHitter, m_netTouched ? "Net" : "Not over");
        if (!inCourt(x, z, side, m_doubles)) return pointTo(1 - m_lastHitter, "Out");
        return Result::None;
    }
    return pointTo(m_lastHitter, "Winner");
}

void Rally::onNet() { m_netTouched = true; }

Rally::Result Rally::fault(const char* call) {
    if (++m_faults >= 2) return pointTo(1 - m_server, "Double fault");
    m_phase = Phase::Over;
    m_call = call;
    return Result::Fault;
}

Rally::Result Rally::onOut() {
    switch (m_phase) {
    case Phase::ServeFlight: return fault("Fault");
    case Phase::Rally:
        if (m_bounces == 0) return pointTo(1 - m_lastHitter, "Out");
        return pointTo(m_lastHitter, "Winner");
    default: return Result::None;
    }
}

} // namespace tennis
