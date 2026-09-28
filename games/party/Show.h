#pragma once

// The show (README.md "The show"): a party is a few rounds, each one a
// minigame picked from the playlist, and points for every round's places.
// Pure logic (no engine, no GPU), unit-tested in tests/test_party.cpp.

#include <cstdint>
#include <string>
#include <vector>

namespace party {

// How one bean did in one round. The minigame fills these in; place()
// turns them into an order.
struct RoundResult {
    bool finished = false;    // reached the goal (races): by finishOrder
    int finishOrder = -1;     // 0 = first over the line
    bool out = false;         // knocked out (survival games): by outOrder, the last out ranks best
    int outOrder = -1;        // 0 = the first one out
    float score = 0.0f;       // everyone else (still standing, or score games): higher is better
    int team = -1;            // team games: the team (-1: none)
};

// Places for a round: 0 = best. Finishers first (in finishing order),
// then those still standing (by score, higher first; equal scores share a
// place), then those knocked out (the last one out first). Beans that tie
// get the same place, so two survivors of a jump rope game share first.
std::vector<int> placesOf(const std::vector<RoundResult>& results);

// Points for a place, with `count` beans in the round: 10, 8, 6, 5, 4,
// 3, 2, 1 for the first eight, then 0 (a round with fewer beans gives the
// same points for the same place).
int pointsFor(int place, int count);

// The playlist: every minigame's id, in the order they're played. A new
// party shuffles them (seeded, so every machine online gets the same
// order) and plays the first `rounds`.
struct Show {
    std::vector<std::string> pool;       // every game this party can play
    std::vector<std::string> playlist;
    int rounds = 5;
    int round = 0;                   // the round being played (0-based)
    std::vector<int> points;         // per bean (the party's roster order)

    // Start a party: shuffles `all` with `seed` (or keeps the order when
    // `shuffle` is false) and zeroes the points of `beans` beans.
    void start(const std::vector<std::string>& all, int roundCount, int beans, uint32_t seed, bool shuffle = true);
    const std::string& current() const;
    bool lastRound() const { return round + 1 >= rounds; }
    bool over() const { return round >= rounds; }
    // A round's results: adds everyone's points and returns what each got.
    std::vector<int> score(const std::vector<RoundResult>& results);
    void next() { ++round; }
    // Picking by vote (README.md "Modes"): up to `count` games for the
    // players to choose from before this round, the ones not played yet
    // first (never the one just played, while there are others), in an
    // order from `seed`. choose() makes the winner this round's game.
    std::vector<std::string> candidates(int count, uint32_t seed) const;
    void choose(const std::string& game);
    // Overall standings: bean indices, most points first (ties keep the
    // roster order), and each one's place (equal points share it).
    std::vector<int> standings() const;
    std::vector<int> standingPlaces() const;
};

// The winner of a vote: the choice with the most `votes` (each one a
// choice 0..choices-1, or -1 for none), a tie (or no votes) settled by
// `seed`.
int tally(const std::vector<int>& votes, int choices, uint32_t seed);

// A small, repeatable random number generator (the same on every
// machine and compiler, unlike std::uniform_*_distribution).
struct Rng {
    uint32_t state = 1;
    explicit Rng(uint32_t seed = 1) : state(seed ? seed : 0x9e3779b9u) {}
    uint32_t next() {
        // xorshift32
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    float unit() { return static_cast<float>(next() >> 8) / 16777216.0f; } // [0, 1)
    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
    int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

} // namespace party
