// Tests for kke::MarketplaceIndex — specifically the exact behavior that
// was asked for: importing the same game twice (including from a
// differently-named folder) must not create a duplicate entry, and a
// broken/missing manifest in the scan must not abort the whole scan.

#include "kke/MarketplaceIndex.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

void writeGameJson(const fs::path& folder, const std::string& id, const std::string& title) {
    fs::create_directories(folder);
    std::ofstream file(folder / "game.json");
    file << R"({"id":")" << id << R"(","title":")" << title << R"(","tags":["test"]})";
}

class MarketplaceIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = fs::temp_directory_path() / "kke_marketplace_index_test";
        fs::remove_all(root);
        fs::create_directories(root);
    }
    void TearDown() override {
        fs::remove_all(root);
    }

    fs::path root;
};

} // namespace

TEST_F(MarketplaceIndexTest, ImportingSameIdFromDifferentFoldersUpdatesInPlace) {
    writeGameJson(root / "alpha", "com.test.one", "Alpha (first import)");
    writeGameJson(root / "alpha_copy", "com.test.one", "Alpha (re-imported, updated title)");

    kke::MarketplaceIndex index;
    ASSERT_TRUE(index.importGame((root / "alpha").string()));
    EXPECT_EQ(index.count(), 1u);

    ASSERT_TRUE(index.importGame((root / "alpha_copy").string()));
    EXPECT_EQ(index.count(), 1u) << "same id from a different folder must not duplicate";
    EXPECT_EQ(index.games()[0].title, "Alpha (re-imported, updated title)")
        << "the later import's data should win";
}

TEST_F(MarketplaceIndexTest, ReimportingTheExactSameFolderIsANoOp) {
    writeGameJson(root / "alpha", "com.test.one", "Alpha");

    kke::MarketplaceIndex index;
    index.importGame((root / "alpha").string());
    ASSERT_EQ(index.count(), 1u);

    EXPECT_TRUE(index.importGame((root / "alpha").string())) << "re-import should succeed, not error";
    EXPECT_EQ(index.count(), 1u) << "importing the exact same folder again must not duplicate";
}

TEST_F(MarketplaceIndexTest, ScanDirectorySkipsNonGameAndBrokenFolders) {
    writeGameJson(root / "alpha", "com.test.one", "Alpha");
    writeGameJson(root / "beta", "com.test.two", "Beta");
    fs::create_directories(root / "not_a_game"); // no game.json at all
    fs::create_directories(root / "broken");
    { std::ofstream f(root / "broken" / "game.json"); f << "{ not valid json"; }

    kke::MarketplaceIndex index;
    index.scanDirectory(root.string());

    EXPECT_EQ(index.count(), 2u) << "exactly the 2 valid game folders, broken/non-game ones skipped";
}

TEST_F(MarketplaceIndexTest, ScanDirectoryDedupsAcrossDuplicateIds) {
    writeGameJson(root / "alpha", "com.test.one", "Alpha");
    writeGameJson(root / "alpha_copy", "com.test.one", "Alpha copy");
    writeGameJson(root / "beta", "com.test.two", "Beta");

    kke::MarketplaceIndex index;
    index.scanDirectory(root.string());

    EXPECT_EQ(index.count(), 2u) << "2 unique ids despite 3 folders (directory iteration order is unspecified, "
                                    "so this only asserts on the order-independent fact: exactly one survivor per id)";
}

TEST_F(MarketplaceIndexTest, ScanningNonexistentDirectoryDoesNotCrash) {
    kke::MarketplaceIndex index;
    index.scanDirectory((root / "does_not_exist").string());
    EXPECT_EQ(index.count(), 0u);
}

TEST_F(MarketplaceIndexTest, ImportingFolderWithoutManifestFails) {
    fs::create_directories(root / "no_manifest");
    kke::MarketplaceIndex index;
    EXPECT_FALSE(index.importGame((root / "no_manifest").string()));
    EXPECT_EQ(index.count(), 0u);
}

TEST_F(MarketplaceIndexTest, SaveLoadRoundTripPreservesOrderAndContent) {
    writeGameJson(root / "alpha", "com.test.one", "Alpha");
    writeGameJson(root / "beta", "com.test.two", "Beta");

    kke::MarketplaceIndex index;
    index.importGame((root / "alpha").string());
    index.importGame((root / "beta").string());

    fs::path indexPath = root / "marketplace.json";
    index.save(indexPath.string());

    kke::MarketplaceIndex reloaded;
    reloaded.load(indexPath.string());

    ASSERT_EQ(reloaded.count(), 2u);
    EXPECT_EQ(reloaded.games()[0].id, "com.test.one") << "order must be preserved (alpha imported first)";
    EXPECT_EQ(reloaded.games()[1].id, "com.test.two");
    EXPECT_EQ(reloaded.games()[0].title, "Alpha");
}

TEST_F(MarketplaceIndexTest, LoadingNonexistentIndexFileIsNotAnError) {
    kke::MarketplaceIndex index;
    index.load((root / "does_not_exist.json").string());
    EXPECT_EQ(index.count(), 0u);
}
