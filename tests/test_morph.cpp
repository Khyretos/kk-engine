#include "kke/Morph.h"
#include "kke/ModelAsset.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

using kke::ModelData;
using kke::ModelMesh;
using kke::ModelMorphTarget;

namespace {

// A flat quad in the XY plane facing +Z, two triangles.
ModelMesh quad() {
    ModelMesh m;
    const glm::vec3 p[4] = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
    for (const glm::vec3& v : p) {
        kke::ModelVertex vert;
        vert.position = v;
        vert.normal = { 0, 0, 1 };
        m.vertices.push_back(vert);
    }
    m.indices = { 0, 1, 2, 0, 2, 3 };
    return m;
}

ModelMorphTarget lift(const char* name, std::vector<uint32_t> verts, glm::vec3 by, bool withNormals = false) {
    ModelMorphTarget t;
    t.name = name;
    t.vertices = std::move(verts);
    for (size_t i = 0; i < t.vertices.size(); ++i) {
        t.positions.push_back(by);
        if (withNormals) t.normals.push_back(glm::vec3(0, 0, 0));
    }
    return t;
}

std::string base64(const std::string& bytes) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned n = (static_cast<unsigned char>(bytes[i]) << 16) | (i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) << 8 : 0) |
                           (i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0);
        out += t[(n >> 18) & 63];
        out += t[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? t[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? t[n & 63] : '=';
    }
    return out;
}

} // namespace

TEST(Morph, WeightsScaleAndAddTheOffsets) {
    ModelMesh m = quad();
    m.morphs.push_back(lift("smile", { 2, 3 }, { 0, 0.2f, 0 }));
    m.morphs.push_back(lift("wide", { 1, 2 }, { 0.5f, 0, 0 }));
    const auto none = kke::morphedVertices(m, { 0.0f, 0.0f });
    EXPECT_EQ(none[2].position, m.vertices[2].position);
    const auto half = kke::morphedVertices(m, { 0.5f, 0.0f });
    EXPECT_NEAR(half[2].position.y, 1.1f, 1e-5f);
    EXPECT_NEAR(half[3].position.y, 1.1f, 1e-5f);
    EXPECT_EQ(half[0].position, m.vertices[0].position) << "vertices a target does not list stay put";
    const auto both = kke::morphedVertices(m, { 1.0f, 2.0f });
    EXPECT_NEAR(both[2].position.x, 2.0f, 1e-5f) << "weights beyond 1 exaggerate";
    EXPECT_NEAR(both[2].position.y, 1.2f, 1e-5f);
    EXPECT_NEAR(both[1].position.x, 2.0f, 1e-5f);
    const auto neg = kke::morphedVertices(m, { -1.0f });
    EXPECT_NEAR(neg[2].position.y, 0.8f, 1e-5f) << "a missing weight is 0, a negative one goes the other way";
}

TEST(Morph, NormalsAreRebuiltFromTheShapeWhenATargetHasNone) {
    ModelMesh m = quad();
    m.morphs.push_back(lift("bump", { 2 }, { 0, 0, 1.0f })); // pulls one corner towards the viewer
    const auto v = kke::morphedVertices(m, { 1.0f });
    // The corner is now a peak: the surface slopes down towards the quad's middle (-x, -y), and its normal leans that way.
    EXPECT_LT(v[2].normal.x, -0.1f) << "the surface tilts, so the normal does too";
    EXPECT_LT(v[2].normal.y, -0.1f);
    EXPECT_GT(v[2].normal.z, 0.3f);
    for (const auto& vert : v) EXPECT_NEAR(glm::length(vert.normal), 1.0f, 1e-5f);
    // At weight 0 nothing changes: the rest normals are kept, not recomputed.
    const auto rest = kke::morphedVertices(m, { 0.0f });
    EXPECT_EQ(rest[2].normal, glm::vec3(0, 0, 1));
}

TEST(Morph, GivenNormalOffsetsAreAddedAndRenormalised) {
    ModelMesh m = quad();
    ModelMorphTarget t = lift("tilt", { 0, 1, 2, 3 }, { 0, 0, 0 }, true);
    for (auto& n : t.normals) n = { 1, 0, 0 };
    m.morphs.push_back(t);
    const auto v = kke::morphedVertices(m, { 1.0f });
    EXPECT_NEAR(v[0].normal.x, std::sqrt(0.5f), 1e-5f);
    EXPECT_NEAR(v[0].normal.z, std::sqrt(0.5f), 1e-5f);
}

TEST(Morph, NamesSpanMeshesSoOneNameMovesEveryMeshThatHasIt) {
    ModelData model;
    ModelMesh head = quad(), eyes = quad();
    head.morphs.push_back(lift("blink", { 2, 3 }, { 0, -0.1f, 0 }));
    head.morphs.push_back(lift("smile", { 1 }, { 0, 0.1f, 0 }));
    eyes.morphs.push_back(lift("blink", { 1 }, { 0, -0.05f, 0 }));
    model.meshes = { head, eyes, quad() };
    const auto names = kke::morphNames(model);
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "blink");
    EXPECT_EQ(names[1], "smile");
    const auto w = kke::meshMorphWeights(model, { { "blink", 0.7f }, { "nothing", 1.0f } });
    ASSERT_EQ(w.size(), 3u);
    EXPECT_EQ(w[0], (std::vector<float>{ 0.7f, 0.0f }));
    EXPECT_EQ(w[1], (std::vector<float>{ 0.7f }));
    EXPECT_TRUE(w[2].empty());
    EXPECT_TRUE(kke::morphActive(model.meshes[0], w[0]));
    EXPECT_FALSE(kke::morphActive(model.meshes[2], w[2]));
    EXPECT_FALSE(kke::morphActive(model.meshes[0], { 0.0f, 0.0f }));
}

TEST(Morph, ReadsGltfMorphTargetsWithTheirNamesAndDefaultWeights) {
    // One triangle with two morph targets, built by hand: positions, normals, indices and the offsets.
    std::string buffer;
    auto add = [&](std::initializer_list<float> floats) {
        for (float f : floats) buffer.append(reinterpret_cast<const char*>(&f), 4);
    };
    add({ 0, 0, 0, 1, 0, 0, 0, 1, 0 });   // positions (0..35)
    add({ 0, 0, 1, 0, 0, 1, 0, 0, 1 });   // normals (36..71)
    add({ 0, 0, 0, 0, 0, 0, 0, 0.5f, 0 }); // "smile": moves only the third vertex up (72..107)
    add({ 0, 0, 0.2f, 0, 0, 0.2f, 0, 0, 0 }); // "puff": moves the first two towards the viewer (108..143)
    const uint16_t idx[4] = { 0, 1, 2, 0 };
    buffer.append(reinterpret_cast<const char*>(idx), 8); // indices (144..151)
    const std::string json = R"({
      "asset": {"version": "2.0"},
      "buffers": [{"byteLength": 152, "uri": "data:application/octet-stream;base64,)" + base64(buffer) + R"("}],
      "bufferViews": [{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},
                      {"buffer":0,"byteOffset":72,"byteLength":36},{"buffer":0,"byteOffset":108,"byteLength":36},
                      {"buffer":0,"byteOffset":144,"byteLength":6}],
      "accessors": [
        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
        {"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":2,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":3,"componentType":5126,"count":3,"type":"VEC3"},
        {"bufferView":4,"componentType":5123,"count":3,"type":"SCALAR"}],
      "meshes": [{"name":"face","weights":[0.25,0],"extras":{"targetNames":["smile","puff"]},
                  "primitives":[{"attributes":{"POSITION":0,"NORMAL":1},"indices":4,
                                 "targets":[{"POSITION":2},{"POSITION":3}]}]}],
      "nodes": [{"mesh":0}],
      "scenes": [{"nodes":[0]}], "scene": 0
    })";
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "kke_morph_test.gltf";
    std::ofstream(file) << json;
    const ModelData model = kke::loadModel(file.string());
    ASSERT_FALSE(model.meshes.empty());
    const ModelMesh& mesh = model.meshes[0];
    ASSERT_EQ(mesh.morphs.size(), 2u);
    EXPECT_EQ(mesh.morphs[0].name, "smile");
    EXPECT_EQ(mesh.morphs[1].name, "puff");
    EXPECT_EQ(mesh.morphs[0].vertices, (std::vector<uint32_t>{ 2 })) << "only the vertices a target moves are kept";
    EXPECT_NEAR(mesh.morphs[0].positions[0].y, 0.5f, 1e-6f);
    EXPECT_EQ(mesh.morphs[1].vertices, (std::vector<uint32_t>{ 0, 1 }));
    ASSERT_EQ(mesh.morphWeights.size(), 2u);
    EXPECT_NEAR(mesh.morphWeights[0], 0.25f, 1e-6f);
    EXPECT_NEAR(mesh.morphWeights[1], 0.0f, 1e-6f);
    // And it morphs: the file's own weights put vertex 2 a quarter of the way up.
    const auto v = kke::morphedVertices(mesh, mesh.morphWeights);
    EXPECT_NEAR(v[2].position.y, 1.0f + 0.125f, 1e-5f);
    std::filesystem::remove(file);
}
