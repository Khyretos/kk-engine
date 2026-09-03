// Tests for kke::loadGameManifest — the game.json parser. Uses real
// temporary files on disk (via std::filesystem), not a mocked file
// interface, since parsing is cheap and this keeps the test exercising
// the exact same code path a real scan does.

#include "kke/GameManifest.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

class GameManifestTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = fs::temp_directory_path() / "kke_game_manifest_test";
        fs::remove_all(root);
        fs::create_directories(root);
    }
    void TearDown() override {
        fs::remove_all(root);
    }

    void writeGameJson(const std::string& content) {
        std::ofstream file(root / "game.json");
        file << content;
    }

    fs::path root;
};

} // namespace

TEST_F(GameManifestTest, ParsesAllFieldsCorrectly) {
    writeGameJson(R"({
        "id": "com.test.full",
        "title": "Full Manifest",
        "description": "A complete example",
        "version": "1.2.3",
        "icon": "icon.png",
        "banner": "banner.png",
        "tags": ["one", "two"],
        "engine_version": "0.1.0",
        "modules": ["Cube", "Grid"]
    })");

    kke::GameManifest manifest = kke::loadGameManifest(root.string());

    EXPECT_EQ(manifest.id, "com.test.full");
    EXPECT_EQ(manifest.title, "Full Manifest");
    EXPECT_EQ(manifest.description, "A complete example");
    EXPECT_EQ(manifest.version, "1.2.3");
    EXPECT_EQ(manifest.icon, "icon.png");
    EXPECT_EQ(manifest.banner, "banner.png");
    EXPECT_EQ(manifest.engineVersion, "0.1.0");
    ASSERT_EQ(manifest.tags.size(), 2u);
    EXPECT_EQ(manifest.tags[0], "one");
    EXPECT_EQ(manifest.tags[1], "two");
    ASSERT_EQ(manifest.modules.size(), 2u);
    EXPECT_EQ(manifest.modules[0], "Cube");
    EXPECT_EQ(manifest.sourcePath, root.string());
}

TEST_F(GameManifestTest, MinimalManifestUsesDefaults) {
    writeGameJson(R"({"id": "com.test.minimal", "title": "Minimal"})");

    kke::GameManifest manifest = kke::loadGameManifest(root.string());

    EXPECT_EQ(manifest.id, "com.test.minimal");
    EXPECT_EQ(manifest.title, "Minimal");
    EXPECT_EQ(manifest.version, "0.1.0") << "version should default when omitted";
    EXPECT_TRUE(manifest.description.empty());
    EXPECT_TRUE(manifest.tags.empty());
    EXPECT_TRUE(manifest.modules.empty());
}

TEST_F(GameManifestTest, MissingFileThrows) {
    // Don't even write game.json — root/game.json genuinely doesn't exist.
    EXPECT_THROW(kke::loadGameManifest(root.string()), std::runtime_error);
}

TEST_F(GameManifestTest, InvalidJsonThrows) {
    writeGameJson("{ this is not valid json");
    EXPECT_THROW(kke::loadGameManifest(root.string()), std::runtime_error);
}

TEST_F(GameManifestTest, MissingIdThrows) {
    writeGameJson(R"({"title": "No Id"})");
    EXPECT_THROW(kke::loadGameManifest(root.string()), std::runtime_error);
}

TEST_F(GameManifestTest, EmptyIdThrows) {
    writeGameJson(R"({"id": "", "title": "Empty Id"})");
    EXPECT_THROW(kke::loadGameManifest(root.string()), std::runtime_error);
}

TEST_F(GameManifestTest, MissingTitleThrows) {
    writeGameJson(R"({"id": "com.test.notitle"})");
    EXPECT_THROW(kke::loadGameManifest(root.string()), std::runtime_error);
}

TEST_F(GameManifestTest, NonStringIdThrows) {
    // A manifest where "id" is present but the wrong type (e.g. a number)
    // — malformed input from a game folder shouldn't crash the parser
    // with a JSON library type-mismatch exception; it should fail the
    // same clear way a missing id does.
    writeGameJson(R"({"id": 12345, "title": "Wrong Type"})");
    EXPECT_THROW(kke::loadGameManifest(root.string()), std::runtime_error);
}

TEST_F(GameManifestTest, NonArrayTagsAreIgnoredNotCrashed) {
    // "tags" present but malformed (a string instead of an array) should
    // degrade gracefully — tags end up empty — rather than throw, since
    // tags aren't required for a manifest to be usable.
    writeGameJson(R"({"id": "com.test.badtags", "title": "Bad Tags", "tags": "not an array"})");
    kke::GameManifest manifest = kke::loadGameManifest(root.string());
    EXPECT_TRUE(manifest.tags.empty());
}
