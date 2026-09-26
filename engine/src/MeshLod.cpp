#include "kke/MeshLod.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>

namespace kke {

namespace {

// Byte-wise comparison needs no padding inside a vertex.
static_assert(sizeof(ModelVertex) == 64, "ModelVertex must stay tightly packed for vertex welding");

size_t weldMesh(ModelMesh& m) {
    if (m.vertices.empty() || m.indices.empty()) return 0;
    const size_t before = m.vertices.size();
    std::vector<unsigned int> remap(before);
    const size_t unique = meshopt_generateVertexRemap(remap.data(), m.indices.data(), m.indices.size(), m.vertices.data(), before, sizeof(ModelVertex));
    std::vector<ModelVertex> vertices(unique);
    meshopt_remapVertexBuffer(vertices.data(), m.vertices.data(), before, sizeof(ModelVertex), remap.data());
    std::vector<uint32_t> indices(m.indices.size());
    meshopt_remapIndexBuffer(indices.data(), m.indices.data(), m.indices.size(), remap.data());
    meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), unique);
    meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), vertices.data(), unique, sizeof(ModelVertex));
    m.vertices = std::move(vertices);
    m.indices = std::move(indices);
    return before - unique;
}

} // namespace

size_t weldModel(ModelData& model) {
    size_t removed = 0;
    for (ModelMesh& m : model.meshes) removed += weldMesh(m);
    return removed;
}

ModelData simplifyModel(const ModelData& model, float ratio, const SimplifyOptions& options) {
    ModelData out = model;
    weldModel(out);
    ratio = std::clamp(ratio, 0.0f, 1.0f);
    for (ModelMesh& m : out.meshes) {
        const size_t triangles = m.indices.size() / 3;
        if (triangles < 64 || ratio >= 1.0f) continue;
        const size_t target = std::max<size_t>(3, static_cast<size_t>(static_cast<float>(triangles) * ratio) * 3);
        // Normals and UVs (5 floats) weigh in so seams and shading survive.
        std::vector<float> attributes(m.vertices.size() * 5);
        for (size_t v = 0; v < m.vertices.size(); ++v) {
            const ModelVertex& mv = m.vertices[v];
            float* a = &attributes[v * 5];
            a[0] = mv.normal.x;
            a[1] = mv.normal.y;
            a[2] = mv.normal.z;
            a[3] = mv.uv.x;
            a[4] = mv.uv.y;
        }
        const float w = options.attributeWeight;
        const float weights[5] = { w, w, w, w, w };
        std::vector<uint32_t> indices(m.indices.size());
        float error = 0.0f;
        unsigned int flags = 0;
        if (options.lockBorders) flags |= meshopt_SimplifyLockBorder;
        if (options.acrossSeams) flags |= meshopt_SimplifyPermissive;
        if (options.prune) flags |= meshopt_SimplifyPrune;
        const size_t count = meshopt_simplifyWithAttributes(
            indices.data(), m.indices.data(), m.indices.size(), &m.vertices[0].position.x, m.vertices.size(), sizeof(ModelVertex), attributes.data(),
            5 * sizeof(float), weights, 5, nullptr, target, options.maxError, flags, &error);
        indices.resize(count);
        // Drop the vertices nothing uses any more.
        std::vector<ModelVertex> vertices(m.vertices.size());
        const size_t kept = meshopt_optimizeVertexFetch(vertices.data(), indices.data(), indices.size(), m.vertices.data(), m.vertices.size(),
                                                        sizeof(ModelVertex));
        vertices.resize(kept);
        meshopt_optimizeVertexCache(indices.data(), indices.data(), indices.size(), kept);
        m.vertices = std::move(vertices);
        m.indices = std::move(indices);
    }
    return out;
}

} // namespace kke
