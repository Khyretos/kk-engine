#pragma once

#include "kke/Sky.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace kke {

struct Lighting;

// A mood: the sky, the sun, the fill light, fog, the colour look and
// exposure of a scene, in one small data file (JSON or YAML, kke::datafile).
// The engine ships a set in assets/moods/ (clear_day, golden_hour,
// sunset, dusk, night, overcast, misty_morning, stormy, ...), each with a
// CC0 sky from Poly Haven (assets/skies/); a game can add its own in a
// moods/ folder next to it. docs/MOODS.md has the whole format.
//
//   sky: { image: qwantani_late_afternoon }   # or zenith/horizon/ground colours
//   sun: { azimuth: 215, intensity: 3.2 }      # elevation and colour from the sky's own sun
//   fill: { color: "#8fa6d8", intensity: 0.3 }
//   fog: { density: 0.004, heightFalloff: 0.05 }
//   look: golden                               # or { slope, offset, power, saturation }
//   exposure: 1.1
//
// Colours are "#rrggbb" (sRGB, as a colour picker gives them) or
// [r, g, b] numbers (linear light, may go above 1).
struct Mood {
    std::string name;
    std::string title;
    std::string description;
    std::filesystem::path file; // where it was read from

    Sky sky;
    // The key light (Lighting::lights[0]). With an image sky that has a
    // sun, elevation (and colour, unless given) come from the picture and
    // the sky turns so its sun sits at `azimuthDegrees`.
    float sunAzimuthDegrees = 215.0f; // around from -z (north) towards +x (east)
    std::optional<float> sunElevationDegrees;
    std::optional<glm::vec3> sunColor;
    float sunIntensity = 2.5f;
    // A soft light from the opposite side (lights[1]); intensity 0 = off.
    glm::vec3 fillColor{0.55f, 0.65f, 0.85f};
    float fillIntensity = 0.0f;
    Fog fog;
    ColorGrade grade;
    float exposure = 1.0f;
    std::string ambience; // a looping sound's name (assets/ambience); empty = none

    // Puts the mood into `lighting`: sky, lights[0] and [1], fog, grade,
    // exposure. Other lights, shadows and the tone curve are left alone.
    void applyTo(Lighting& lighting) const;
};

// Direction towards the sun (y up) from azimuth/elevation in degrees, and back.
glm::vec3 sunDirectionFrom(float azimuthDegrees, float elevationDegrees);
float azimuthOf(const glm::vec3& towardsSun);

// Reads a mood from parsed data. `baseDir` resolves relative sky image
// paths. False with `error` for a bad value (the message names the field).
bool parseMood(const nlohmann::json& data, const std::filesystem::path& baseDir, Mood& out, std::string* error = nullptr);

// Finds a mood by name ("golden_hour") or path ("moods/my_mood.yaml"):
// a path as given, then moods/<name> next to the running game, then
// assets/moods/<name>. Nothing with `error` when it isn't there or
// doesn't parse.
std::optional<Mood> loadMood(const std::string& nameOrPath, std::string* error = nullptr);

// The names loadMood() can find, sorted.
std::vector<std::string> listMoods();

} // namespace kke
