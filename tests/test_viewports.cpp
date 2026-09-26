#include "kke/Viewports.h"

#include <gtest/gtest.h>

namespace {

uint64_t area(const VkRect2D& r) { return static_cast<uint64_t>(r.extent.width) * r.extent.height; }

} // namespace

// Every layout covers the window without overlap: the pixel areas add up
// to the whole (three players leave one quarter empty).
TEST(Viewports, SplitScreenTilesTheWindow) {
    const VkExtent2D odd{ 1281, 721 }; // odd sizes: halves don't divide evenly
    for (int n = 1; n <= 4; ++n) {
        for (bool side : { true, false }) {
            const auto views = kke::splitScreen(n, side);
            ASSERT_EQ(static_cast<int>(views.size()), n);
            uint64_t total = 0;
            for (const auto& v : views) total += area(kke::viewPixels(v, odd));
            const uint64_t whole = static_cast<uint64_t>(odd.width) * odd.height;
            if (n == 3) EXPECT_LT(total, whole);
            else EXPECT_EQ(total, whole) << n << " views";
        }
    }
    EXPECT_EQ(kke::splitScreen(0).size(), 1u);
    EXPECT_EQ(kke::splitScreen(9).size(), 4u);
}

TEST(Viewports, TwoPlayersSideBySideOrStacked) {
    const VkExtent2D e{ 1280, 720 };
    auto side = kke::splitScreen(2, true);
    VkRect2D right = kke::viewPixels(side[1], e);
    EXPECT_EQ(right.offset.x, 640);
    EXPECT_EQ(right.extent.height, 720u);
    EXPECT_NEAR(kke::viewAspect(side[0], e), 640.0f / 720.0f, 1e-4f);
    auto stacked = kke::splitScreen(2, false);
    VkRect2D bottom = kke::viewPixels(stacked[1], e);
    EXPECT_EQ(bottom.offset.y, 360);
    EXPECT_EQ(bottom.extent.width, 1280u);
}

// Neighbours meet exactly: the left view ends where the right one starts.
TEST(Viewports, NeighboursShareTheirEdge) {
    const VkExtent2D e{ 1001, 555 };
    auto q = kke::splitScreen(4);
    VkRect2D a = kke::viewPixels(q[0], e), b = kke::viewPixels(q[1], e), c = kke::viewPixels(q[2], e);
    EXPECT_EQ(a.offset.x + static_cast<int32_t>(a.extent.width), b.offset.x);
    EXPECT_EQ(a.offset.y + static_cast<int32_t>(a.extent.height), c.offset.y);
    EXPECT_EQ(b.offset.x + static_cast<int32_t>(b.extent.width), 1001);
}

TEST(Viewports, PictureInPictureSitsInItsCorner) {
    const VkExtent2D e{ 1000, 500 };
    VkRect2D tl = kke::viewPixels(kke::pictureInPicture(0, 0.25f, 0.02f), e);
    EXPECT_EQ(tl.offset.x, 20);
    EXPECT_EQ(tl.offset.y, 10);
    EXPECT_EQ(tl.extent.width, 250u);
    VkRect2D br = kke::viewPixels(kke::pictureInPicture(3, 0.25f, 0.02f), e);
    EXPECT_EQ(br.offset.x + static_cast<int32_t>(br.extent.width), 980);
    EXPECT_EQ(br.offset.y + static_cast<int32_t>(br.extent.height), 490);
    // Same shape as the window.
    EXPECT_NEAR(kke::viewAspect(kke::pictureInPicture(1), e), 2.0f, 0.02f);
}
