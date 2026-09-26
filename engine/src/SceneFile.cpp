#include "kke/SceneFile.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace kke {

namespace {

glm::vec3 vec3(const nlohmann::json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
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
        s.objects.push_back(std::move(so));
    }
    return s;
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
