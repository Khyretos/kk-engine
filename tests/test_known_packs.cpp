#include "kke/AssetCatalog.h"
#include "kke/KnownPacks.h"
#include "kke/SpriteCatalog.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

fs::path freshDir(const char* name) {
    fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

} // namespace

TEST(KnownPacks, KeyIgnoresDownloadSuffixVersionCaseAndPunctuation) {
    EXPECT_EQ(kke::packKey("POLYGON_Street_Racer"), "polygonstreetracer");
    EXPECT_EQ(kke::packKey("POLYGON_Street_Racer_SourceFiles_v3"), "polygonstreetracer");
    EXPECT_EQ(kke::packKey("PolygonStreetRacer"), "polygonstreetracer");
    EXPECT_EQ(kke::packKey("polygon street racer v2"), "polygonstreetracer");
    EXPECT_EQ(kke::packKey("POLYGON_Town (1)"), "polygontown");
    EXPECT_EQ(kke::packKey("INTERFACE_Apocalypse_HUD_Source_Sprites_v3"), "interfaceapocalypsehud");
    EXPECT_TRUE(kke::samePack("Farm Animals Animated  by Quaternius", "Farm_Animals_Animated_by_Quaternius"));
    // Numbers that are part of the name stay: UAL 1 and 2 are different packs.
    EXPECT_FALSE(kke::samePack("Universal Animation Library", "Universal Animation Library 2"));
    EXPECT_FALSE(kke::samePack("POLYGON_City", "POLYGON_City_Characters"));
    EXPECT_TRUE(kke::samePack("POLYGON_Dungeon_Pack", "POLYGON_Dungeon")); // as the Dungeon Pack unzips
    EXPECT_TRUE(kke::samePack("POLYGON_Pirate_Pack", "PolygonPirate"));
}

TEST(KnownPacks, EveryKnownPackHasAKindAFolderAndAUse) {
    for (const kke::KnownPack& p : kke::knownPacks()) {
        EXPECT_FALSE(p.kind.empty()) << p.name;
        EXPECT_TRUE(p.folder == "assets/synty" || p.folder == "assets/sprites") << p.name;
        EXPECT_FALSE(p.usedFor.empty()) << p.name;
        EXPECT_EQ(kke::knownPack(p.name), &p) << p.name; // names don't collide
    }
}

TEST(KnownPacks, FolderIsRecognisedByNameOrByItsFiles) {
    const fs::path root = freshDir("kke_known_packs_identify");
    touch(root / "POLYGON_Street_Racer_SourceFiles_v3/FBX/SM_Veh_Car_01.fbx");
    touch(root / "my racing stuff/Textures/Vehicles/PolygonStreetRacer_Veh_Tex_01_Race_Purple.png");
    touch(root / "my racing stuff/FBX/SM_Veh_Car_01.fbx");
    touch(root / "random models/SM_Thing.fbx");
    touch(root / "POLYGON_Casino/PolygonStreetRacer_Veh_Tex_01_Race_Purple.png"); // named like a pack: taken by its name

    auto id = kke::identifyPackFolder((root / "POLYGON_Street_Racer_SourceFiles_v3").string());
    EXPECT_EQ(id.name, "POLYGON_Street_Racer");
    EXPECT_EQ(id.how, kke::PackIdentity::How::Name);
    id = kke::identifyPackFolder((root / "my racing stuff").string());
    EXPECT_EQ(id.name, "POLYGON_Street_Racer");
    EXPECT_EQ(id.how, kke::PackIdentity::How::Contents);
    id = kke::identifyPackFolder((root / "random models").string());
    EXPECT_EQ(id.name, "random models");
    EXPECT_EQ(id.how, kke::PackIdentity::How::Unknown);
    id = kke::identifyPackFolder((root / "POLYGON_Casino").string());
    EXPECT_EQ(id.name, "POLYGON_Casino");
    EXPECT_EQ(id.how, kke::PackIdentity::How::Unknown);
}

TEST(KnownPacks, CatalogNamesRenamedPacksAndOnlyPacksFindsThem) {
    const fs::path root = freshDir("kke_known_packs_catalog");
    touch(root / "Street Racer/Textures/PolygonStreetRacer_Veh_Tex_01_Race_Purple.png");
    touch(root / "Street Racer/FBX/SM_Veh_Car_Muscle_01.fbx");
    touch(root / "Synty/POLYGON_Nature_Source_Files_v2/Models/SM_Env_Tree_01.fbx"); // packs kept in a folder of their own
    touch(root / "POLYGON_Town/FBX/SM_Bld_Shop_01.fbx");

    kke::AssetCatalog all = kke::AssetCatalog::scan(root.string());
    ASSERT_NE(all.pack("POLYGON_Street_Racer"), nullptr);
    ASSERT_NE(all.pack("POLYGON_Nature"), nullptr);
    EXPECT_NE(all.pack("POLYGON_Nature_Source_Files"), nullptr); // as scenes save it
    EXPECT_NE(all.find("SM_Veh_Car_Muscle_01", { "POLYGON_Street_Racer" }), nullptr);

    kke::CatalogScanOptions only;
    only.onlyPacks = { "POLYGON_Street_Racer", "POLYGON_Nature" };
    kke::AssetCatalog some = kke::AssetCatalog::scan(root.string(), only);
    EXPECT_NE(some.pack("POLYGON_Street_Racer"), nullptr);
    EXPECT_NE(some.pack("POLYGON_Nature"), nullptr);
    EXPECT_EQ(some.pack("POLYGON_Town"), nullptr);
}

TEST(KnownPacks, FindsFilesInAPackWhateverItsFolderIsCalled) {
    const fs::path root = freshDir("kke_known_packs_files");
    touch(root / "UAL_2_unzipped/Unity/UAL2.fbx");
    touch(root / "Goblins/Assets/Synty/SidekickCharacters/Characters/GoblinFighters/GoblinFighter_01.sk");
    EXPECT_EQ(kke::findPackFolder(root.string(), "Universal Animation Library 2"), (root / "UAL_2_unzipped").string());
    EXPECT_EQ(kke::findFileInPack(root.string(), "Universal Animation Library 2", "UAL2.fbx"), (root / "UAL_2_unzipped/Unity/UAL2.fbx").string());
    EXPECT_EQ(kke::findPackFolder(root.string(), "SIDEKICK_Goblin_Fighters"), (root / "Goblins").string());
    EXPECT_EQ(kke::findPackFolder(root.string(), "POLYGON_Town"), "");
    EXPECT_EQ(kke::findFileInPack(root.string(), "Universal Animation Library 2", "UAL3.fbx"), "");
}

TEST(KnownPacks, AnalysisSaysWhatKindOfPackAFolderIs) {
    const fs::path root = freshDir("kke_known_packs_kinds");
    touch(root / "chars/SK_Chr_A.fbx");
    touch(root / "chars/SK_Chr_B.fbx");
    touch(root / "chars/Textures/atlas.png");
    touch(root / "anims/Animations/A_Run.fbx");
    touch(root / "anims/Animations/A_Walk.fbx");
    touch(root / "props/SM_Prop_Crate.fbx");
    touch(root / "ui/Sprites/ICON_Heart.png");
    touch(root / "ui/Sprites/SPR_Frame.png");
    touch(root / "music/track1.ogg");
    touch(root / "music/cover.png");
    touch(root / "music/track2.wav");
    touch(root / "unreal/Content/a.uasset");
    EXPECT_EQ(kke::analyzePackFolder((root / "chars").string()).kind(), "Characters");
    EXPECT_EQ(kke::analyzePackFolder((root / "anims").string()).kind(), "Animations");
    EXPECT_EQ(kke::analyzePackFolder((root / "props").string()).kind(), "Models");
    EXPECT_EQ(kke::analyzePackFolder((root / "ui").string()).kind(), "Sprites");
    EXPECT_EQ(kke::analyzePackFolder((root / "ui").string()).folder(), "assets/sprites");
    EXPECT_EQ(kke::analyzePackFolder((root / "music").string()).kind(), "Audio");
    EXPECT_EQ(kke::analyzePackFolder((root / "music").string()).folder(), "");
    EXPECT_EQ(kke::analyzePackFolder((root / "unreal").string()).kind(), "Unreal/Unity project");
    EXPECT_EQ(kke::analyzePackFolder((root / "nothing").string()).kind(), "Empty");
}

TEST(SpriteCatalog, FindsSpritesByNameInAnyLayout) {
    const fs::path root = freshDir("kke_sprite_catalog");
    touch(root / "INTERFACE_Apocalypse_HUD_Source_Sprites_v3/Source_Sprites/Sprites/Icons_Map/ICON_Map_Fire.png");
    touch(root / "INTERFACE_Apocalypse_HUD_Source_Sprites_v3/Source_Sprites/Sprites/.mayaSwatches/ICON_Map_Fire.png");
    touch(root / "my_icons/ICON_Map_Fire.png");
    touch(root / "my_icons/heart.png");
    touch(root / "my_icons/readme.txt");
    touch(root / "coin.png");

    const kke::SpriteCatalog c = kke::SpriteCatalog::scan(root.string());
    EXPECT_EQ(c.sprites.size(), 4u);
    const kke::Sprite* fire = c.find("ICON_Map_Fire", "INTERFACE_Apocalypse_HUD");
    ASSERT_NE(fire, nullptr);
    EXPECT_EQ(fire->pack, "INTERFACE_Apocalypse_HUD");
    EXPECT_EQ(fire->category, "Icons_Map");
    ASSERT_NE(c.find("ICON_Map_Fire", "my_icons"), nullptr);
    EXPECT_EQ(c.find("ICON_Map_Fire", "my_icons")->pack, "my_icons");
    ASSERT_NE(c.find("coin"), nullptr);
    EXPECT_EQ(c.find("coin")->pack, "sprites");
    EXPECT_EQ(c.find("readme"), nullptr);
    EXPECT_EQ(kke::SpriteCatalog::scan((root / "missing").string()).sprites.size(), 0u);
    EXPECT_TRUE(kke::isSpriteReference("sprite:coin"));
    EXPECT_FALSE(kke::isSpriteReference("ui/coin.png"));
}
