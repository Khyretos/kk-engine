#include "kke/ColorFormats.h"

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

// ENG-G17 (10-bit output): which swapchain format is picked, and how a
// screenshot of each becomes RGBA8.

namespace {
VkSurfaceFormatKHR sf(VkFormat f) { return { f, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }; }
} // namespace

TEST(ColorFormats, KnowsWhichFormatsEncodeSrgb) {
    EXPECT_TRUE(kke::formatEncodesSrgb(VK_FORMAT_B8G8R8A8_SRGB));
    EXPECT_TRUE(kke::formatEncodesSrgb(VK_FORMAT_R8G8B8A8_SRGB));
    EXPECT_FALSE(kke::formatEncodesSrgb(VK_FORMAT_B8G8R8A8_UNORM));
    EXPECT_FALSE(kke::formatEncodesSrgb(VK_FORMAT_A2B10G10R10_UNORM_PACK32));
    EXPECT_FALSE(kke::formatEncodesSrgb(VK_FORMAT_A2R10G10B10_UNORM_PACK32));
}

TEST(ColorFormats, PrefersTenBitWhenAllowed) {
    const std::vector<VkSurfaceFormatKHR> offered = { sf(VK_FORMAT_B8G8R8A8_UNORM), sf(VK_FORMAT_B8G8R8A8_SRGB),
                                                      sf(VK_FORMAT_A2R10G10B10_UNORM_PACK32), sf(VK_FORMAT_A2B10G10R10_UNORM_PACK32) };
    EXPECT_EQ(kke::chooseSurfaceFormat(offered, true).format, VK_FORMAT_A2B10G10R10_UNORM_PACK32);
    EXPECT_EQ(kke::chooseSurfaceFormat(offered, false).format, VK_FORMAT_B8G8R8A8_SRGB);
}

TEST(ColorFormats, TenBitOnlyInTheSrgbColourSpace) {
    const std::vector<VkSurfaceFormatKHR> offered = { { VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT },
                                                      sf(VK_FORMAT_R8G8B8A8_SRGB) };
    EXPECT_EQ(kke::chooseSurfaceFormat(offered, true).format, VK_FORMAT_R8G8B8A8_SRGB);
}

TEST(ColorFormats, FallsBackToTheFirstOffered) {
    // A 30-bit X display: lavapipe offers only this one.
    const std::vector<VkSurfaceFormatKHR> offered = { sf(VK_FORMAT_A2R10G10B10_UNORM_PACK32) };
    EXPECT_EQ(kke::chooseSurfaceFormat(offered, false).format, VK_FORMAT_A2R10G10B10_UNORM_PACK32);
    const std::vector<VkSurfaceFormatKHR> odd = { sf(VK_FORMAT_R5G6B5_UNORM_PACK16) };
    EXPECT_EQ(kke::chooseSurfaceFormat(odd, true).format, VK_FORMAT_R5G6B5_UNORM_PACK16);
}

TEST(ColorFormats, CapturesEightBitFormats) {
    const uint8_t bgra[4] = { 10, 20, 30, 0 };
    uint8_t out[4] = {};
    ASSERT_TRUE(kke::capturableFormat(VK_FORMAT_B8G8R8A8_SRGB));
    kke::captureToRgba8(bgra, 1, VK_FORMAT_B8G8R8A8_SRGB, out);
    EXPECT_EQ(out[0], 30);
    EXPECT_EQ(out[1], 20);
    EXPECT_EQ(out[2], 10);
    EXPECT_EQ(out[3], 255);
    kke::captureToRgba8(bgra, 1, VK_FORMAT_R8G8B8A8_UNORM, out);
    EXPECT_EQ(out[0], 10);
    EXPECT_EQ(out[2], 30);
}

TEST(ColorFormats, CapturesTenBitFormats) {
    // R = 1023, G = 512, B = 0.
    const uint32_t abgr = (3u << 30) | (0u << 20) | (512u << 10) | 1023u;
    const uint32_t argb = (3u << 30) | (1023u << 20) | (512u << 10) | 0u;
    uint8_t src[4];
    uint8_t out[4] = {};
    ASSERT_TRUE(kke::capturableFormat(VK_FORMAT_A2B10G10R10_UNORM_PACK32));
    ASSERT_TRUE(kke::capturableFormat(VK_FORMAT_A2R10G10B10_UNORM_PACK32));
    std::memcpy(src, &abgr, 4);
    kke::captureToRgba8(src, 1, VK_FORMAT_A2B10G10R10_UNORM_PACK32, out);
    EXPECT_EQ(out[0], 255);
    EXPECT_EQ(out[1], 128);
    EXPECT_EQ(out[2], 0);
    EXPECT_EQ(out[3], 255);
    std::memcpy(src, &argb, 4);
    kke::captureToRgba8(src, 1, VK_FORMAT_A2R10G10B10_UNORM_PACK32, out);
    EXPECT_EQ(out[0], 255);
    EXPECT_EQ(out[1], 128);
    EXPECT_EQ(out[2], 0);
    EXPECT_FALSE(kke::capturableFormat(VK_FORMAT_R16G16B16A16_SFLOAT));
}
