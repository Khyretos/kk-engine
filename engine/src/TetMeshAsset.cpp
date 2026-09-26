#include "kke/TetMeshAsset.h"

#include "kke/DataFile.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace kke {

TetMeshData loadTetMeshFromFile(const std::string& path) {
    std::string error;
    bool exists = false;
    nlohmann::json j;
    const bool parsed = datafile::loadPath(path, j, &error, &exists); // .json, .yml or .yaml
    if (!exists) {
        throw std::runtime_error("loadTetMeshFromFile: could not open '" + path + "'");
    }
    if (!parsed) {
        throw std::runtime_error("loadTetMeshFromFile: not valid JSON or YAML: " + error);
    }

    if (!j.is_object() || !j.contains("vertices") || !j.contains("tets")) {
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
