#include "Mountains.h"

#include "kke/DataFile.h"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace climb_race {

namespace {

// A number between lo and hi; out of range is clamped and reported.
bool number(const nlohmann::json& j, const char* key, float lo, float hi, float& out, std::vector<std::string>& problems) {
    const auto it = j.find(key);
    if (it == j.end()) return false;
    if (!it->is_number()) {
        problems.push_back(std::string(key) + " should be a number");
        return false;
    }
    const float v = it->get<float>();
    out = std::clamp(v, lo, hi);
    if (out != v) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s %g is out of range (%g to %g): using %g", key, static_cast<double>(v), static_cast<double>(lo),
                      static_cast<double>(hi), static_cast<double>(out));
        problems.push_back(buf);
    }
    return true;
}

} // namespace

bool mountainFromJson(const nlohmann::json& j, Mountain& out, std::vector<std::string>& problems) {
    if (!j.is_object()) {
        problems.push_back("not a mountain (expected keys like name: and height:)");
        return false;
    }
    static const char* const kKeys[] = { "name", "about", "order", "mood", "seed", "height", "ledges", "overhang", "slab",
                                         "holds", "jugs", "crimps", "loose", "step", "medals" };
    for (const auto& [key, value] : j.items()) {
        (void)value;
        if (std::find_if(std::begin(kKeys), std::end(kKeys), [&key](const char* k) { return key == k; }) == std::end(kKeys))
            problems.push_back("unknown key \"" + key + "\" (ignored)");
    }
    out.name = kke::datafile::text(j, "name", out.name);
    out.about = kke::datafile::text(j, "about", out.about);
    out.mood = kke::datafile::text(j, "mood", out.mood);
    float v = 0.0f;
    if (number(j, "order", -1000.0f, 1000.0f, v, problems)) out.order = static_cast<int>(v);
    kke::ClimbWallDesc& d = out.desc;
    if (number(j, "seed", 0.0f, 1.0e9f, v, problems)) d.seed = static_cast<uint32_t>(v);
    number(j, "height", 6.0f, 80.0f, d.height, problems);
    if (number(j, "ledges", 0.0f, 8.0f, v, problems)) d.ledges = static_cast<int>(v);
    number(j, "overhang", 0.0f, 35.0f, d.maxOverhang, problems);
    number(j, "slab", 0.0f, 35.0f, d.maxSlab, problems);
    number(j, "holds", 0.4f, 3.0f, d.density, problems);
    number(j, "jugs", -0.5f, 1.5f, d.jugBias, problems);
    number(j, "crimps", -0.5f, 1.5f, d.crimpBias, problems);
    number(j, "loose", 0.0f, 0.6f, d.looseChance, problems);
    // Longer than an arm span and the line isn't climbable any more.
    number(j, "step", 0.6f, 1.3f, d.routeStep, problems);
    if (const auto it = j.find("medals"); it != j.end()) {
        if (it->is_array() && it->size() == 3 && std::all_of(it->begin(), it->end(), [](const nlohmann::json& m) { return m.is_number(); })) {
            for (int i = 0; i < 3; ++i) out.medals[i] = std::max(0.0f, (*it)[static_cast<size_t>(i)].get<float>());
            if (!(out.medals[0] <= out.medals[1] && out.medals[1] <= out.medals[2]))
                problems.push_back("medals should go gold, silver, bronze (fastest first)");
        } else {
            problems.push_back("medals should be three times in seconds: [gold, silver, bronze]");
        }
    }
    if (out.name.empty()) out.name = out.id;
    return true;
}

std::vector<Mountain> loadMountains(const std::filesystem::path& folder, std::vector<std::string>& problems) {
    std::vector<Mountain> out;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) {
        problems.push_back(folder.string() + ": no mountains folder");
        return out;
    }
    // One mountain per name: pebble_hill.yaml and pebble_hill.json are the
    // same mountain (the newest wins, kke::datafile says so).
    std::vector<std::string> stems;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || !kke::datafile::formatOf(entry.path())) continue;
        const std::string stem = entry.path().stem().string();
        if (std::find(stems.begin(), stems.end(), stem) == stems.end()) stems.push_back(stem);
    }
    for (const std::string& stem : stems) {
        kke::datafile::Loaded loaded;
        std::string error;
        if (!kke::datafile::load(folder, stem, loaded, &error)) {
            problems.push_back(stem + ": " + error);
            continue;
        }
        Mountain m;
        m.id = stem;
        std::vector<std::string> own;
        const bool ok = mountainFromJson(loaded.data, m, own);
        for (const std::string& p : own) problems.push_back(loaded.file.filename().string() + ": " + p);
        if (ok) out.push_back(std::move(m));
    }
    std::sort(out.begin(), out.end(), [](const Mountain& a, const Mountain& b) { return a.order != b.order ? a.order < b.order : a.id < b.id; });
    return out;
}

Mountain randomMountain(uint32_t seed) {
    Mountain m;
    m.id = "random";
    m.name = "Random";
    m.about = "A new mountain every time.";
    m.mood = "golden_hour";
    m.desc.seed = seed;
    return m;
}

int medalFor(const Mountain& m, float seconds) {
    for (int i = 0; i < 3; ++i)
        if (m.medals[i] > 0.0f && seconds <= m.medals[i]) return i;
    return -1;
}

const char* medalName(int medal) {
    switch (medal) {
    case 0: return "gold";
    case 1: return "silver";
    case 2: return "bronze";
    default: return "";
    }
}

} // namespace climb_race
