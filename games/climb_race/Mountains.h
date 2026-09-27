#pragma once

#include "kke/ClimbWall.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace climb_race {

// A mountain of the tour (games/climb_race/mountains/*.yaml, JSON works
// too): its name, a line about it, the knobs of the rock generator
// (kke::ClimbWallDesc), its mood and its medal times. Copy one, change a
// few numbers, and it's a new mountain in the start menu. The keys:
//
//   name: Pebble Hill        what the menu calls it
//   about: ...               one line under the name
//   order: 1                 where it sits in the tour (1 first)
//   mood: morning            the sky and light (assets/moods/)
//   seed: 3                  which rock: same seed, same mountain
//   height: 12               m to the summit
//   ledges: 1                shelves to stand and rest on
//   overhang: 4              steepest lean past vertical, degrees
//   slab: 26                 most it leans back, degrees
//   holds: 1.3               holds per square metre
//   jugs: 0.5                more big green holds (0 = the usual mix)
//   crimps: -0.2             more thin orange edges (negative: fewer)
//   loose: 0.05              share of the holds that break off
//   step: 1.0                longest step on the always-climbable line, m
//   medals: [22, 30, 45]     gold, silver, bronze: seconds to the top
struct Mountain {
    std::string id;             // the file's name ("pebble_hill"); "random" for Random
    std::string name, about, mood;
    int order = 100;
    kke::ClimbWallDesc desc;
    float medals[3] = {};       // gold, silver, bronze (s; 0 = no medal)
};

// Reads one mountain. Unknown keys and odd values are reported in
// `problems` (the mountain still loads, with the value clamped or left
// out); false only when it's not a mountain at all.
bool mountainFromJson(const nlohmann::json& j, Mountain& out, std::vector<std::string>& problems);
// Every mountain in `folder`, in tour order. Problems come back as "file: what".
std::vector<Mountain> loadMountains(const std::filesystem::path& folder, std::vector<std::string>& problems);
// Random: the generator's own defaults (the rock the game always had), on `seed`.
Mountain randomMountain(uint32_t seed);

// 0 gold, 1 silver, 2 bronze, -1 none: the medal a time earns.
int medalFor(const Mountain& m, float seconds);
const char* medalName(int medal);

} // namespace climb_race
