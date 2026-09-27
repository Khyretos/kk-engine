#pragma once

#include "Mountains.h"

#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

namespace climb_race {

// The tour (DESIGN.md "Progression"): the best time and medal on every
// mountain, and which mountains are open. The first mountain is always
// open; finishing one (any time) opens the next. Saved next to the game
// (climb_race_progress.json, or .yml) by ClimbRaceModule. Random has no
// records: its mountains don't come back.
class Progress {
public:
    struct Record {
        float best = 0.0f;   // s (0 = never finished)
        int medal = -1;      // 0 gold, 1 silver, 2 bronze, -1 none
        int finishes = 0;
    };
    // What one finish did.
    struct Result {
        bool newBest = false;
        int medal = -1;          // this time's medal
        bool betterMedal = false;
        std::string opened;      // the id of a mountain this opened ("" = none)
    };

    const Record* record(const std::string& id) const;
    // A player reached the top of `m` in `seconds`. `tour` is every
    // mountain in order (to know which one comes next).
    Result finish(const std::vector<Mountain>& tour, const Mountain& m, float seconds);
    bool isOpen(const std::vector<Mountain>& tour, size_t index) const;
    // Every mountain open (KKE_CLIMB_ALL=1, testers): not saved.
    bool openAll = false;

    nlohmann::json save() const;
    void load(const nlohmann::json& j);

private:
    std::map<std::string, Record> m_records;
};

} // namespace climb_race
