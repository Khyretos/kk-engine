#pragma once

#include <string>

// Tennis rules (pure logic, unit-tested in tests/test_tennis.cpp): the
// score of a match, and the umpire who watches one rally and says who won
// the point. Two teams, 0 and 1, of one or two players each.
namespace tennis {

struct MatchRules {
    int teamSize = 1;         // 1 singles, 2 doubles
    int gamesPerSet = 6;      // first to this many games, two clear (or a tiebreak)
    int setsToWin = 1;        // 1 = a one-set match, 2 = best of three
    bool tiebreak = true;     // at gamesPerSet all: a tiebreak to 7, two clear
    bool noAd = false;        // at deuce the next point wins the game
    bool switchEnds = true;   // after every odd game, and every 6 points of a tiebreak
};

// The score and whose serve it is.
class Score {
public:
    explicit Score(const MatchRules& rules = {});
    const MatchRules& rules() const { return m_rules; }

    // A point to `team`. Returns true when that point finished a game.
    bool pointTo(int team);

    int points(int team) const { return m_points[team]; }       // in this game (or tiebreak)
    int games(int team) const { return m_games[team]; }         // in this set
    int sets(int team) const { return m_sets[team]; }
    int setGames(int set, int team) const { return set < kMaxSets ? m_history[set][team] : 0; }
    int setsPlayed() const { return m_setsPlayed; }
    bool inTiebreak() const { return m_tiebreak; }
    bool over() const { return m_winner >= 0; }
    int winner() const { return m_winner; }

    int servingTeam() const { return m_server; }
    // Which player of the serving team serves (doubles: they take turns
    // across the team's service games).
    int servingPlayer() const { return m_serverPlayer[m_server]; }
    // The first point of every game (and every even point of a tiebreak)
    // is served from the right half, the deuce court.
    bool deuceCourt() const;
    // Which half team plays on: +1 or -1 (they swap when ends switch).
    int sideOf(int team) const { return team == 0 ? m_side0 : -m_side0; }
    // Ends switched with the last point: the game moves everyone.
    bool endsJustSwitched() const { return m_switched; }

    // "15-30", "Deuce", "Advantage", "Game"... from the server's point of view.
    std::string callText() const;
    // The whole score for a scoreboard line: "6-4 3-2" (sets so far, then this set).
    std::string setsText(int team) const;
    // The game's points for a scoreboard cell: "0", "15", "30", "40", "AD", or a tiebreak count.
    std::string pointText(int team) const;

    static constexpr int kMaxSets = 5;

private:
    void winGame(int team);
    void winSet(int team);
    void nextServer();

    MatchRules m_rules;
    int m_points[2] = { 0, 0 }, m_games[2] = { 0, 0 }, m_sets[2] = { 0, 0 };
    int m_history[kMaxSets][2] = {};
    int m_setsPlayed = 0;
    bool m_tiebreak = false;
    int m_tiebreakFirstServer = 0;
    int m_winner = -1;
    int m_server = 0;
    int m_serverPlayer[2] = { 0, 0 };
    int m_side0 = 1;
    bool m_switched = false;
    int m_lastCall = -1; // who won the last point, for "Game"
};

// One rally: the umpire's view. Tell it what the ball does; it answers
// when the point is over and who won it.
class Rally {
public:
    enum class Result { None, PointTo0, PointTo1, Fault, Let };

    // A new point: `server` (the team) serves from their half
    // (`serverSide`), into the box for `deuceCourt`. Fault count kept
    // across calls for the second serve; newPoint() clears it.
    void newPoint(int server, int serverSide, bool deuceCourt, bool doubles);
    void serveAgain(); // after a fault (the second serve) or a let (the same serve again)
    bool secondServeNow() const { return m_faults == 1; }
    void setSecondServe(bool second) { m_faults = second ? 1 : 0; } // online: the umpire's word

    // May `team` hit the ball now? (It came over and hasn't bounced twice;
    // the serve's return waits for its bounce; nobody hits it twice.)
    bool mayHit(int team) const;
    Result onHit(int team);
    // The ball touched the court at (x, z).
    Result onBounce(float x, float z);
    // The ball touched the net (tape or mesh).
    void onNet();
    // The ball hit the fence or the roof (anything not the court).
    Result onOut();

    bool serving() const { return m_phase == Phase::Serve || m_phase == Phase::ServeFlight; }
    bool live() const { return m_phase != Phase::Over; }
    int lastHitter() const { return m_lastHitter; }
    int servingTeam() const { return m_server; }
    // What the umpire says about the last call: "Out", "Fault", "Double fault", "Let", "Net", "Not up"...
    const std::string& call() const { return m_call; }
    // The ball has bounced once since the last hit (on the half it's on).
    bool bouncedOnce() const { return (m_phase == Phase::Rally || m_phase == Phase::ServeFlight) && m_bounces == 1; }

private:
    enum class Phase { Serve, ServeFlight, Rally, Over };
    Result pointTo(int team, const char* call);
    Result fault(const char* call);
    int sideOfTeam(int team) const { return team == m_server ? m_serverSide : -m_serverSide; }

    Phase m_phase = Phase::Over;
    int m_server = 0, m_serverSide = 1;
    bool m_deuce = true, m_doubles = false;
    int m_faults = 0;
    int m_lastHitter = -1;
    int m_bounces = 0;       // since the last hit
    int m_bounceSide = 0;    // the half of the last bounce
    bool m_netTouched = false;
    std::string m_call;
};

} // namespace tennis
