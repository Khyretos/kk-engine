#include "kke/SceneFile.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

namespace {
const char* kScene = R"({
  // comments are allowed
  "format": "kke.scene", "version": 1,
  "name": "Test", "packs": ["PolygonTown_Source_Files"],
  "spawn": { "position": [1, 2, 3], "yaw": 90 },
  "objects": [
    { "asset": "SM_Prop_Barrel_01", "position": [4, 0, 0], "collision": "box" },
    { "asset": "SM_Env_Grass_01", "grid": { "count": [3, 2], "step": [5, 5] }, "collision": "none" },
    { "asset": "SM_Prop_Barrel_01", "position": [0, 0, 4], "yaw": 90, "scale": 2, "pivot": true }
  ]
})";
glm::vec3 apply(const glm::mat4& m, glm::vec3 p) { return glm::vec3(m * glm::vec4(p, 1.0f)); }
} // namespace

TEST(SceneFile, ParsesObjectsGridsAndSpawn) {
    kke::SceneFile s = kke::SceneFile::parse(kScene);
    EXPECT_EQ(s.name, "Test");
    ASSERT_EQ(s.objects.size(), 3u);
    EXPECT_EQ(s.spawn, glm::vec3(1, 2, 3));
    EXPECT_FLOAT_EQ(s.spawnYaw, 90.0f);
    EXPECT_EQ(s.objects[0].collision, kke::SceneObject::Collision::Box);
    EXPECT_EQ(s.objects[1].collision, kke::SceneObject::Collision::None);
    EXPECT_EQ(s.objects[2].collision, kke::SceneObject::Collision::Mesh); // default
    EXPECT_EQ(s.objects[2].scale, glm::vec3(2.0f));
    EXPECT_EQ(s.instanceCount(), 8u);
    auto used = s.assetsUsed();
    EXPECT_EQ(used["SM_Prop_Barrel_01"], 2);
    EXPECT_EQ(used["SM_Env_Grass_01"], 6);
}

TEST(SceneFile, PlacesByBoundsBottomCentreUnlessPivot) {
    kke::SceneFile s = kke::SceneFile::parse(kScene);
    // A barrel modelled off-centre and below zero: its bottom centre lands
    // on the given position.
    const glm::vec3 bmin(1, -0.5f, 1), bmax(2, 0.7f, 2);
    glm::mat4 m = kke::SceneFile::placement(s.objects[0], bmin, bmax);
    glm::vec3 bottomCentre = apply(m, glm::vec3(1.5f, -0.5f, 1.5f));
    EXPECT_NEAR(glm::distance(bottomCentre, glm::vec3(4, 0, 0)), 0.0f, 1e-5f);
    // Pivot: origin goes to the position, scaled and turned.
    glm::mat4 p = kke::SceneFile::placement(s.objects[2], bmin, bmax);
    EXPECT_NEAR(glm::distance(apply(p, glm::vec3(0)), glm::vec3(0, 0, 4)), 0.0f, 1e-5f);
    EXPECT_NEAR(glm::distance(apply(p, glm::vec3(1, 0, 0)), glm::vec3(0, 0, 2)), 0.0f, 1e-4f); // +X -> -Z, x2
    // Grid cells step along X and Z.
    glm::mat4 g = kke::SceneFile::placement(s.objects[1], glm::vec3(0), glm::vec3(5, 0.1f, 5), { 2, 1 });
    EXPECT_NEAR(glm::distance(apply(g, glm::vec3(2.5f, 0, 2.5f)), glm::vec3(10, 0, 5)), 0.0f, 1e-4f);
}

TEST(SceneFile, ErrorsNameTheProblem) {
    auto msg = [](const std::string& json) {
        try { kke::SceneFile::parse(json, "x.json"); } catch (const std::runtime_error& e) { return std::string(e.what()); }
        return std::string();
    };
    EXPECT_NE(msg("{").find("not valid JSON"), std::string::npos);
    EXPECT_NE(msg(R"({"format":"other"})").find("format"), std::string::npos);
    EXPECT_NE(msg(R"({"format":"kke.scene","version":1,"objects":[{}]})").find("objects[0]"), std::string::npos);
    EXPECT_NE(msg(R"({"format":"kke.scene","version":1,"objects":[{"asset":"a","collision":"soft"}]})").find("soft"), std::string::npos);
}

// Every scene shipped in scenes/ parses.
TEST(SceneFile, ShippedScenesParse) {
    namespace fs = std::filesystem;
    fs::path dir = fs::path(KKE_SOURCE_DIR) / "scenes";
    if (!fs::exists(dir)) GTEST_SKIP();
    int n = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().string().size() > 11 && e.path().string().ends_with(".scene.json")) {
            EXPECT_NO_THROW(kke::SceneFile::load(e.path().string())) << e.path();
            ++n;
        }
    EXPECT_GT(n, 0);
}

TEST(SceneFile, SavesAndLoadsBackTheSameScene) {
    kke::SceneFile s;
    s.name = "Round trip";
    s.packs = { "POLYGON_Town" };
    s.spawn = glm::vec3(1.5f, 0.0f, -2.25f);
    s.spawnYaw = 135.0f;
    s.groundSize = glm::vec2(60.0f, 40.0f);
    s.worldSeed = 42;
    s.hasSun = true;
    s.sunDirection = glm::normalize(glm::vec3(-0.3f, -1.0f, 0.2f));
    s.sunColor = glm::vec3(1.0f, 0.9f, 0.8f);
    s.sunIntensity = 1.25f;
    s.hasAmbient = true;
    s.ambient = glm::vec3(0.1f, 0.12f, 0.15f);
    s.lights.push_back({ glm::vec3(3.0f, 2.5f, 0.1f), glm::vec3(1.0f, 0.7f, 0.4f), 2.0f });
    kke::SceneObject a;
    a.asset = "SM_Prop_Crate_01";
    a.position = glm::vec3(0.1f, 0.3f, -7.7f);
    a.yaw = 45.0f;
    a.scale = glm::vec3(1.5f);
    a.collision = kke::SceneObject::Collision::Box;
    a.texture = "PolygonTown_Texture_01_B.png";
    a.breakable = "wood";
    a.fractureSeed = 7;
    a.pack = "POLYGON_City";
    kke::SceneObject b;
    b.asset = "SM_Env_Road_01";
    b.gridCount = glm::ivec2(4, 2);
    b.gridStep = glm::vec2(5.0f, 5.0f);
    b.scale = glm::vec3(1.0f, 2.0f, 1.0f);
    b.pivot = true;
    s.objects = { a, b };

    const std::string text = s.toJson();
    EXPECT_EQ(text.find("0.10000000149"), std::string::npos) << "floats are written short: " << text;
    kke::SceneFile r = kke::SceneFile::parse(text, "roundtrip");
    EXPECT_EQ(r.toJson(), text); // stable: saving what was loaded changes nothing
    EXPECT_EQ(r.name, s.name);
    EXPECT_EQ(r.packs, s.packs);
    EXPECT_EQ(r.spawn, s.spawn);
    EXPECT_EQ(r.spawnYaw, s.spawnYaw);
    EXPECT_EQ(r.groundSize, s.groundSize);
    EXPECT_EQ(r.worldSeed, 42u);
    ASSERT_TRUE(r.hasSun);
    EXPECT_NEAR(glm::length(r.sunDirection - s.sunDirection), 0.0f, 1e-6f);
    EXPECT_EQ(r.sunIntensity, 1.25f);
    ASSERT_TRUE(r.hasAmbient);
    EXPECT_EQ(r.ambient, s.ambient);
    ASSERT_EQ(r.lights.size(), 1u);
    EXPECT_EQ(r.lights[0].position, s.lights[0].position);
    EXPECT_EQ(r.lights[0].intensity, 2.0f);
    ASSERT_EQ(r.objects.size(), 2u);
    EXPECT_EQ(r.objects[0].position, a.position); // exact, not approximately
    EXPECT_EQ(r.objects[0].yaw, 45.0f);
    EXPECT_EQ(r.objects[0].scale, glm::vec3(1.5f));
    EXPECT_EQ(r.objects[0].collision, kke::SceneObject::Collision::Box);
    EXPECT_EQ(r.objects[0].texture, a.texture);
    EXPECT_EQ(r.objects[0].breakable, "wood");
    EXPECT_EQ(r.objects[0].fractureSeed, 7u);
    EXPECT_EQ(r.objects[0].pack, "POLYGON_City");
    EXPECT_NE(text.find("\"position\": [0.1, 0.3, -7.7]"), std::string::npos) << text;
    EXPECT_EQ(r.objects[1].gridCount, b.gridCount);
    EXPECT_EQ(r.objects[1].gridStep, b.gridStep);
    EXPECT_EQ(r.objects[1].scale, b.scale);
    EXPECT_TRUE(r.objects[1].pivot);
    EXPECT_EQ(r.instanceCount(), 9u);

    // Through a file too (the temp-file-then-rename save).
    const std::string path = ::testing::TempDir() + "kke_roundtrip.scene.json";
    s.save(path);
    EXPECT_EQ(kke::SceneFile::load(path).toJson(), text);
    EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));
    std::filesystem::remove(path);
}

TEST(SceneFile, RejectsUnknownBreakableAndBadSun) {
    EXPECT_THROW(kke::SceneFile::parse(R"({"format":"kke.scene","version":1,"objects":[{"asset":"A","breakable":"cheese"}]})"),
                 std::runtime_error);
    EXPECT_THROW(kke::SceneFile::parse(R"({"format":"kke.scene","version":1,"sun":{"direction":[0,0,0]},"objects":[]})"),
                 std::runtime_error);
}

TEST(SceneFile, SaveToAMissingFolderThrowsNamingTheFile) {
    kke::SceneFile s;
    try {
        s.save("/nonexistent-kke-folder/level.scene.json");
        FAIL() << "expected a throw";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("level.scene.json"), std::string::npos);
    }
}
