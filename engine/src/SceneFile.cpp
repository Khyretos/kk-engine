#include "kke/SceneFile.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kke {

namespace {

glm::vec3 vec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

// A float as the shortest decimal that reads back as the same float:
// 0.1f is written "0.1", not "0.10000000149011612" (JSON numbers are
// doubles), and still loads as exactly 0.1f.
double num(float f) {
    char buf[32];
    auto r = std::to_chars(buf, buf + sizeof(buf), f);
    *r.ptr = '\0';
    return std::strtod(buf, nullptr);
}
nlohmann::json arr(const glm::vec3& v) { return nlohmann::json::array({ num(v.x), num(v.y), num(v.z) }); }

const char* const kBreakables[] = { "wood", "stone", "glass", "ceramic", "metal" };

bool knownBreakable(const std::string& b) {
    for (const char* k : kBreakables) if (b == k) return true;
    return false;
}

} // namespace

SceneFile SceneFile::parse(const std::string& text, const std::string& sourceName) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(text, nullptr, true, /*ignore_comments=*/true);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(sourceName + ": not valid JSON: " + e.what());
    }
    if (j.value("format", std::string()) != "kke.scene")
        throw std::runtime_error(sourceName + ": \"format\" must be \"kke.scene\"");
    if (j.value("version", 0) != 1)
        throw std::runtime_error(sourceName + ": unsupported \"version\" (this engine reads 1)");
    SceneFile s;
    s.name = j.value("name", std::string());
    s.description = j.value("description", std::string());
    if (j.contains("packs")) s.packs = j["packs"].get<std::vector<std::string>>();
    if (j.contains("spawn")) {
        s.spawn = vec3(j["spawn"].value("position", nlohmann::json()), glm::vec3(0.0f));
        s.spawnYaw = j["spawn"].value("yaw", 0.0f);
    }
    s.worldSeed = j.value("worldSeed", 0u);
    if (j.contains("sun")) {
        const nlohmann::json& sun = j["sun"];
        s.hasSun = true;
        s.sunDirection = vec3(sun.value("direction", nlohmann::json()), s.sunDirection);
        if (glm::dot(s.sunDirection, s.sunDirection) < 1e-8f) throw std::runtime_error(sourceName + ": \"sun\" direction is zero");
        s.sunDirection = glm::normalize(s.sunDirection);
        s.sunColor = vec3(sun.value("color", nlohmann::json()), s.sunColor);
        s.sunIntensity = sun.value("intensity", s.sunIntensity);
    }
    if (j.contains("ambient")) {
        s.hasAmbient = true;
        s.ambient = vec3(j["ambient"], s.ambient);
    }
    if (j.contains("lights")) {
        if (!j["lights"].is_array()) throw std::runtime_error(sourceName + ": \"lights\" must be an array");
        for (const nlohmann::json& l : j["lights"]) {
            SceneLight light;
            light.position = vec3(l.value("position", nlohmann::json()), light.position);
            light.color = vec3(l.value("color", nlohmann::json()), light.color);
            light.intensity = l.value("intensity", light.intensity);
            s.lights.push_back(light);
        }
    }
    if (j.contains("ground")) {
        const nlohmann::json& g = j["ground"];
        if (g.contains("size")) s.groundSize = glm::vec2(g["size"][0].get<float>(), g["size"][1].get<float>());
        s.groundColor = vec3(g.value("color", nlohmann::json()), s.groundColor);
    }
    if (!j.contains("objects") || !j["objects"].is_array()) throw std::runtime_error(sourceName + ": needs an \"objects\" array");
    int index = 0;
    for (const nlohmann::json& o : j["objects"]) {
        const std::string where = sourceName + ": objects[" + std::to_string(index++) + "]";
        SceneObject so;
        so.asset = o.value("asset", std::string());
        if (so.asset.empty()) throw std::runtime_error(where + " has no \"asset\"");
        so.position = vec3(o.value("position", nlohmann::json()), glm::vec3(0.0f));
        so.yaw = o.value("yaw", 0.0f);
        if (o.contains("scale")) so.scale = o["scale"].is_number() ? glm::vec3(o["scale"].get<float>()) : vec3(o["scale"], glm::vec3(1.0f));
        const std::string c = o.value("collision", std::string("mesh"));
        if (c == "mesh") so.collision = SceneObject::Collision::Mesh;
        else if (c == "box") so.collision = SceneObject::Collision::Box;
        else if (c == "none") so.collision = SceneObject::Collision::None;
        else throw std::runtime_error(where + ": \"collision\" is \"mesh\", \"box\" or \"none\", not \"" + c + "\"");
        so.pivot = o.value("pivot", false);
        if (o.contains("grid")) {
            const nlohmann::json& g = o["grid"];
            if (g.contains("count")) so.gridCount = glm::ivec2(g["count"][0].get<int>(), g["count"][1].get<int>());
            if (g.contains("step")) so.gridStep = glm::vec2(g["step"][0].get<float>(), g["step"][1].get<float>());
            if (so.gridCount.x < 1 || so.gridCount.y < 1 || so.gridCount.x * so.gridCount.y > 10000)
                throw std::runtime_error(where + ": grid count must be 1..10000 cells");
        }
        so.pack = o.value("pack", std::string());
        so.texture = o.value("texture", std::string());
        so.breakable = o.value("breakable", std::string());
        if (!so.breakable.empty() && !knownBreakable(so.breakable))
            throw std::runtime_error(where + ": \"breakable\" is wood, stone, glass, ceramic or metal, not \"" + so.breakable + "\"");
        so.fractureSeed = o.value("fractureSeed", 0u);
        s.objects.push_back(std::move(so));
    }
    return s;
}

std::string SceneFile::toJson() const {
    // Key order as a person would write it (ordered_json keeps insertion
    // order), and defaults left out, so saved scenes stay readable diffs.
    nlohmann::ordered_json j;
    j["format"] = "kke.scene";
    j["version"] = 1;
    if (!name.empty()) j["name"] = name;
    if (!description.empty()) j["description"] = description;
    if (!packs.empty()) j["packs"] = packs;
    j["spawn"] = { { "position", arr(spawn) }, { "yaw", num(spawnYaw) } };
    if (groundSize.x > 0.0f || groundSize.y > 0.0f)
        j["ground"] = { { "size", { num(groundSize.x), num(groundSize.y) } }, { "color", arr(groundColor) } };
    if (worldSeed) j["worldSeed"] = worldSeed;
    if (hasSun) j["sun"] = { { "direction", arr(sunDirection) }, { "color", arr(sunColor) }, { "intensity", num(sunIntensity) } };
    if (hasAmbient) j["ambient"] = arr(ambient);
    if (!lights.empty()) {
        nlohmann::ordered_json ls = nlohmann::ordered_json::array();
        for (const SceneLight& l : lights) ls.push_back({ { "position", arr(l.position) }, { "color", arr(l.color) }, { "intensity", num(l.intensity) } });
        j["lights"] = ls;
    }
    nlohmann::ordered_json objs = nlohmann::ordered_json::array();
    for (const SceneObject& o : objects) {
        nlohmann::ordered_json e;
        e["asset"] = o.asset;
        e["position"] = arr(o.position);
        if (o.yaw != 0.0f) e["yaw"] = num(o.yaw);
        if (o.scale != glm::vec3(1.0f)) {
            if (o.scale.x == o.scale.y && o.scale.y == o.scale.z) e["scale"] = num(o.scale.x);
            else e["scale"] = arr(o.scale);
        }
        if (o.collision != SceneObject::Collision::Mesh) e["collision"] = o.collision == SceneObject::Collision::Box ? "box" : "none";
        if (o.pivot) e["pivot"] = true;
        if (o.gridCount != glm::ivec2(1, 1) || o.gridStep != glm::vec2(0.0f))
            e["grid"] = { { "count", { o.gridCount.x, o.gridCount.y } }, { "step", { num(o.gridStep.x), num(o.gridStep.y) } } };
        if (!o.pack.empty()) e["pack"] = o.pack;
        if (!o.texture.empty()) e["texture"] = o.texture;
        if (!o.breakable.empty()) e["breakable"] = o.breakable;
        if (o.fractureSeed) e["fractureSeed"] = o.fractureSeed;
        objs.push_back(std::move(e));
    }
    j["objects"] = std::move(objs);
    // One line per array of numbers ("position": [1, 0, 2]), as people
    // write scenes, instead of dump()'s one number per line.
    const std::string text = j.dump(2);
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '[') {
            const size_t end = text.find_first_of("[]{}\"", i + 1);
            if (end != std::string::npos && text[end] == ']') {
                std::string inner;
                bool space = false;
                for (size_t k = i + 1; k < end; ++k) {
                    const char ch = text[k];
                    if (ch == ' ' || ch == '\n') { space = !inner.empty(); continue; }
                    if (space && inner.back() == ',') inner += ' ';
                    space = false;
                    inner += ch;
                }
                out += '[' + inner + ']';
                i = end;
                continue;
            }
        }
        out += text[i];
    }
    return out + "\n";
}

void SceneFile::save(const std::string& path) const {
    const std::string text = toJson();
    const std::filesystem::path target(path);
    std::filesystem::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error(path + ": can't write (is the folder there and writable?)");
        out << text;
        out.flush();
        if (!out) throw std::runtime_error(path + ": write failed (disk full?)");
    }
    std::error_code ec;
    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error(path + ": can't replace the file: " + ec.message());
    }
}

SceneFile SceneFile::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error(path + ": can't open");
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str(), path);
}

glm::mat4 SceneFile::placement(const SceneObject& o, const glm::vec3& bmin, const glm::vec3& bmax, glm::ivec2 cell) {
    // Grid cells step along the object's own (rotated) X/Z axes, so a
    // rotated fence line stays a line.
    const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(o.yaw), glm::vec3(0, 1, 0));
    const glm::vec3 step = glm::vec3(rot * glm::vec4(cell.x * o.gridStep.x, 0.0f, cell.y * o.gridStep.y, 0.0f));
    glm::mat4 t = glm::translate(glm::mat4(1.0f), o.position + step) * rot;
    t = glm::scale(t, o.scale);
    if (o.pivot) return t;
    const glm::vec3 c = (bmin + bmax) * 0.5f;
    return glm::translate(t, glm::vec3(-c.x, -bmin.y, -c.z));
}

std::map<std::string, int> SceneFile::assetsUsed() const {
    std::map<std::string, int> used;
    for (const SceneObject& o : objects) used[o.asset] += o.gridCount.x * o.gridCount.y;
    return used;
}

size_t SceneFile::instanceCount() const {
    size_t n = 0;
    for (const SceneObject& o : objects) n += static_cast<size_t>(o.gridCount.x * o.gridCount.y);
    return n;
}

} // namespace kke
