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
