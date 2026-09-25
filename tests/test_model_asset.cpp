#include "kke/ModelAsset.h"

#include "kke/AssetCatalog.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// A unit cube as 6 quads, split across two materials, with a texture the
// .mtl refers to by a path that doesn't exist here (like Synty's FBX files).
fs::path writeCubeObj(const fs::path& dir, bool withTexture) {
    fs::create_directories(dir / "Textures");
    if (withTexture) std::ofstream(dir / "Textures" / "Atlas_01.png") << "not really a png";
    std::ofstream(dir / "cube.mtl") << "newmtl Red\nKd 1 0 0\n" << (withTexture ? "map_Kd U:/Artist/Work/Atlas_01.png\n" : "")
                                    << "newmtl Blue\nKd 0 0 1\n";
    std::ofstream obj(dir / "cube.obj");
    obj << "mtllib cube.mtl\n"
           "v -0.5 -0.5 -0.5\nv 0.5 -0.5 -0.5\nv 0.5 0.5 -0.5\nv -0.5 0.5 -0.5\n"
           "v -0.5 -0.5 0.5\nv 0.5 -0.5 0.5\nv 0.5 0.5 0.5\nv -0.5 0.5 0.5\n"
           "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
           "usemtl Red\n"
           "f 1/1 4/4 3/3 2/2\nf 5/1 6/2 7/3 8/4\nf 1/1 2/2 6/3 5/4\n"
           "usemtl Blue\n"
           "f 4/1 8/2 7/3 3/4\nf 1/1 5/2 8/3 4/4\nf 2/1 3/2 7/3 6/4\n";
    return dir / "cube.obj";
}

fs::path tempDir(const char* name) {
    fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

} // namespace

TEST(ModelAsset, LoadsObjTriangulatedAndSplitByMaterial) {
    fs::path dir = tempDir("kke_model_cube");
    kke::ModelData m = kke::loadModel(writeCubeObj(dir, true).string());
    EXPECT_EQ(m.triangleCount(), 12u);
    EXPECT_EQ(m.materials.size(), 2u);
    ASSERT_EQ(m.meshes.size(), 2u);
    EXPECT_EQ(m.meshes[0].indices.size(), 18u);
    EXPECT_EQ(m.meshes[1].indices.size(), 18u);
    EXPECT_NE(m.meshes[0].material, m.meshes[1].material);
    EXPECT_FALSE(m.isSkinned());
    EXPECT_TRUE(m.bones.empty());
    for (const auto& mesh : m.meshes) {
        for (uint32_t i : mesh.indices) EXPECT_LT(i, mesh.vertices.size());
        for (const auto& v : mesh.vertices) EXPECT_NEAR(glm::length(v.normal), 1.0f, 1e-3f); // generated normals
    }
    EXPECT_NEAR(m.boundsMin.x, -0.5f, 1e-4f);
    EXPECT_NEAR(m.boundsMax.y, 0.5f, 1e-4f);
    fs::remove_all(dir);
}

TEST(ModelAsset, ResolvesArtistTexturePathByFileName) {
    fs::path dir = tempDir("kke_model_tex");
    kke::ModelData m = kke::loadModel(writeCubeObj(dir, true).string());
    const kke::ModelMaterial* red = nullptr;
    for (const auto& mat : m.materials) if (mat.name == "Red") red = &mat;
    ASSERT_NE(red, nullptr);
    EXPECT_EQ(fs::path(red->albedoTexture).filename().string(), "Atlas_01.png");
    EXPECT_TRUE(fs::exists(red->albedoTexture));
    EXPECT_NE(red->albedoTextureOriginal.find("U:/Artist"), std::string::npos);
    EXPECT_EQ(red->baseColor, glm::vec3(1.0f)); // textured: color becomes a neutral tint
    fs::remove_all(dir);
}

TEST(ModelAsset, AuthoringFormatResolvesToShippedImage) {
    // The .mtl says .psd (what the artist painted in); the pack ships .png.
    fs::path dir = tempDir("kke_model_psd");
    fs::path obj = writeCubeObj(dir, true);
    {
        std::ifstream in(dir / "cube.mtl");
        std::string mtl((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        mtl.replace(mtl.find("Atlas_01.png"), 12, "Atlas_01.psd");
        std::ofstream(dir / "cube.mtl") << mtl;
    }
    kke::ModelData m = kke::loadModel(obj.string());
    bool resolved = false;
    for (const auto& mat : m.materials) resolved |= fs::path(mat.albedoTexture).filename() == "Atlas_01.png";
    EXPECT_TRUE(resolved);
    fs::remove_all(dir);
}

TEST(ModelAsset, UnresolvableTextureUsesFallback) {
    fs::path dir = tempDir("kke_model_fallback");
    fs::path obj = writeCubeObj(dir, true);
    fs::remove(dir / "Textures" / "Atlas_01.png");
    kke::ModelLoadOptions opts;
    opts.fallbackTexture = "fallback.png";
    kke::ModelData m = kke::loadModel(obj.string(), opts);
    bool sawFallback = false;
    for (const auto& mat : m.materials) sawFallback |= (mat.albedoTexture == "fallback.png");
    EXPECT_TRUE(sawFallback);
    fs::remove_all(dir);
}

TEST(ModelAsset, TextureFoundInExtraSearchPath) {
    fs::path dir = tempDir("kke_model_search");
    fs::path obj = writeCubeObj(dir, true);
    fs::path elsewhere = tempDir("kke_model_search_tex");
    fs::rename(dir / "Textures" / "Atlas_01.png", elsewhere / "atlas_01.PNG"); // also checks case-insensitivity
    kke::ModelLoadOptions opts;
    opts.textureSearchPaths = { elsewhere.string() };
    kke::ModelData m = kke::loadModel(obj.string(), opts);
    bool found = false;
    for (const auto& mat : m.materials) found |= fs::path(mat.albedoTexture).parent_path() == elsewhere;
    EXPECT_TRUE(found);
    fs::remove_all(dir);
    fs::remove_all(elsewhere);
}

TEST(ModelAsset, MissingFileThrowsWithPath) {
    try {
        kke::loadModel("/definitely/not/here.fbx");
        FAIL() << "expected an exception";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("/definitely/not/here.fbx"), std::string::npos);
    }
}

// A two-bone chain along +Y: root at the origin, child 1m up. One vertex
// fully on the child, one split 50/50.
kke::ModelData twoBoneModel() {
    kke::ModelData m;
    kke::ModelBone root;
    root.name = "root";
    kke::ModelBone child;
    child.name = "child";
    child.parent = 0;
    child.localRest = glm::translate(glm::mat4(1.0f), glm::vec3(0, 1, 0));
    child.inverseBind = glm::inverse(child.localRest);
    m.bones = { root, child };
    kke::ModelMesh mesh;
    mesh.skinned = true;
    kke::ModelVertex tip;
    tip.position = glm::vec3(0, 2, 0);
    tip.normal = glm::vec3(1, 0, 0);
    tip.joints = glm::uvec4(1, 0, 0, 0);
    tip.weights = glm::vec4(1, 0, 0, 0);
    kke::ModelVertex mid = tip;
    mid.position = glm::vec3(0, 1, 0);
    mid.joints = glm::uvec4(0, 1, 0, 0);
    mid.weights = glm::vec4(0.5f, 0.5f, 0, 0);
    mesh.vertices = { tip, mid };
    mesh.indices = { 0, 1, 0 };
    m.meshes = { mesh };
    return m;
}

TEST(ModelAsset, RestPoseSkinningIsIdentity) {
    kke::ModelData m = twoBoneModel();
    std::vector<glm::mat4> locals = { m.bones[0].localRest, m.bones[1].localRest };
    auto skin = kke::computeSkinMatrices(m, locals);
    glm::vec3 p, n;
    kke::skinVertex(m.meshes[0].vertices[0], skin, p, n);
    EXPECT_NEAR(glm::distance(p, glm::vec3(0, 2, 0)), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(n, glm::vec3(1, 0, 0)), 0.0f, 1e-5f);
    auto rest = kke::computeRestPose(m);
    EXPECT_NEAR(rest[1][3][1], 1.0f, 1e-6f);
    EXPECT_TRUE(m.isSkinned());
    EXPECT_EQ(m.findBone("child"), 1);
    EXPECT_EQ(m.findBone("nope"), -1);
}

TEST(ModelAsset, RotatingChildBoneMovesOnlyWeightedVertices) {
    kke::ModelData m = twoBoneModel();
    // Bend the child 90 degrees about Z: the tip (1m above the joint)
    // swings to (-1, 1, 0); the 50/50 vertex sits at the joint and stays.
    glm::mat4 bent = m.bones[1].localRest * glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));
    auto skin = kke::computeSkinMatrices(m, { m.bones[0].localRest, bent });
    glm::vec3 p, n;
    kke::skinVertex(m.meshes[0].vertices[0], skin, p, n);
    EXPECT_NEAR(glm::distance(p, glm::vec3(-1, 1, 0)), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(n, glm::vec3(0, 1, 0)), 0.0f, 1e-5f);
    kke::skinVertex(m.meshes[0].vertices[1], skin, p, n);
    EXPECT_NEAR(glm::distance(p, glm::vec3(0, 1, 0)), 0.0f, 1e-5f);
}

// Only runs where the Synty Prototype pack has been dropped into
// assets/synty/ (it is never committed — paid, licensed content).
TEST(ModelAsset, SyntyCharacterIfInstalled) {
    auto catalog = kke::AssetCatalog::scan((std::filesystem::path(KKE_SOURCE_DIR) / "assets/synty").string());
    const kke::CatalogAsset* dummy = catalog.find("SK_Character_Dummy_Male_01");
    if (!dummy) GTEST_SKIP() << "Synty Prototype pack not installed in assets/synty";
    std::filesystem::path file = dummy->path;
    kke::ModelData m = kke::loadModel(file.string());
    EXPECT_TRUE(m.isSkinned());
    EXPECT_GE(m.bones.size(), 48u);
    EXPECT_GE(m.findBone("Pelvis"), 0);
    EXPECT_GE(m.findBone("Hand_L"), 0);
    float height = m.boundsMax.y - m.boundsMin.y;
    EXPECT_GT(height, 1.5f); // meters, Y up: a human, not a 180m giant or lying down
    EXPECT_LT(height, 2.2f);
    EXPECT_NEAR(m.boundsMin.y, 0.0f, 0.1f); // feet on the ground
    for (const auto& bone : m.bones) {
        if (bone.parent >= 0) EXPECT_LT(bone.parent, &bone - m.bones.data()); // parents first
    }
}
