#include "kke/modules/DemoPanelModule.h"

#include <gtest/gtest.h>

using kke::DemoPanelModule;

TEST(DemoPanel, SliderStepsSnapToTheGridAndClamp) {
    EXPECT_FLOAT_EQ(DemoPanelModule::stepValue(0.5f, 0.0f, 1.0f, 0.1f, 1), 0.6f);
    EXPECT_FLOAT_EQ(DemoPanelModule::stepValue(0.5f, 0.0f, 1.0f, 0.1f, -1), 0.4f);
    // A value set elsewhere (a key, a script) lands on a round step.
    EXPECT_FLOAT_EQ(DemoPanelModule::stepValue(0.3333f, 0.0f, 1.0f, 0.1f, 1), 0.4f);
    EXPECT_FLOAT_EQ(DemoPanelModule::stepValue(0.97f, 0.0f, 1.0f, 0.1f, 1), 1.0f);
    EXPECT_FLOAT_EQ(DemoPanelModule::stepValue(0.02f, 0.0f, 1.0f, 0.1f, -1), 0.0f);
    EXPECT_FLOAT_EQ(DemoPanelModule::stepValue(-170.0f, -180.0f, 180.0f, 5.0f, -3), -180.0f);
}

TEST(DemoPanel, ChoicesWrapBothWays) {
    EXPECT_EQ(DemoPanelModule::cycle(0, 3, -1), 2);
    EXPECT_EQ(DemoPanelModule::cycle(2, 3, 1), 0);
    EXPECT_EQ(DemoPanelModule::cycle(1, 3, 1), 2);
    EXPECT_EQ(DemoPanelModule::cycle(5, 0, 1), 0);
}

TEST(DemoPanel, FormatsOneNumberInTheGamesText) {
    EXPECT_EQ(DemoPanelModule::formatValue("%.1f m/s", 7.04f), "7.0 m/s");
    EXPECT_EQ(DemoPanelModule::formatValue("%.0f drops/s", 120.4f), "120 drops/s");
    EXPECT_EQ(DemoPanelModule::formatValue("%d %%", 42.6f), "43 %");
    EXPECT_EQ(DemoPanelModule::formatValue("", 0.5f), "0.50");
    // Only the first number is formatted; a second % stays as text.
    EXPECT_EQ(DemoPanelModule::formatValue("%.2f of %.2f", 1.0f), "1.00 of %.2f");
    // Nothing a format string could do to snprintf.
    EXPECT_EQ(DemoPanelModule::formatValue("%s %n", 1.0f), "%s %n");
}
