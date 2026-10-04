#include "kke/CookedFile.h"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
namespace cooked = kke::cooked;

TEST(CookedFile, RoundTripsAndHidesTheBytes) {
    const std::vector<uint8_t> plain = { 'K', 'a', 'y', 'd', 'a', 'r', 'a', ' ', 'F', 'B', 'X', 0, 1, 2, 3 };
    std::vector<uint8_t> c, back;
    ASSERT_TRUE(cooked::cook(plain, c));
    EXPECT_TRUE(cooked::isCooked(c));
    EXPECT_FALSE(cooked::isCooked(plain));
    EXPECT_EQ(c.size(), plain.size() + 56); // magic, build tag, nonce, MAC
    EXPECT_EQ(std::search(c.begin(), c.end(), plain.begin(), plain.begin() + 11), c.end()); // no plain text inside
    ASSERT_TRUE(cooked::uncook(c, back));
    EXPECT_EQ(back, plain);

    std::vector<uint8_t> again;
    ASSERT_TRUE(cooked::cook(plain, again));
    EXPECT_NE(again, c); // a fresh nonce each time
}

TEST(CookedFile, DamageIsRefused) {
    std::vector<uint8_t> c, back;
    ASSERT_TRUE(cooked::cook(std::vector<uint8_t>(100, 7), c));
    c[70] ^= 1;
    std::string error;
    EXPECT_FALSE(cooked::uncook(c, back, &error));
    EXPECT_NE(error.find("damaged"), std::string::npos);
    EXPECT_TRUE(back.empty());
}

TEST(CookedFile, AnotherBuildsArtIsRefused) {
    std::vector<uint8_t> c, back;
    ASSERT_TRUE(cooked::cook(std::vector<uint8_t>(100, 7), c));
    c[8] ^= 1; // the build tag right after the magic
    std::string error;
    EXPECT_FALSE(cooked::uncook(c, back, &error));
    EXPECT_NE(error.find("another build"), std::string::npos);
}

TEST(CookedFile, ReadAssetFileReadsPlainAndCookedAndTraces) {
    const fs::path dir = fs::temp_directory_path() / "kke_cooked_test";
    fs::create_directories(dir);
    const std::vector<uint8_t> plain = { 1, 2, 3, 4, 5 };
    std::vector<uint8_t> c;
    ASSERT_TRUE(cooked::cook(plain, c));
    std::ofstream(dir / "plain.png", std::ios::binary).write(reinterpret_cast<const char*>(plain.data()), plain.size());
    std::ofstream(dir / "cooked.png", std::ios::binary).write(reinterpret_cast<const char*>(c.data()), c.size());
    const fs::path trace = dir / "trace.txt";
    fs::remove(trace);
    // SDL_getenv reads SDL's copy of the environment, which setenv() no
    // longer reaches once any earlier test has touched SDL.
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "KKE_ASSET_TRACE", trace.string().c_str(), true);
    std::vector<uint8_t> a, b;
    EXPECT_TRUE(cooked::readAssetFile((dir / "plain.png").string(), a));
    EXPECT_TRUE(cooked::readAssetFile((dir / "cooked.png").string(), b));
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "KKE_ASSET_TRACE");
    EXPECT_EQ(a, plain);
    EXPECT_EQ(b, plain);
    EXPECT_TRUE(cooked::isCookedFile((dir / "cooked.png").string()));
    EXPECT_FALSE(cooked::isCookedFile((dir / "plain.png").string()));
    std::string l1, l2;
    {
        // Closed before remove_all below: Windows can't delete an open file.
        std::ifstream in(trace);
        std::getline(in, l1);
        std::getline(in, l2);
    }
    EXPECT_NE(l1.find("plain.png"), std::string::npos);
    EXPECT_NE(l2.find("cooked.png"), std::string::npos);
    std::vector<uint8_t> none;
    EXPECT_FALSE(cooked::readAssetFile((dir / "missing.png").string(), none));
    fs::remove_all(dir);
}
