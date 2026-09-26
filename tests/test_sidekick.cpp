#include "kke/MeshLod.h"
#include "kke/Sidekick.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

// A skinned triangle on a two-bone chain (root, child), with its own
// copy of the skeleton, as each Sidekick part FBX has.
kke::ModelData part(const std::string& extraBone, uint32_t joint, const glm::mat4& geometry = glm::mat4(1.0f)) {
    kke::ModelData m;
    m.bones.push_back({ "root", -1, glm::mat4(1.0f), glm::mat4(1.0f) * geometry });
    m.bones.push_back({ "spine", 0, glm::mat4(1.0f), glm::mat4(1.0f) * geometry });
    if (!extraBone.empty()) m.bones.push_back({ extraBone, 1, glm::mat4(1.0f), glm::mat4(1.0f) * geometry });
    kke::ModelMesh mesh;
    mesh.skinned = true;
    for (int i = 0; i < 3; ++i) {
        kke::ModelVertex v;
        v.position = glm::vec3(static_cast<float>(i), 1.0f, 0.0f);
        v.joints = glm::uvec4(joint, 0, 0, 0);
        v.weights = glm::vec4(1, 0, 0, 0);
        mesh.vertices.push_back(v);
    }
    mesh.indices = { 0, 1, 2 };
    m.meshes.push_back(mesh);
    m.materials.push_back({ "COLOR", glm::vec3(1.0f), 0.0f, 0.8f, "", "" });
    return m;
}

TEST(Sidekick, MergeUnifiesBonesByNameAndRemapsJoints) {
    const kke::ModelData a = part("", 1);
    const kke::ModelData b = part("ear_l", 2); // a bone only this part has
    const kke::ModelData merged = kke::mergeSkinnedModels({ a, b });
    ASSERT_EQ(merged.bones.size(), 3u);
    EXPECT_EQ(merged.findBone("ear_l"), 2);
    EXPECT_EQ(merged.bones[2].parent, 1);
    ASSERT_EQ(merged.meshes.size(), 2u);
    EXPECT_EQ(merged.meshes[1].vertices[0].joints.x, 2u);
    EXPECT_EQ(merged.materials.size(), 1u); // same name and texture: one material
    EXPECT_EQ(merged.meshes[1].material, 0u);

    kke::ModelData joined = merged;
    kke::joinMeshesByMaterial(joined);
    ASSERT_EQ(joined.meshes.size(), 1u);
    EXPECT_EQ(joined.meshes[0].vertices.size(), 6u);
    EXPECT_EQ(joined.meshes[0].indices[3], 3u);
}

TEST(Sidekick, PartInAnotherGeometrySpaceIsMovedIntoTheFirst) {
    const glm::mat4 g = glm::mat4(1.0f) + glm::mat4(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5f, 0, 0); // translate y +0.5
    const kke::ModelData a = part("", 1);
    const kke::ModelData b = part("", 1, g);
    const kke::ModelData merged = kke::mergeSkinnedModels({ a, b });
    // Same bone, so the skinned result must match: b's vertices shift by g.
    EXPECT_NEAR(merged.meshes[1].vertices[0].position.y, 1.5f, 1e-5f);
}

TEST(Sidekick, ReadsASkFileAndFindsItsParts) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "kke_sidekick_test";
    fs::remove_all(root);
    fs::create_directories(root / "Resources" / "Meshes" / "Outfits");
    fs::create_directories(root / "Characters" / "Gob" / "Textures");
    std::ofstream(root / "Resources" / "Meshes" / "Outfits" / "SK_PART_A.fbx") << "x";
    std::ofstream(root / "Characters" / "Gob" / "Textures" / "T_GobColorMap.png") << "x";
    std::ofstream(root / "Characters" / "Gob" / "Gob.sk") << "Name: Gob\nSpecies: 2\nParts:\n- Name: SK_PART_A\n  PartType: Torso\n  PartVersion: 1\n"
                                                             "- Name: SK_PART_B\n  PartType: Head\n  PartVersion: 1\n";
    const kke::SidekickCharacter c = kke::readSidekickCharacter((root / "Characters" / "Gob" / "Gob.sk").string());
    EXPECT_EQ(c.name, "Gob");
    ASSERT_EQ(c.parts.size(), 2u);
    EXPECT_EQ(c.parts[0].type, "Torso");
    EXPECT_FALSE(c.parts[0].file.empty());
    EXPECT_TRUE(c.parts[1].file.empty());
    EXPECT_FALSE(c.colorMap.empty());
    fs::remove_all(root);
}

// With the Synty pack in KKE_ASSETS_DIR: a real goblin comes together.
TEST(Sidekick, RealGoblinFighter) {
    const char* dir = std::getenv("KKE_ASSETS_DIR");
    if (!dir) GTEST_SKIP() << "KKE_ASSETS_DIR not set";
    const std::filesystem::path sk = std::filesystem::path(dir) /
                                     "SIDEKICK_Goblin_Fighters/Assets/Synty/SidekickCharacters/Characters/GoblinFighters/GoblinFighter_01/GoblinFighter_01.sk";
    if (!std::filesystem::exists(sk)) GTEST_SKIP() << "Goblin Fighters pack not found";
    const kke::SidekickCharacter c = kke::readSidekickCharacter(sk.string());
    std::vector<std::string> missing;
    const kke::ModelData m = kke::loadSidekickCharacter(c, {}, &missing);
    EXPECT_TRUE(missing.empty()) << missing.front();
    EXPECT_TRUE(m.isSkinned());
    EXPECT_EQ(m.meshes.size(), 1u);
    EXPECT_GE(m.findBone("pelvis"), 0);
    EXPECT_GT(m.boundsMax.y, 1.0f); // a goblin stands over a metre tall
    EXPECT_LT(m.boundsMax.y, 2.5f); // ears and hair included

    // A crowd copy (what the goblin horde draws): a fraction of the triangles.
    kke::SimplifyOptions o;
    o.acrossSeams = true;
    o.prune = true;
    const kke::ModelData lod = kke::simplifyModel(m, 0.15f, o);
    printf("goblin: %zu triangles, %zu vertices; crowd copy %zu triangles, %zu vertices\n", m.triangleCount(), m.meshes[0].vertices.size(),
           lod.triangleCount(), lod.meshes[0].vertices.size());
    EXPECT_LT(lod.triangleCount(), m.triangleCount() / 4);
}

} // namespace
