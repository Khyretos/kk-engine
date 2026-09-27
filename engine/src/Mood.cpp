#include "kke/Mood.h"

#include "kke/Application.h"
#include "kke/DataFile.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <set>
#include <sstream>
#include <system_error>

namespace kke {

namespace fs = std::filesystem;

namespace {

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

// "#rrggbb" (sRGB) or [r, g, b] (linear).
bool readColor(const nlohmann::json& v, const std::string& field, glm::vec3& out, std::string* error) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s.size() == 7 && s[0] == '#' && std::all_of(s.begin() + 1, s.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; })) {
            for (int i = 0; i < 3; ++i) out[i] = srgbToLinear(static_cast<float>(std::stoi(s.substr(1 + i * 2, 2), nullptr, 16)) / 255.0f);
            return true;
        }
    } else if (v.is_array() && v.size() == 3 && std::all_of(v.begin(), v.end(), [](const nlohmann::json& x) { return x.is_number(); })) {
        out = glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>());
        if (out.x >= 0.0f && out.y >= 0.0f && out.z >= 0.0f) return true;
    }
    if (error) *error = field + ": a colour is \"#rrggbb\" or [r, g, b] (0 or more)";
    return false;
}

bool readNumber(const nlohmann::json& obj, const char* key, const std::string& prefix, float& out, std::string* error,
                float lo = -1e9f, float hi = 1e9f) {
    auto it = obj.find(key);
    if (it == obj.end()) return true;
    if (!it->is_number() || it->get<float>() < lo || it->get<float>() > hi) {
        if (error) {
            std::ostringstream msg;
            msg << prefix << key << ": a number";
            if (lo > -1e9f) msg << " from " << lo << " to " << hi;
            *error = msg.str();
        }
        return false;
    }
    out = it->get<float>();
    return true;
}

bool readVec3Or1(const nlohmann::json& obj, const char* key, const std::string& prefix, glm::vec3& out, std::string* error) {
    auto it = obj.find(key);
    if (it == obj.end()) return true;
    if (it->is_number()) {
        out = glm::vec3(it->get<float>());
        return true;
    }
    if (it->is_array() && it->size() == 3 && std::all_of(it->begin(), it->end(), [](const nlohmann::json& x) { return x.is_number(); })) {
        out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>());
        return true;
    }
    if (error) *error = prefix + key + ": a number, or [r, g, b]";
    return false;
}

// Where a sky image name points: the name as a file next to the mood,
// then in a skies/ folder beside the mood's folder, then assets/skies/.
std::string resolveSkyImage(const std::string& name, const fs::path& baseDir) {
    std::vector<fs::path> candidates;
    const bool hasExt = fs::path(name).has_extension();
    const std::string file = hasExt ? name : name + ".hdr";
    candidates.push_back(baseDir / file);
    candidates.push_back(baseDir.parent_path() / "skies" / file);
    candidates.push_back(fs::path("assets") / "skies" / file);
    candidates.push_back(fs::path(file));
    std::error_code ec;
    for (const fs::path& c : candidates)
        if (fs::is_regular_file(c, ec)) return c.generic_string();
    return (fs::path("assets") / "skies" / file).generic_string(); // SkyResolver reports it missing
}

// Where moods are looked up by name: the game's own, then the engine's.
const std::vector<fs::path>& moodFolders() {
    static const std::vector<fs::path> dirs{ fs::path("moods"), fs::path("assets") / "moods" };
    return dirs;
}

const std::set<std::string>& knownKeys() {
    static const std::set<std::string> k{ "title", "description", "sky", "sun", "fill", "fog", "look", "exposure", "ambience" };
    return k;
}

} // namespace

glm::vec3 sunDirectionFrom(float azimuthDegrees, float elevationDegrees) {
    const float az = glm::radians(azimuthDegrees), el = glm::radians(elevationDegrees);
    return { std::cos(el) * std::sin(az), std::sin(el), -std::cos(el) * std::cos(az) };
}

float azimuthOf(const glm::vec3& d) {
    float a = glm::degrees(std::atan2(d.x, -d.z));
    return a < 0.0f ? a + 360.0f : a;
}

bool parseMood(const nlohmann::json& data, const fs::path& baseDir, Mood& out, std::string* error) {
    if (!data.is_object()) {
        if (error) *error = "a mood is a set of named values (sky, sun, fog, look, ...)";
        return false;
    }
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (!knownKeys().count(it.key())) {
            if (error) *error = "unknown field '" + it.key() + "' (known: title, description, sky, sun, fill, fog, look, exposure, ambience)";
            return false;
        }
    }
    Mood m;
    m.name = out.name;
    m.file = out.file;
    if (auto t = data.find("title"); t != data.end() && t->is_string()) m.title = t->get<std::string>();
    if (auto d = data.find("description"); d != data.end() && d->is_string()) m.description = d->get<std::string>();

    if (auto s = data.find("sky"); s != data.end()) {
        if (!s->is_object()) {
            if (error) *error = "sky: a set of values (image, or zenith/horizon/ground)";
            return false;
        }
        Sky& sky = m.sky;
        sky.kind = Sky::Kind::Gradient;
        if (auto img = s->find("image"); img != s->end()) {
            if (!img->is_string() || img->get<std::string>().empty()) {
                if (error) *error = "sky.image: the name of a sky in assets/skies, or a path to a .hdr file";
                return false;
            }
            sky.kind = Sky::Kind::Image;
            sky.image = resolveSkyImage(img->get<std::string>(), baseDir);
        }
        for (const char* key : { "zenith", "horizon", "ground", "sunColor" }) {
            if (auto c = s->find(key); c != s->end()) {
                glm::vec3& dst = std::string(key) == "zenith" ? sky.zenith : std::string(key) == "horizon" ? sky.horizon : std::string(key) == "ground" ? sky.ground : sky.sunColor;
                if (!readColor(*c, std::string("sky.") + key, dst, error)) return false;
            }
        }
        if (!readNumber(*s, "horizonFalloff", "sky.", sky.horizonFalloff, error, 0.1f, 20.0f)) return false;
        if (!readNumber(*s, "sunSize", "sky.", sky.sunSizeDegrees, error, 0.05f, 20.0f)) return false;
        if (!readNumber(*s, "sunDisc", "sky.", sky.sunDiscIntensity, error, 0.0f, 10000.0f)) return false;
        if (!readNumber(*s, "sunGlow", "sky.", sky.sunGlow, error, 0.0f, 100.0f)) return false;
        if (!readNumber(*s, "yaw", "sky.", sky.yawDegrees, error, -360.0f, 360.0f)) return false;
        if (!readNumber(*s, "intensity", "sky.", sky.intensity, error, 0.0f, 100.0f)) return false;
        if (!readNumber(*s, "ambientStrength", "sky.", sky.ambientStrength, error, 0.0f, 10.0f)) return false;
        if (!readNumber(*s, "ambientSaturation", "sky.", sky.ambientSaturation, error, 0.0f, 2.0f)) return false;
        if (auto l = s->find("lightsScene"); l != s->end()) {
            if (!l->is_boolean()) {
                if (error) *error = "sky.lightsScene: true or false";
                return false;
            }
            sky.lightsScene = l->get<bool>();
        }
    }

    if (auto s = data.find("sun"); s != data.end()) {
        if (!s->is_object()) {
            if (error) *error = "sun: a set of values (azimuth, elevation, color, intensity)";
            return false;
        }
        if (!readNumber(*s, "azimuth", "sun.", m.sunAzimuthDegrees, error, -360.0f, 360.0f)) return false;
        float el = 0.0f;
        if (s->contains("elevation")) {
            if (!readNumber(*s, "elevation", "sun.", el, error, -90.0f, 90.0f)) return false;
            m.sunElevationDegrees = el;
        }
        if (auto c = s->find("color"); c != s->end()) {
            glm::vec3 col;
            if (!readColor(*c, "sun.color", col, error)) return false;
            m.sunColor = col;
        }
        if (!readNumber(*s, "intensity", "sun.", m.sunIntensity, error, 0.0f, 100.0f)) return false;
    }

    if (auto f = data.find("fill"); f != data.end()) {
        if (!f->is_object()) {
            if (error) *error = "fill: a set of values (color, intensity)";
            return false;
        }
        if (auto c = f->find("color"); c != f->end() && !readColor(*c, "fill.color", m.fillColor, error)) return false;
        if (!readNumber(*f, "intensity", "fill.", m.fillIntensity, error, 0.0f, 100.0f)) return false;
    }

    if (auto f = data.find("fog"); f != data.end()) {
        if (!f->is_object()) {
            if (error) *error = "fog: a set of values (density, heightFalloff, height, maxOpacity, color, sunScatter)";
            return false;
        }
        Fog& fog = m.fog;
        fog.enabled = true;
        if (!readNumber(*f, "density", "fog.", fog.density, error, 0.0f, 10.0f)) return false;
        if (!readNumber(*f, "heightFalloff", "fog.", fog.heightFalloff, error, 0.0001f, 10.0f)) return false;
        if (!readNumber(*f, "height", "fog.", fog.height, error)) return false;
        if (!readNumber(*f, "maxOpacity", "fog.", fog.maxOpacity, error, 0.0f, 1.0f)) return false;
        if (!readNumber(*f, "sunScatter", "fog.", fog.sunScatter, error, 0.0f, 10.0f)) return false;
        if (auto c = f->find("color"); c != f->end()) {
            if (c->is_string() && c->get<std::string>() == "sky") {
                fog.colorFromSky = true;
            } else {
                fog.colorFromSky = false;
                if (!readColor(*c, "fog.color", fog.color, error)) return false;
            }
        }
        fog.enabled = fog.density > 0.0f;
    }

    if (auto l = data.find("look"); l != data.end()) {
        if (l->is_string()) {
            if (!ColorGrade::preset(l->get<std::string>(), m.grade)) {
                if (error) *error = "look: '" + l->get<std::string>() + "' isn't one of none, punchy, golden, cool, faded";
                return false;
            }
        } else if (l->is_object()) {
            ColorGrade& g = m.grade;
            if (auto p = l->find("preset"); p != l->end()) {
                if (!p->is_string() || !ColorGrade::preset(p->get<std::string>(), g)) {
                    if (error) *error = "look.preset: one of none, punchy, golden, cool, faded";
                    return false;
                }
            }
            if (!readVec3Or1(*l, "slope", "look.", g.slope, error)) return false;
            if (!readVec3Or1(*l, "offset", "look.", g.offset, error)) return false;
            if (!readVec3Or1(*l, "power", "look.", g.power, error)) return false;
            if (!readNumber(*l, "saturation", "look.", g.saturation, error, 0.0f, 4.0f)) return false;
        } else {
            if (error) *error = "look: a name (punchy, golden, cool, faded, none) or { slope, offset, power, saturation }";
            return false;
        }
    }

    if (!readNumber(data, "exposure", "", m.exposure, error, 0.01f, 64.0f)) return false;
    if (auto a = data.find("ambience"); a != data.end()) {
        if (!a->is_string()) {
            if (error) *error = "ambience: the name of a looping sound in assets/ambience";
            return false;
        }
        m.ambience = a->get<std::string>();
    }
    out = std::move(m);
    return true;
}

void Mood::applyTo(Lighting& lighting) const {
    lighting.sky = sky;
    lighting.fog = fog;
    lighting.grade = grade;
    lighting.exposure = exposure;

    float elevation = sunElevationDegrees.value_or(35.0f);
    glm::vec3 color = sunColor.value_or(glm::vec3(1.0f, 0.96f, 0.9f));
    if (sky.kind == Sky::Kind::Image) {
        std::string err;
        if (auto img = SkyImage::cached(sky.image, &err)) {
            const SkyImage::Analysis& a = img->analysis();
            if (a.hasSun) {
                // Turn the sky so its sun is where the mood wants it.
                lighting.sky.yawDegrees = sunAzimuthDegrees - azimuthOf(a.sunDirection);
                if (!sunElevationDegrees) elevation = a.sunElevationDegrees;
                if (!sunColor) color = a.sunColor;
            }
        }
        // (A missing image is reported once by SkyResolver when it's drawn.)
    }
    Light& key = lighting.lights[0];
    key.enabled = true;
    key.isDirectional = true;
    key.direction = -sunDirectionFrom(sunAzimuthDegrees, std::max(elevation, 2.0f)); // a sun at the horizon would light nothing
    key.color = color;
    key.intensity = sunIntensity;
    if (sky.kind == Sky::Kind::Gradient) lighting.sky.sunColor = sunColor.value_or(sky.sunColor);

    Light& fill = lighting.lights[1];
    fill.enabled = fillIntensity > 0.0f;
    if (fill.enabled) {
        fill.isDirectional = true;
        fill.direction = -sunDirectionFrom(sunAzimuthDegrees + 180.0f, 25.0f);
        fill.color = fillColor;
        fill.intensity = fillIntensity;
    }
}

std::optional<Mood> loadMood(const std::string& nameOrPath, std::string* error) {
    std::error_code ec;
    Mood m;
    datafile::Loaded loaded;
    std::string err;
    const fs::path asPath(nameOrPath);
    bool found = false;
    if (datafile::formatOf(asPath) && fs::is_regular_file(asPath, ec)) {
        if (!datafile::loadFile(asPath, loaded.data, &err)) {
            if (error) *error = "mood '" + nameOrPath + "': " + err;
            return std::nullopt;
        }
        loaded.file = asPath;
        found = true;
    } else {
        for (const fs::path& dir : moodFolders()) {
            if (!datafile::exists(dir, nameOrPath)) continue;
            if (!datafile::load(dir, nameOrPath, loaded, &err)) {
                if (error) *error = "mood '" + nameOrPath + "': " + err;
                return std::nullopt;
            }
            found = true;
            break;
        }
    }
    if (!found) {
        std::string names;
        for (const std::string& n : listMoods()) names += (names.empty() ? "" : ", ") + n;
        if (error) *error = "no mood called '" + nameOrPath + "'" + (names.empty() ? std::string() : " (there are: " + names + ")");
        return std::nullopt;
    }
    m.name = loaded.file.stem().string();
    m.file = loaded.file;
    if (!parseMood(loaded.data, loaded.file.parent_path(), m, &err)) {
        if (error) *error = loaded.file.generic_string() + ": " + err;
        return std::nullopt;
    }
    return m;
}

std::vector<std::string> listMoods() {
    std::set<std::string> names;
    std::error_code ec;
    for (const fs::path& dir : moodFolders()) {
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec))
            if (entry.is_regular_file(ec) && datafile::formatOf(entry.path())) names.insert(entry.path().stem().string());
    }
    return { names.begin(), names.end() };
}

} // namespace kke
