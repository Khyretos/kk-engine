#include "kke/UiProfile.h"

#include <gtest/gtest.h>

using kke::ScreenRect;
using kke::UiProfile;

TEST(UiProfile, FollowsTheTargetUnlessOverridden) {
    EXPECT_EQ(kke::uiProfileForTarget("android"), UiProfile::Phone);
    EXPECT_EQ(kke::uiProfileForTarget("ios"), UiProfile::Phone);
    EXPECT_EQ(kke::uiProfileForTarget("steam-deck"), UiProfile::Console);
    EXPECT_EQ(kke::uiProfileForTarget("desktop-high"), UiProfile::Desktop);
    EXPECT_EQ(kke::uiProfileForTarget("desktop-high", "phone"), UiProfile::Phone);
    EXPECT_EQ(kke::uiProfileForTarget("android", "nonsense"), UiProfile::Phone);
    EXPECT_STREQ(kke::uiProfileName(UiProfile::Console), "console");
}

TEST(UiProfile, SafeRectKeepsClearOfBarsAndTheMargin) {
    // No system insets, no margin: the whole screen.
    EXPECT_EQ(kke::safeScreenRect(1000, 500, {}, 0), (ScreenRect{ 0, 0, 1000, 500 }));
    // A margin on every side.
    EXPECT_EQ(kke::safeScreenRect(1000, 500, {}, 10), (ScreenRect{ 10, 10, 980, 480 }));
    // A navigation bar on the right (landscape) and a status bar on top:
    // the larger of the bar and the margin wins on each side.
    EXPECT_EQ(kke::safeScreenRect(1000, 500, { 0, 30, 920, 470 }, 10), (ScreenRect{ 10, 30, 910, 460 }));
    // Nonsense insets (half the screen gone) are ignored.
    EXPECT_EQ(kke::safeScreenRect(1000, 500, { 0, 0, 100, 500 }, 0), (ScreenRect{ 0, 0, 1000, 500 }));
}
