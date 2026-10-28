#include "kke/Morph.h"

#include <algorithm>
#include <cmath>

namespace kke {

std::vector<std::string> morphNames(const ModelData& model) {
    std::vector<std::string> names;
    for (const ModelMesh& mesh : model.meshes)
        for (const ModelMorphTarget& target : mesh.morphs)
            if (std::find(names.begin(), names.end(), target.name) == names.end()) names.push_back(target.name);
    return names;
}

std::vector<std::vector<float>> meshMorphWeights(const ModelData& model, const std::map<std::string, float>& byName) {
    std::vector<std::vector<float>> out;
    out.reserve(model.meshes.size());
    for (const ModelMesh& mesh : model.meshes) {
        std::vector<float> weights(mesh.morphs.size(), 0.0f);
        for (size_t t = 0; t < mesh.morphs.size(); ++t)
            if (auto it = byName.find(mesh.morphs[t].name); it != byName.end()) weights[t] = it->second;
        out.push_back(std::move(weights));
    }
    return out;
}

bool morphActive(const ModelMesh& mesh, const std::vector<float>& weights) {
    for (size_t t = 0; t < mesh.morphs.size() && t < weights.size(); ++t)
        if (weights[t] != 0.0f) return true;
    return false;
}

std::vector<ModelVertex> morphedVertices(const ModelMesh& mesh, const std::vector<float>& weights) {
    std::vector<ModelVertex> out = mesh.vertices;
    bool normalsFromShape = false;
    for (size_t t = 0; t < mesh.morphs.size() && t < weights.size(); ++t) {
        const float w = weights[t];
        if (w == 0.0f) continue;
        const ModelMorphTarget& target = mesh.morphs[t];
        const bool hasNormals = target.normals.size() == target.vertices.size();
        if (!hasNormals) normalsFromShape = true;
        for (size_t k = 0; k < target.vertices.size(); ++k) {
            const uint32_t v = target.vertices[k];
            if (v >= out.size()) continue;
            out[v].position += target.positions[k] * w;
            if (hasNormals) out[v].normal += target.normals[k] * w;
        }
    }
    if (normalsFromShape) {
        // A target without normal offsets: smooth normals from the morphed triangles (area weighted).
        std::vector<glm::vec3> sum(out.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            const uint32_t a = mesh.indices[i], b = mesh.indices[i + 1], c = mesh.indices[i + 2];
            if (a >= out.size() || b >= out.size() || c >= out.size()) continue;
            const glm::vec3 n = glm::cross(out[b].position - out[a].position, out[c].position - out[a].position);
            sum[a] += n;
            sum[b] += n;
            sum[c] += n;
        }
        for (size_t v = 0; v < out.size(); ++v)
            if (glm::dot(sum[v], sum[v]) > 1e-20f) out[v].normal = sum[v];
    }
    for (ModelVertex& v : out) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-8f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
    return out;
}

} // namespace kke
