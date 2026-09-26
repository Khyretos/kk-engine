#include "kke/Thumbnails.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace kke;

namespace {
glm::vec3 project(const glm::mat4& viewProj, const glm::vec3& p) {
    const glm::vec4 c = viewProj * glm::vec4(p, 1.0f);
    return glm::vec3(c) / c.w;
}
} // namespace

TEST(Thumbnails, FramingFitsAnyModelInTheView) {
    const glm::mat4 vp = thumbnailProj() * thumbnailView();
    // A tall thin lamp post, a flat wide floor tile, a tiny coin off-centre.
    const std::pair<glm::vec3, glm::vec3> models[] = {
        { { -0.1f, 0.0f, -0.1f }, { 0.1f, 6.0f, 0.1f } },
        { { -5.0f, 0.0f, -5.0f }, { 5.0f, 0.1f, 5.0f } },
        { { 10.0f, 2.0f, 3.0f }, { 10.02f, 2.001f, 3.02f } },
    };
    for (const auto& [lo, hi] : models) {
        const glm::mat4 m = thumbnailFraming(lo, hi);
        float extent = 0.0f;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 p((corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y, (corner & 4) ? hi.z : lo.z);
            const glm::vec3 ndc = project(vp, glm::vec3(m * glm::vec4(p, 1.0f)));
            EXPECT_LE(std::fabs(ndc.x), 1.0f);
            EXPECT_LE(std::fabs(ndc.y), 1.0f);
            EXPECT_GT(ndc.z, 0.0f);
            EXPECT_LT(ndc.z, 1.0f);
            extent = std::max({ extent, std::fabs(ndc.x), std::fabs(ndc.y) });
        }
        EXPECT_GT(extent, 0.4f); // and fills a good part of it, whatever its size
        // Centred.
        const glm::vec3 c = project(vp, glm::vec3(m * glm::vec4((lo + hi) * 0.5f, 1.0f)));
        EXPECT_NEAR(c.x, 0.0f, 1e-4f);
        EXPECT_NEAR(c.y, 0.0f, 1e-4f);
    }
}

TEST(Thumbnails, FramingSurvivesEmptyOrBrokenBounds) {
    for (const glm::mat4& m : { thumbnailFraming(glm::vec3(1.0f), glm::vec3(1.0f)),
                                thumbnailFraming(glm::vec3(NAN), glm::vec3(1.0f)) })
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) EXPECT_TRUE(std::isfinite(m[i][j]));
}

TEST(Thumbnails, CacheFileChangesWithTheAssetAndStaysInItsPackFolder) {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "kke_thumb_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string asset = (dir / "SM_Prop_Crate_01.fbx").string();
    { std::ofstream(asset) << "one"; }
    const std::string root = (dir / "cache").string();
    const std::string a = thumbnailCacheFile(root, "POLYGON_Town", asset);
    ASSERT_FALSE(a.empty());
    EXPECT_EQ(fs::path(a).parent_path(), fs::path(root) / "POLYGON_Town");
    EXPECT_EQ(fs::path(a).extension(), ".png");
    EXPECT_EQ(thumbnailCacheFile(root, "POLYGON_Town", asset), a); // stable
    { std::ofstream(asset) << "a different, longer file"; }
    EXPECT_NE(thumbnailCacheFile(root, "POLYGON_Town", asset), a); // edited: made again
    EXPECT_TRUE(thumbnailCacheFile(root, "POLYGON_Town", (dir / "missing.fbx").string()).empty());
    EXPECT_TRUE(thumbnailCacheFile("", "POLYGON_Town", asset).empty());
    // A hostile pack name can't leave the cache folder.
    const std::string evil = thumbnailCacheFile(root, "../../etc", asset);
    EXPECT_EQ(fs::path(evil).parent_path().parent_path(), fs::path(root));
    fs::remove_all(dir);
}

TEST(Thumbnails, PackFolderNamesAreSafe) {
    EXPECT_EQ(sanitizePackFolder("POLYGON_Town"), "POLYGON_Town");
    EXPECT_EQ(sanitizePackFolder("a/b\\c:d"), "a_b_c_d");
    for (const char* bad : { "", ".", "..", "/", "..." }) {
        const std::string s = sanitizePackFolder(bad);
        EXPECT_FALSE(s.empty());
        EXPECT_NE(s, ".");
        EXPECT_NE(s, "..");
        EXPECT_EQ(s.find('/'), std::string::npos);
    }
}

TEST(Thumbnails, CacheRootFollowsTheEnvironment) {
#ifndef _WIN32
    const char* old = std::getenv("KKE_THUMBNAIL_CACHE");
    const std::string saved = old ? old : "";
    setenv("KKE_THUMBNAIL_CACHE", "/tmp/kke-thumbs", 1);
    EXPECT_EQ(defaultThumbnailCacheRoot(), "/tmp/kke-thumbs");
    unsetenv("KKE_THUMBNAIL_CACHE");
    const std::string def = defaultThumbnailCacheRoot();
    if (std::getenv("XDG_CACHE_HOME") || std::getenv("HOME")) {
        EXPECT_NE(def.find("kk-engine/thumbnails"), std::string::npos);
    }
    if (old) setenv("KKE_THUMBNAIL_CACHE", saved.c_str(), 1);
#endif
}

TEST(ThumbnailSlots, ReusesTheLeastRecentlyUsedButNeverWhatsOnScreen) {
    ThumbnailSlots slots(3);
    EXPECT_EQ(slots.acquire("a", 1), 0);
    EXPECT_EQ(slots.acquire("b", 2), 1);
    EXPECT_EQ(slots.acquire("c", 3), 2);
    EXPECT_EQ(slots.acquire("a", 4), 0); // already has one: same tile
    std::string evicted;
    // All full; "b" was used longest ago.
    EXPECT_EQ(slots.acquire("d", 5, &evicted), 1);
    EXPECT_EQ(evicted, "b");
    EXPECT_EQ(slots.find("b"), -1);
    EXPECT_EQ(slots.find("d"), 1);
    // Everything used in frame 6 is on screen: nothing to give.
    for (const char* k : { "a", "c", "d" }) slots.touch(slots.find(k), 6);
    EXPECT_EQ(slots.acquire("e", 6, &evicted), -1);
    EXPECT_TRUE(evicted.empty());
    // Next frame they can go again.
    EXPECT_GE(slots.acquire("e", 7), 0);
    slots.release("e");
    EXPECT_EQ(slots.find("e"), -1);
    EXPECT_EQ(slots.used(), 2u);
}

TEST(ThumbnailSlots, TileRectsTileTheAtlas) {
    const int atlas = 2048, tile = 128, perRow = atlas / tile;
    const ThumbnailTileRect first = thumbnailTileRect(0, atlas, tile);
    EXPECT_EQ(first.x, 0);
    EXPECT_EQ(first.y, 0);
    EXPECT_FLOAT_EQ(first.uv1.x, 128.0f / 2048.0f);
    const ThumbnailTileRect last = thumbnailTileRect(perRow * perRow - 1, atlas, tile);
    EXPECT_EQ(last.x, atlas - tile);
    EXPECT_EQ(last.y, atlas - tile);
    EXPECT_FLOAT_EQ(last.uv1.x, 1.0f);
    EXPECT_FLOAT_EQ(last.uv1.y, 1.0f);
    const ThumbnailTileRect nextRow = thumbnailTileRect(perRow, atlas, tile);
    EXPECT_EQ(nextRow.x, 0);
    EXPECT_EQ(nextRow.y, tile);
}
