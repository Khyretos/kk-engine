#include "kke/Ktx2.h"
#include "kke/TextureCompress.h"
#include "kke/TextureMips.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>

namespace {

// A test card: colour gradients, a hard edge and a cutout hole, at a size
// that isn't a multiple of 4 (edge blocks).
std::vector<uint8_t> card(uint32_t w, uint32_t h) {
    std::vector<uint8_t> p(size_t(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* t = &p[(size_t(y) * w + x) * 4];
            t[0] = uint8_t(255 * x / (w - 1));
            t[1] = uint8_t(255 * y / (h - 1));
            t[2] = x < w / 2 ? 40 : 220;
            const float dx = float(x) - w / 2.0f, dy = float(y) - h / 2.0f;
            t[3] = dx * dx + dy * dy < float(w * w) / 16.0f ? 0 : 255;
        }
    return p;
}

double psnr(const uint8_t* a, const uint8_t* b, size_t texels, int channels) {
    double se = 0.0;
    for (size_t i = 0; i < texels; ++i)
        for (int c = 0; c < channels; ++c) {
            const double d = double(a[i * 4 + c]) - double(b[i * 4 + c]);
            se += d * d;
        }
    const double mse = se / double(texels * channels);
    return mse == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST(TextureCompress, Bc7RoundTripLooksLikeTheSource) {
    const uint32_t w = 70, h = 45;
    const std::vector<uint8_t> src = card(w, h);
    const std::vector<uint8_t> blocks = kke::encodeBc7(src.data(), w, h, true, 2);
    ASSERT_EQ(blocks.size(), kke::ktx2::levelSize(kke::ktx2::kBc7Srgb, w, h));
    EXPECT_EQ(blocks.size(), size_t(18) * 12 * 16);
    std::vector<uint8_t> back(src.size());
    kke::decodeBc7(blocks.data(), w, h, back.data());
    // BC7 on smooth gradients and a hard edge: well over 35 dB, and the
    // cutout hole stays a hole.
    EXPECT_GT(psnr(src.data(), back.data(), size_t(w) * h, 3), 35.0);
    EXPECT_GT(psnr(src.data(), back.data(), size_t(w) * h, 4), 35.0);
    EXPECT_LT(back[(size_t(h / 2) * w + w / 2) * 4 + 3], 16);
    EXPECT_GT(back[3], 240);
}

TEST(TextureCompress, ThreadCountDoesNotChangeTheResult) {
    const std::vector<uint8_t> src = card(64, 64);
    EXPECT_EQ(kke::encodeBc7(src.data(), 64, 64, true, 1), kke::encodeBc7(src.data(), 64, 64, true, 4));
}

TEST(TextureCompress, CookedImageHasTheSameMipsAsTheLoader) {
    const uint32_t w = 37, h = 20;
    const std::vector<uint8_t> src = card(w, h);
    const kke::ktx2::Image img = kke::cookBc7(src.data(), w, h, true, 2);
    std::vector<uint8_t> chain;
    const uint32_t levels = kke::buildMipChainRgba8(src.data(), w, h, true, chain);
    ASSERT_EQ(img.levels.size(), levels);
    EXPECT_EQ(levels, 6u); // 37 -> 18 -> 9 -> 4 -> 2 -> 1
    EXPECT_EQ(img.vkFormat, kke::ktx2::kBc7Srgb);
    const std::vector<uint8_t> decoded = kke::toRgba8Chain(img);
    ASSERT_EQ(decoded.size(), chain.size());
    // Lower than level 0 alone: the 4x4 and smaller levels of a card this
    // sharp are mostly edge.
    EXPECT_GT(psnr(chain.data(), decoded.data(), chain.size() / 4, 4), 28.0);
    EXPECT_EQ(kke::cookBc7(src.data(), w, h, false, 1).vkFormat, kke::ktx2::kBc7Unorm);
}

TEST(Ktx2, WritesAndReadsBackEveryLevel) {
    const std::vector<uint8_t> src = card(33, 17);
    const kke::ktx2::Image img = kke::cookBc7(src.data(), 33, 17, true, 1);
    const std::vector<uint8_t> file = kke::ktx2::write(img);
    ASSERT_TRUE(kke::ktx2::isKtx2(file.data(), file.size()));
    kke::ktx2::Image back;
    std::string why;
    ASSERT_TRUE(kke::ktx2::read(file.data(), file.size(), back, &why)) << why;
    EXPECT_EQ(back.vkFormat, img.vkFormat);
    EXPECT_EQ(back.width, 33u);
    EXPECT_EQ(back.height, 17u);
    EXPECT_EQ(back.levels, img.levels);
    // The layout the format asks for: levels stored smallest first, each
    // on a 16-byte boundary, and a data format descriptor saying BC7 sRGB.
    uint64_t off0, off1;
    std::memcpy(&off0, file.data() + 80, 8);
    std::memcpy(&off1, file.data() + 80 + 24, 8);
    EXPECT_GT(off0, off1);
    EXPECT_EQ(off0 % 16, 0u);
    uint32_t dfdOffset, word;
    std::memcpy(&dfdOffset, file.data() + 48, 4);
    std::memcpy(&word, file.data() + dfdOffset + 12, 4);
    EXPECT_EQ(word & 0xFF, 134u);          // KHR_DF_MODEL_BC7
    EXPECT_EQ((word >> 16) & 0xFF, 2u);    // sRGB
}

TEST(Ktx2, RefusesWhatItCannotRead) {
    kke::ktx2::Image img;
    img.vkFormat = kke::ktx2::kRgba8Unorm;
    img.width = 2;
    img.height = 2;
    img.levels = { std::vector<uint8_t>(16, 7), std::vector<uint8_t>(4, 9) };
    std::vector<uint8_t> file = kke::ktx2::write(img);
    kke::ktx2::Image back;
    std::string why;
    ASSERT_TRUE(kke::ktx2::read(file.data(), file.size(), back, &why)) << why;
    EXPECT_EQ(kke::toRgba8Chain(back).size(), 20u);

    EXPECT_FALSE(kke::ktx2::read(file.data(), 40, back, &why)); // cut short
    std::vector<uint8_t> bad = file;
    bad[0] = 'x';
    EXPECT_FALSE(kke::ktx2::read(bad.data(), bad.size(), back, &why));
    bad = file;
    const uint32_t astc = 157;
    std::memcpy(bad.data() + 12, &astc, 4);
    EXPECT_FALSE(kke::ktx2::read(bad.data(), bad.size(), back, &why));
    EXPECT_NE(why.find("157"), std::string::npos);
    bad = file;
    const uint32_t zstd = 2;
    std::memcpy(bad.data() + 44, &zstd, 4);
    EXPECT_FALSE(kke::ktx2::read(bad.data(), bad.size(), back, &why));
    bad = file;
    const uint64_t huge = 1ull << 40;
    std::memcpy(bad.data() + 80, &huge, 8);
    EXPECT_FALSE(kke::ktx2::read(bad.data(), bad.size(), back, &why)); // a level past the end
    file.resize(file.size() - 2);
    EXPECT_FALSE(kke::ktx2::read(file.data(), file.size(), back, &why));
}
