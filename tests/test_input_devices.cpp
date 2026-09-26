#include "kke/InputDevices.h"

#include <gtest/gtest.h>

using Identity = kke::InputDevices::Identity;

TEST(InputDevices, IdenticalSticksGetDistinctStableKeysByPort) {
    // HOSAS: same vendor/product/name, no serial; told apart by USB port.
    std::vector<Identity> ids = {
        { "VKB Gladiator NXT", "", "pci0000:00/0000:00:14.0/usb1/1-4/1-4:1.0", "/dev/input/event19", 0x231d, 0x0200 },
        { "Xbox Controller", "3033:abcd", "", "/dev/input/event5", 0x045e, 0x0b12 },
        { "VKB Gladiator NXT", "", "pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0", "/dev/input/event21", 0x231d, 0x0200 },
    };
    auto k = kke::InputDevices::keyDevices(ids);
    EXPECT_NE(k[0].stableKey, k[2].stableKey);
    EXPECT_NE(k[0].ref, k[2].ref);
    EXPECT_EQ(k[0].duplicateCount, 2);
    EXPECT_EQ(k[2].duplicateCount, 2);
    // Numbered by port: 1-2 is #1, 1-4 is #2, whatever order SDL listed them in.
    EXPECT_EQ(k[2].duplicateIndex, 0);
    EXPECT_EQ(k[0].duplicateIndex, 1);
    EXPECT_EQ(k[1].duplicateCount, 1);
    EXPECT_NE(k[1].stableKey.find("sn:3033:abcd"), std::string::npos);

    // Same devices, enumerated in another order after a reboot (eventN changed): same keys.
    std::vector<Identity> again = { ids[2], ids[1], ids[0] };
    again[0].path = "/dev/input/event3";
    again[2].path = "/dev/input/event4";
    auto k2 = kke::InputDevices::keyDevices(again);
    EXPECT_EQ(k2[0].stableKey, k[2].stableKey);
    EXPECT_EQ(k2[2].stableKey, k[0].stableKey);
    EXPECT_EQ(k2[0].duplicateIndex, 0);
}

TEST(InputDevices, TwinsWithNothingToTellThemApartStillGetUniqueKeys) {
    std::vector<Identity> ids = { { "Stick", "", "", "", 1, 2 }, { "Stick", "", "", "", 1, 2 } };
    auto k = kke::InputDevices::keyDevices(ids);
    EXPECT_NE(k[0].stableKey, k[1].stableKey);
    EXPECT_NE(k[0].ref, k[1].ref);
    EXPECT_NE(k[0].ref, 0u);
}

TEST(InputDevices, SerialOfZerosIsIgnored) {
    std::vector<Identity> ids = { { "Pad", "000000", "port-a", "", 1, 2 } };
    auto k = kke::InputDevices::keyDevices(ids);
    EXPECT_EQ(k[0].stableKey.find("sn:"), std::string::npos);
    EXPECT_NE(k[0].stableKey.find("port:port-a"), std::string::npos);
}
