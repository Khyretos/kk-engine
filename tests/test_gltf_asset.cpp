// glTF 2.0 import (engine/src/GltfAsset.cpp): files are written here from
// scratch (JSON + binary buffers), so every value checked is known.

#include "kke/CookedFile.h"
#include "kke/ModelAsset.h"

#include <gtest/gtest.h>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path tempDir(const char* name) {
    fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

std::string base64(const std::vector<uint8_t>& in) {
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < in.size(); i += 3) {
        const uint32_t n = (uint32_t(in[i]) << 16) | (i + 1 < in.size() ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
        out += abc[(n >> 18) & 63];
        out += abc[(n >> 12) & 63];
        out += i + 1 < in.size() ? abc[(n >> 6) & 63] : '=';
        out += i + 2 < in.size() ? abc[n & 63] : '=';
    }
    return out;
}

// One growing binary buffer with views and accessors over it.
struct Builder {
    json doc = { { "asset", { { "version", "2.0" } } } };
    std::vector<uint8_t> bin;

    int view(const void* data, size_t size) {
        while (bin.size() % 4) bin.push_back(0);
        const size_t offset = bin.size();
        bin.insert(bin.end(), static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        doc["bufferViews"].push_back({ { "buffer", 0 }, { "byteOffset", offset }, { "byteLength", size } });
        return static_cast<int>(doc["bufferViews"].size()) - 1;
    }
    int floats(const std::vector<float>& v, const char* type, size_t width, bool minMax = false) {
        json acc = { { "bufferView", view(v.data(), v.size() * sizeof(float)) }, { "componentType", 5126 }, { "count", v.size() / width }, { "type", type } };
        if (minMax) {
            std::vector<float> lo(width, 1e30f), hi(width, -1e30f);
            for (size_t i = 0; i < v.size(); ++i) {
                lo[i % width] = std::min(lo[i % width], v[i]);
                hi[i % width] = std::max(hi[i % width], v[i]);
            }
            acc["min"] = lo;
            acc["max"] = hi;
        }
        doc["accessors"].push_back(acc);
        return static_cast<int>(doc["accessors"].size()) - 1;
    }
    int indices(const std::vector<uint16_t>& v) {
        doc["accessors"].push_back({ { "bufferView", view(v.data(), v.size() * 2) }, { "componentType", 5123 }, { "count", v.size() }, { "type", "SCALAR" } });
        return static_cast<int>(doc["accessors"].size()) - 1;
    }
    int bytes4(const std::vector<uint8_t>& v) {
        doc["accessors"].push_back({ { "bufferView", view(v.data(), v.size()) }, { "componentType", 5121 }, { "count", v.size() / 4 }, { "type", "VEC4" } });
        return static_cast<int>(doc["accessors"].size()) - 1;
    }

    // As a .gltf with the buffer inline (base64 data URI).
    fs::path writeGltf(const fs::path& file) {
        json d = doc;
        d["buffers"] = json::array({ { { "byteLength", bin.size() }, { "uri", "data:application/octet-stream;base64," + base64(bin) } } });
        std::ofstream(file) << d.dump();
        return file;
    }
    // As a .gltf with an external .bin next to it.
    fs::path writeGltfWithBin(const fs::path& file, const std::string& binName) {
        json d = doc;
        d["buffers"] = json::array({ { { "byteLength", bin.size() }, { "uri", binName } } });
        std::ofstream(file) << d.dump();
        std::ofstream(file.parent_path() / binName, std::ios::binary).write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
        return file;
    }
    // As one .glb.
    fs::path writeGlb(const fs::path& file) {
        json d = doc;
        while (bin.size() % 4) bin.push_back(0);
        d["buffers"] = json::array({ { { "byteLength", bin.size() } } });
        std::string text = d.dump();
        while (text.size() % 4) text += ' ';
        auto u32 = [](std::ofstream& o, uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); };
        std::ofstream o(file, std::ios::binary);
        u32(o, 0x46546C67u);
        u32(o, 2);
        u32(o, static_cast<uint32_t>(12 + 8 + text.size() + 8 + bin.size()));
        u32(o, static_cast<uint32_t>(text.size()));
        u32(o, 0x4E4F534Au);
        o.write(text.data(), static_cast<std::streamsize>(text.size()));
        u32(o, static_cast<uint32_t>(bin.size()));
        u32(o, 0x004E4942u);
        o.write(reinterpret_cast<const char*>(bin.data()), static_cast<std::streamsize>(bin.size()));
        return file;
    }
};

// A 1x1 quad in the XY plane facing +Z, two triangles.
void addQuadMesh(Builder& b, bool withNormals, int material = -1) {
    const int pos = b.floats({ -0.5f, -0.5f, 0, 0.5f, -0.5f, 0, 0.5f, 0.5f, 0, -0.5f, 0.5f, 0 }, "VEC3", 3, true);
    json attrs = { { "POSITION", pos }, { "TEXCOORD_0", b.floats({ 0, 1, 1, 1, 1, 0, 0, 0 }, "VEC2", 2) } };
    if (withNormals) attrs["NORMAL"] = b.floats({ 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 }, "VEC3", 3);
    json prim = { { "attributes", attrs }, { "indices", b.indices({ 0, 1, 2, 0, 2, 3 }) } };
    if (material >= 0) prim["material"] = material;
    b.doc["meshes"].push_back({ { "name", "Quad" }, { "primitives", json::array({ prim }) } });
}

std::vector<uint8_t> png(int w, int h, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> out;
    stbi_write_png_to_func([](void* ctx, void* data, int size) {
        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
        v->insert(v->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
    }, &out, w, h, 4, rgba.data(), w * 4);
    return out;
}

glm::vec3 faceNormal(const kke::ModelMesh& m, size_t tri) {
    const glm::vec3 a = m.vertices[m.indices[tri * 3]].position, b = m.vertices[m.indices[tri * 3 + 1]].position,
                    c = m.vertices[m.indices[tri * 3 + 2]].position;
    return glm::normalize(glm::cross(b - a, c - a));
}

} // namespace

TEST(GltfAsset, LoadsMeshesPlacedByTheirNodesWithFactors) {
    const fs::path dir = tempDir("kke_gltf_static");
    Builder b;
    b.doc["materials"].push_back({ { "name", "Paint" },
                                   { "pbrMetallicRoughness", { { "baseColorFactor", { 0.2, 0.4, 0.6, 1.0 } }, { "metallicFactor", 0.25 }, { "roughnessFactor", 0.5 } } } });
    addQuadMesh(b, true, 0);
    // One quad moved 2 m right, one mirrored (scale -1 on X) 2 m left.
    b.doc["nodes"] = json::array({ { { "mesh", 0 }, { "translation", { 2, 0, 0 } } }, { { "mesh", 0 }, { "translation", { -2, 0, 0 } }, { "scale", { -1, 1, 1 } } } });
    b.doc["scenes"] = json::array({ { { "nodes", { 0, 1 } } } });
    b.doc["scene"] = 0;
    const kke::ModelData m = kke::loadModel(b.writeGltf(dir / "quads.gltf").string());
    ASSERT_EQ(m.meshes.size(), 2u);
    EXPECT_EQ(m.triangleCount(), 4u);
    ASSERT_EQ(m.materials.size(), 1u); // no default material: every primitive names one
    EXPECT_EQ(m.materials[0].name, "Paint");
    EXPECT_NEAR(m.materials[0].baseColor.g, 0.4f, 1e-5f);
    EXPECT_NEAR(m.materials[0].metallic, 0.25f, 1e-5f);
    EXPECT_NEAR(m.materials[0].roughness, 0.5f, 1e-5f);
    EXPECT_NEAR(m.boundsMin.x, -2.5f, 1e-5f);
    EXPECT_NEAR(m.boundsMax.x, 2.5f, 1e-5f);
    EXPECT_FALSE(m.isSkinned());
    // Counter-clockwise front faces stay so under the mirror: the winding
    // flips with the scale, and both faces still point along +Z.
    for (const kke::ModelMesh& mesh : m.meshes) {
        EXPECT_GT(faceNormal(mesh, 0).z, 0.99f);
        EXPECT_GT(mesh.vertices[0].normal.z, 0.99f);
    }
    // glTF's UV origin is top-left, like Vulkan's: kept as written.
    EXPECT_NEAR(m.meshes[0].vertices[0].uv.y, 1.0f, 1e-6f);
}

TEST(GltfAsset, GivesFlatNormalsWhenTheFileHasNone) {
    const fs::path dir = tempDir("kke_gltf_flat");
    Builder b;
    addQuadMesh(b, false);
    b.doc["nodes"] = json::array({ { { "mesh", 0 }, { "rotation", { -0.70710678, 0, 0, 0.70710678 } } } }); // -90 deg about X: faces +Y
    const kke::ModelData m = kke::loadModel(b.writeGltf(dir / "flat.gltf").string());
    ASSERT_EQ(m.meshes.size(), 1u);
    EXPECT_EQ(m.meshes[0].vertices.size(), 6u); // three corners per triangle
    for (const kke::ModelVertex& v : m.meshes[0].vertices) EXPECT_GT(v.normal.y, 0.99f);
    ASSERT_EQ(m.materials.size(), 1u);
    EXPECT_EQ(m.materials[0].name, "default");
    EXPECT_EQ(m.meshes[0].material, 0u);
}

TEST(GltfAsset, ReadsAGlbWithItsTexturesInside) {
    const fs::path dir = tempDir("kke_gltf_glb");
    Builder b;
    const std::vector<uint8_t> albedo = png(2, 1, { 255, 0, 0, 255, 0, 255, 0, 255 });
    // Packed metallic-roughness: green = roughness 64, blue = metallic 200.
    const std::vector<uint8_t> packed = png(1, 1, { 0, 64, 200, 255 });
    b.doc["images"] = json::array({ { { "bufferView", b.view(albedo.data(), albedo.size()) }, { "mimeType", "image/png" } },
                                    { { "bufferView", b.view(packed.data(), packed.size()) }, { "mimeType", "image/png" } } });
    b.doc["textures"] = json::array({ { { "source", 0 } }, { { "source", 1 } } });
    b.doc["materials"].push_back({ { "name", "Skin" },
                                   { "pbrMetallicRoughness", { { "baseColorTexture", { { "index", 0 } } }, { "metallicRoughnessTexture", { { "index", 1 } } } } },
                                   { "normalTexture", { { "index", 0 } } } });
    addQuadMesh(b, true, 0);
    b.doc["nodes"] = json::array({ { { "mesh", 0 } } });
    const fs::path file = b.writeGlb(dir / "box.glb");
    const kke::ModelData m = kke::loadModel(file.string());
    ASSERT_EQ(m.materials.size(), 1u);
    const kke::ModelMaterial& mat = m.materials[0];
    EXPECT_EQ(mat.albedoTexture, file.string() + "#image0.png");
    EXPECT_EQ(mat.normalTexture, mat.albedoTexture);
    EXPECT_NEAR(mat.baseColor.r, 1.0f, 1e-6f); // the factor's default, multiplied with the map
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(kke::cooked::readAssetFile(mat.albedoTexture, bytes));
    EXPECT_EQ(bytes, albedo);
    // The engine reads roughness and metallic from the red channel: each
    // gets its own grey image.
    auto grey = [](const std::string& path) {
        std::vector<uint8_t> data;
        EXPECT_TRUE(kke::cooked::readAssetFile(path, data)) << path;
        int w = 0, h = 0, n = 0;
        stbi_uc* px = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &n, 4);
        EXPECT_NE(px, nullptr);
        const int red = px ? px[0] : -1;
        stbi_image_free(px);
        return red;
    };
    EXPECT_EQ(grey(mat.roughnessTexture), 64);
    EXPECT_EQ(grey(mat.metallicTexture), 200);
    EXPECT_NEAR(mat.roughness, 1.0f, 1e-6f);
    EXPECT_NEAR(mat.metallic, 1.0f, 1e-6f);
}

TEST(GltfAsset, FindsAnExternalBinAndImageWithEscapedNames) {
    const fs::path dir = tempDir("kke_gltf_external");
    Builder b;
    const std::vector<uint8_t> albedo = png(1, 1, { 9, 9, 9, 255 });
    fs::create_directories(dir / "tex");
    std::ofstream(dir / "tex" / "my wood.png", std::ios::binary).write(reinterpret_cast<const char*>(albedo.data()), static_cast<std::streamsize>(albedo.size()));
    b.doc["images"] = json::array({ { { "uri", "tex/my%20wood.png" } }, { { "uri", "C:/Artist/Missing/Moved.png" } } });
    b.doc["textures"] = json::array({ { { "source", 0 } }, { { "source", 1 } } });
    b.doc["materials"] = json::array({ { { "pbrMetallicRoughness", { { "baseColorTexture", { { "index", 0 } } } } } },
                                       { { "pbrMetallicRoughness", { { "baseColorTexture", { { "index", 1 } } } } } } });
    addQuadMesh(b, true, 0);
    b.doc["nodes"] = json::array({ { { "mesh", 0 } } });
    std::ofstream(dir / "Textures_moved.png") << "x"; // not the name looked for
    fs::create_directories(dir / "Textures");
    std::ofstream(dir / "Textures" / "moved.png", std::ios::binary).write(reinterpret_cast<const char*>(albedo.data()), static_cast<std::streamsize>(albedo.size()));
    const kke::ModelData m = kke::loadModel(b.writeGltfWithBin(dir / "wood.gltf", "wood.bin").string());
    ASSERT_EQ(m.materials.size(), 2u);
    EXPECT_EQ(fs::path(m.materials[0].albedoTexture).filename(), "my wood.png");
    EXPECT_TRUE(fs::is_regular_file(m.materials[0].albedoTexture));
    // An author's absolute path that isn't here: found by name, any case,
    // in the usual texture folders. (On Windows the file system ignores case,
    // so the name comes back as the author spelled it: compare without case.)
    std::string found = fs::path(m.materials[1].albedoTexture).filename().string();
    std::transform(found.begin(), found.end(), found.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    EXPECT_EQ(found, "moved.png");
    EXPECT_TRUE(fs::is_regular_file(m.materials[1].albedoTexture));
    EXPECT_EQ(m.materials[1].albedoTextureOriginal, "C:/Artist/Missing/Moved.png");
    EXPECT_EQ(m.triangleCount(), 2u);
}

TEST(GltfAsset, ReadsASkinAndSamplesItsAnimation) {
    const fs::path dir = tempDir("kke_gltf_skin");
    Builder b;
    // Two bones up the Y axis (root at 0, tip at 1 m) and a strip of two
    // vertices per bone: the root pair on the root, the tip pair shared.
    const int pos = b.floats({ -0.1f, 0, 0, 0.1f, 0, 0, -0.1f, 1, 0, 0.1f, 1, 0 }, "VEC3", 3, true);
    const int nrm = b.floats({ 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1 }, "VEC3", 3);
    const int joints = b.bytes4({ 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0 });
    const int weights = b.floats({ 1, 0, 0, 0, 1, 0, 0, 0, 0.5f, 0.5f, 0, 0, 0.25f, 0.75f, 0, 0 }, "VEC4", 4);
    b.doc["meshes"].push_back({ { "primitives", json::array({ { { "attributes", { { "POSITION", pos }, { "NORMAL", nrm }, { "JOINTS_0", joints }, { "WEIGHTS_0", weights } } },
                                                                  { "indices", b.indices({ 0, 1, 3, 0, 3, 2 }) } } }) } });
    // Inverse binds: root at the origin (identity), tip 1 m up.
    const int ibm = b.floats({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1, 0, 1 }, "MAT4", 16);
    // The root turns 90 degrees about Z over one second.
    const int times = b.floats({ 0.0f, 1.0f }, "SCALAR", 1, true);
    const float s = std::sqrt(0.5f);
    const int rots = b.floats({ 0, 0, 0, 1, 0, 0, s, s }, "VEC4", 4);
    b.doc["nodes"] = json::array({ { { "name", "Skinned" }, { "mesh", 0 }, { "skin", 0 }, { "translation", { 50, 0, 0 } } }, // ignored for a skinned mesh
                                   { { "name", "Root" }, { "children", { 2 } } },
                                   { { "name", "Tip" }, { "translation", { 0, 1, 0 } } } });
    b.doc["skins"] = json::array({ { { "joints", { 1, 2 } }, { "inverseBindMatrices", ibm } } });
    b.doc["animations"] = json::array({ { { "name", "Bend" },
                                          { "samplers", json::array({ { { "input", times }, { "output", rots }, { "interpolation", "LINEAR" } } }) },
                                          { "channels", json::array({ { { "sampler", 0 }, { "target", { { "node", 1 }, { "path", "rotation" } } } } }) } } });
    b.doc["scenes"] = json::array({ { { "nodes", { 0, 1 } } } });
    kke::ModelLoadOptions opts;
    opts.animationSampleRate = 10.0f;
    const kke::ModelData m = kke::loadModel(b.writeGltf(dir / "arm.gltf").string(), opts);

    ASSERT_EQ(m.bones.size(), 2u);
    EXPECT_EQ(m.bones[0].name, "Root");
    EXPECT_EQ(m.bones[0].parent, -1);
    EXPECT_EQ(m.findBone("Tip"), 1);
    EXPECT_EQ(m.bones[1].parent, 0);
    EXPECT_NEAR(m.bones[1].inverseBind[3][1], -1.0f, 1e-6f);
    ASSERT_EQ(m.meshes.size(), 1u);
    EXPECT_TRUE(m.meshes[0].skinned);
    const kke::ModelVertex& v3 = m.meshes[0].vertices[3];
    EXPECT_EQ(v3.joints[0], 0u);
    EXPECT_EQ(v3.joints[1], 1u);
    EXPECT_NEAR(v3.weights[1], 0.75f, 1e-6f);
    // The skinned node's own translation doesn't move the mesh.
    EXPECT_NEAR(m.boundsMax.x, 0.1f, 1e-5f);
    EXPECT_NEAR(m.boundsMax.y, 1.0f, 1e-5f);

    ASSERT_EQ(m.animations.size(), 1u);
    const kke::ModelAnimation& a = m.animations[0];
    EXPECT_EQ(a.name, "Bend");
    EXPECT_NEAR(a.duration, 1.0f, 1e-6f);
    ASSERT_EQ(a.frames.size(), 11u);
    // Halfway: 45 degrees (slerp), so the tip bone sits at (-sin45, cos45).
    const std::vector<glm::mat4> skin = kke::computeSkinMatrices(m, a.frames[5]);
    glm::vec3 p, n;
    kke::ModelVertex tip;
    tip.position = glm::vec3(0, 1, 0);
    tip.joints = glm::uvec4(1, 0, 0, 0);
    tip.weights = glm::vec4(1, 0, 0, 0);
    kke::skinVertex(tip, skin, p, n);
    EXPECT_NEAR(p.x, -s, 1e-4f);
    EXPECT_NEAR(p.y, s, 1e-4f);
    // The un-animated tip keeps its rest transform every frame.
    EXPECT_NEAR(a.frames[10][1][3][1], 1.0f, 1e-6f);
}

TEST(GltfAsset, SaysWhatACompressedFileNeeds) {
    const fs::path dir = tempDir("kke_gltf_draco");
    Builder b;
    addQuadMesh(b, true);
    b.doc["nodes"] = json::array({ { { "mesh", 0 } } });
    b.doc["extensionsUsed"] = { "KHR_draco_mesh_compression" };
    b.doc["extensionsRequired"] = { "KHR_draco_mesh_compression" };
    const fs::path file = b.writeGltf(dir / "draco.gltf");
    try {
        kke::loadModel(file.string());
        FAIL() << "a Draco file loaded";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("KHR_draco_mesh_compression"), std::string::npos) << e.what();
    }
}

TEST(GltfAsset, RejectsAFileThatIsNotGltf) {
    const fs::path dir = tempDir("kke_gltf_bad");
    std::ofstream(dir / "bad.glb") << "not a model";
    EXPECT_THROW(kke::loadModel((dir / "bad.glb").string()), std::runtime_error);
    EXPECT_THROW(kke::loadModel((dir / "missing.gltf").string()), std::runtime_error);
}
