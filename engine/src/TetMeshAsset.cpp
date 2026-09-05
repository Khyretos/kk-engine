#include "kke/TetMeshAsset.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

namespace kke {

TetMeshData loadTetMeshFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("loadTetMeshFromFile: could not open '" + path + "'");
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("loadTetMeshFromFile: '" + path + "' is not valid JSON: " + e.what());
    }

    if (!j.contains("vertices") || !j.contains("tets")) {
        throw std::runtime_error("loadTetMeshFromFile: '" + path + "' is missing 'vertices' or 'tets'");
    }

    TetMeshData data;

    for (const auto& v : j["vertices"]) {
        if (!v.is_array() || v.size() != 3) {
            throw std::runtime_error("loadTetMeshFromFile: '" + path + "' has a vertex that isn't a [x, y, z] triple");
        }
        data.vertices.push_back(glm::vec3(v[0].get<float>(), v[1].get<float>(), v[2].get<float>()));
    }

    for (const auto& t : j["tets"]) {
        if (!t.is_array() || t.size() != 4) {
            throw std::runtime_error("loadTetMeshFromFile: '" + path + "' has a tet that isn't a [a, b, c, d] quadruple");
        }
        std::array<uint32_t, 4> tet = {
            t[0].get<uint32_t>(), t[1].get<uint32_t>(), t[2].get<uint32_t>(), t[3].get<uint32_t>()
        };
        for (uint32_t idx : tet) {
            if (idx >= data.vertices.size()) {
                throw std::runtime_error("loadTetMeshFromFile: '" + path + "' has a tet referencing vertex " +
                                          std::to_string(idx) + ", but only " + std::to_string(data.vertices.size()) +
                                          " vertices exist -- file is internally inconsistent");
            }
        }
        data.tets.push_back(tet);
    }

    if (data.vertices.empty() || data.tets.empty()) {
        throw std::runtime_error("loadTetMeshFromFile: '" + path + "' has zero vertices or zero tets");
    }

    return data;
}

} // namespace kke
