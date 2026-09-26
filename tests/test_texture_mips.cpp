#include "kke/TextureMips.h"

#include <gtest/gtest.h>
#include <vector>

namespace {

// A w x h RGBA image: white, opaque where `solid` says, alpha 0 elsewhere.
template <typename F>
std::vector<uint8_t> makeImage(uint32_t w, uint32_t h, F solid) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 255);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) px[(y * w + x) * 4 + 3] = solid(x, y) ? 255 : 0;
    return px;
}

} // namespace

TEST(TextureMips, OpaqueTextureIsNotCutout) {
    auto px = makeImage(4, 4, [](uint32_t, uint32_t) { return true; });
    EXPECT_FALSE(kke::hasTransparentTexels(px.data(), 16));
    EXPECT_FLOAT_EQ(kke::alphaCoverage(px.data(), 16, 0.5f), 1.0f);
}

TEST(TextureMips, DownsampleAveragesColourInLinearLight) {
    // Black/white checker: the linear average is 0.5, which is ~188 in sRGB
    // (averaging the bytes would give 128, visibly too dark).
    std::vector<uint8_t> px = { 0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255 };
    uint8_t out[4];
    kke::downsampleRgba8Srgb(px.data(), 2, 2, out, 1, 1);
    EXPECT_NEAR(out[0], 188, 1);
    EXPECT_EQ(out[3], 255);
}

TEST(TextureMips, CoverageKeptDownTheMipChain) {
    // Thin leaf-like stripes: one opaque column in four. A plain box
    // filter halves alpha each level, so after two levels nothing passes
    // a 0.5 alpha test and the foliage disappears.
    const uint32_t n = 64;
    auto base = makeImage(n, n, [](uint32_t x, uint32_t) { return x % 4 == 0; });
    const float baseCoverage = kke::alphaCoverage(base.data(), n * n, 0.5f);
    ASSERT_NEAR(baseCoverage, 0.25f, 1e-6f);

    std::vector<uint8_t> plain = base, scaled = base;
    uint32_t w = n;
    for (int level = 0; level < 2; ++level) {
        uint32_t nw = w / 2;
        std::vector<uint8_t> a(static_cast<size_t>(nw) * nw * 4), b(a.size());
        kke::downsampleRgba8Srgb(plain.data(), w, w, a.data(), nw, nw);
        kke::downsampleRgba8Srgb(scaled.data(), w, w, b.data(), nw, nw);
        kke::scaleAlphaToCoverage(b.data(), static_cast<size_t>(nw) * nw, baseCoverage, 0.5f);
        plain.swap(a);
        scaled.swap(b);
        w = nw;
    }
    EXPECT_FLOAT_EQ(kke::alphaCoverage(plain.data(), w * w, 0.5f), 0.0f);
    EXPECT_GE(kke::alphaCoverage(scaled.data(), w * w, 0.5f), 0.25f);
}

TEST(TextureMips, FullyTransparentLevelStaysTransparent) {
    auto px = makeImage(4, 4, [](uint32_t, uint32_t) { return false; });
    kke::scaleAlphaToCoverage(px.data(), 16, 0.5f, 0.5f);
    EXPECT_FLOAT_EQ(kke::alphaCoverage(px.data(), 16, 0.5f), 0.0f);
}
