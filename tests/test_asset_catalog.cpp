#include "kke/AssetCatalog.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include <filesystem>
#include <fstream>

namespace {
// setenv/unsetenv are POSIX; Windows has _putenv_s ("" removes it).
void setEnv(const char* name, const std::string& value) {
#if defined(_WIN32)
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name);
    else setenv(name, value.c_str(), 1);
#endif
}
} // namespace

namespace fs = std::filesystem;

namespace {
void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

// Mirrors the layout the user actually got when extracting their packs
// (see HARDWARE_TESTS / session notes): packs side by side, no
// _SourceFiles, Town keeps meshes in "FBX", both ship OBJ duplicates.
fs::path makeUserLayout() {
    fs::path root = fs::temp_directory_path() / "kke_catalog_user";
    fs::remove_all(root);
    touch(root / "POLYGON_Prototype/Characters/SK_Character_Dummy_Male_01.fbx");
    touch(root / "POLYGON_Prototype/Characters/SM_Chr_Attach_Male_Hair_01.fbx");
    touch(root / "POLYGON_Prototype/StaticMeshes/SM_Prop_Crate_01.fbx");
    touch(root / "POLYGON_Prototype/StaticMeshes/SM_Buildings_Wall_5x3_01.fbx");
    touch(root / "POLYGON_Prototype/StaticMeshes/SM_Generic_Tree_01.fbx");
    touch(root / "POLYGON_Prototype/OBJ/SM_Prop_Crate_01.obj");      // duplicate of the FBX
    touch(root / "POLYGON_Prototype/OBJ/SM_Prop_OnlyObj_01.obj");     // OBJ-only asset
    touch(root / "POLYGON_Prototype/Demonstration.fbx");
    touch(root / "POLYGON_Prototype/Textures/PolygonPrototype_Texture_01_Emission.png");
    touch(root / "POLYGON_Prototype/Textures/PolygonPrototype_Texture_01.png");
    touch(root / "POLYGON_Prototype/Textures/PolygonPrototype_Texture_02.png");
    touch(root / "POLYGON_Prototype/Textures/PolygonPrototype_Texture_Grid_01.png");
    touch(root / "POLYGON_Town/Characters/SK_Character_Father_01.fbx");
    touch(root / "POLYGON_Town/FBX/SM_Bld_House_01.fbx");
    touch(root / "POLYGON_Town/FBX/SM_Veh_Car_01.fbx");
    touch(root / "POLYGON_Town/OBJ/SM_Bld_House_01.obj");
    touch(root / "POLYGON_Town/Textures/PolygonTown_Texture_01_A.png");
    touch(root / "POLYGON_Town/Textures/PolygonTown_Window_Normal.png");
    touch(root / "synty_assets_folder.txt");
    return root;
}
} // namespace

TEST(AssetCatalog, FindsSideBySidePacks) {
    fs::path root = makeUserLayout();
    auto c = kke::AssetCatalog::scan(root.string());
    ASSERT_EQ(c.packs.size(), 2u);
    EXPECT_EQ(c.packs[0].name, "POLYGON_Prototype");
    EXPECT_EQ(c.packs[1].name, "POLYGON_Town");
    EXPECT_EQ(c.packs[0].assetCount, 7u); // 5 fbx + 1 obj-only + Demonstration
    EXPECT_EQ(c.packs[1].assetCount, 3u);
    fs::remove_all(root);
}

TEST(AssetCatalog, PrefersFbxOverObjDuplicates) {
    fs::path root = makeUserLayout();
    auto c = kke::AssetCatalog::scan(root.string());
    auto crates = c.filter("POLYGON_Prototype", "", "crate");
    ASSERT_EQ(crates.size(), 1u);
    EXPECT_EQ(fs::path(crates[0]->path).extension(), ".fbx");
    auto objOnly = c.filter("", "", "OnlyObj");
    ASSERT_EQ(objOnly.size(), 1u);
    EXPECT_EQ(fs::path(objOnly[0]->path).extension(), ".obj");
    fs::remove_all(root);
}

TEST(AssetCatalog, CategoriesFromSyntyPrefixes) {
    fs::path root = makeUserLayout();
    auto c = kke::AssetCatalog::scan(root.string());
    auto cats = c.categories();
    std::vector<std::string> expected = { "Characters", "Character parts", "Buildings", "Props", "Environment", "Vehicles", "Scenes" };
    EXPECT_EQ(cats, expected);
    auto chars = c.filter("", "Characters", "");
    ASSERT_EQ(chars.size(), 2u);
    EXPECT_TRUE(chars[0]->skinned);
    EXPECT_EQ(c.filter("POLYGON_Town", "Vehicles", "").size(), 1u);
    EXPECT_EQ(kke::categoryForAssetName("SM_Wep_Sword_01"), "Weapons");
    EXPECT_EQ(kke::categoryForAssetName("SM_Env_Rock_03"), "Environment");
    EXPECT_EQ(kke::categoryForAssetName("Banana"), "Other");
    fs::remove_all(root);
}

TEST(AssetCatalog, PicksMainTextureNotEmissionOrNormal) {
    fs::path root = makeUserLayout();
    auto c = kke::AssetCatalog::scan(root.string());
    EXPECT_EQ(fs::path(c.pack("POLYGON_Prototype")->defaultTexture).filename(), "PolygonPrototype_Texture_01.png");
    EXPECT_EQ(fs::path(c.pack("POLYGON_Town")->defaultTexture).filename(), "PolygonTown_Texture_01_A.png");
    // Variants and overlays: helpers (emission) excluded, grids separate.
    const kke::CatalogPack* proto = c.pack("POLYGON_Prototype");
    ASSERT_EQ(proto->textureVariants.size(), 2u);
    EXPECT_EQ(fs::path(proto->textureVariants[1]).filename(), "PolygonPrototype_Texture_02.png");
    ASSERT_EQ(proto->overlayTextures.size(), 1u);
    EXPECT_EQ(fs::path(proto->overlayTextures[0]).filename(), "PolygonPrototype_Texture_Grid_01.png");
    EXPECT_EQ(c.pack("POLYGON_Town")->textureDirs.size(), 1u);
    EXPECT_EQ(c.pack("nope"), nullptr);
    fs::remove_all(root);
}

TEST(AssetCatalog, SinglePackAsRootAndSourceFilesLayout) {
    fs::path root = fs::temp_directory_path() / "kke_catalog_single";
    fs::remove_all(root);
    touch(root / "_SourceFiles/Characters/SK_Guy_01.fbx");
    touch(root / "_SourceFiles/StaticMeshes/SM_Prop_Box_01.fbx");
    touch(root / "_SourceFiles/Textures/Pack_Texture_01.png");
    auto c = kke::AssetCatalog::scan(root.string());
    ASSERT_EQ(c.packs.size(), 1u);
    EXPECT_EQ(c.packs[0].name, "kke_catalog_single");
    EXPECT_EQ(c.assets.size(), 2u);
    EXPECT_FALSE(c.packs[0].defaultTexture.empty());
    fs::remove_all(root);
}

TEST(AssetCatalog, MissingOrEmptyFolderIsEmptyNotAnError) {
    EXPECT_TRUE(kke::AssetCatalog::scan("/no/such/folder").packs.empty());
    fs::path root = fs::temp_directory_path() / "kke_catalog_empty";
    fs::remove_all(root);
    fs::create_directories(root / "Stuff");
    EXPECT_TRUE(kke::AssetCatalog::scan(root.string()).assets.empty());
    fs::remove_all(root);
}

TEST(AssetCatalog, RealSyntyPackIfInstalled) {
    fs::path root = fs::path(KKE_SOURCE_DIR) / "assets/synty";
    if (!fs::exists(root)) GTEST_SKIP() << "no packs in assets/synty";
    auto c = kke::AssetCatalog::scan(root.string());
    ASSERT_FALSE(c.packs.empty());
    EXPECT_GT(c.assets.size(), 400u);
    EXPECT_FALSE(c.filter("", "Characters", "").empty());
    for (const auto& p : c.packs) EXPECT_FALSE(p.defaultTexture.empty()) << p.name;
}

TEST(AssetCatalog, FindAssetFolderSearchesEnvThenParents) {
    fs::path base = fs::temp_directory_path() / "kke_find_root";
    fs::remove_all(base);
    fs::create_directories(base / "assets/packs");
    fs::create_directories(base / "build/bin");
    std::vector<std::string> searched;
    // from the executable folder two levels down
    std::string found = kke::findAssetFolder("assets/packs", {}, (base / "build/bin").string(), &searched);
    EXPECT_EQ(fs::path(found), fs::weakly_canonical(base / "assets/packs"));
    EXPECT_FALSE(searched.empty());
    // env var wins
    fs::create_directories(base / "elsewhere");
    setEnv("KKE_TEST_ASSETS", (base / "elsewhere").string());
    EXPECT_EQ(fs::path(kke::findAssetFolder("assets/packs", { "KKE_TEST_ASSETS" }, (base / "build/bin").string())),
              fs::weakly_canonical(base / "elsewhere"));
    setEnv("KKE_TEST_ASSETS", "");
    EXPECT_EQ(kke::findAssetFolder("definitely/not/here", {}, (base / "build/bin").string()), "");
    fs::remove_all(base);
}
