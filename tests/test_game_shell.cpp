// The game shell's pure helpers (simple remapping, frame caps) and what a
// player may type into a Join menu.

#include "kke/InputMap.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/NetModule.h"

#include <gtest/gtest.h>

#include <algorithm>

using kke::GameShellModule;
using kke::InputModule;

namespace {

size_t countFor(const kke::InputMap& map, const std::string& action, GameShellModule::DeviceClass cls) {
    size_t n = 0;
    for (size_t i : map.bindingsFor(action))
        if (GameShellModule::sourceIs(cls, map.bindings()[i].source)) ++n;
    return n;
}

} // namespace

TEST(GameShell, RebindReplacesOnlyThatDevicesBindings) {
    kke::InputMap map;
    map.defineAction({ "jump", "Jump", "Movement" });
    map.addBinding(InputModule::bind("jump", InputModule::key(SDL_SCANCODE_SPACE)));
    map.addBinding(InputModule::bind("jump", InputModule::key(SDL_SCANCODE_W)));
    map.addBinding(InputModule::bind("jump", InputModule::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    using DC = GameShellModule::DeviceClass;

    ASSERT_TRUE(GameShellModule::rebind(map, "jump", DC::KeyboardMouse, InputModule::key(SDL_SCANCODE_J)));
    EXPECT_EQ(countFor(map, "jump", DC::KeyboardMouse), 1u);
    EXPECT_EQ(countFor(map, "jump", DC::Controller), 1u); // the pad's A stays
    const auto keyboard = map.bindingsFor("jump");
    EXPECT_TRUE(std::any_of(keyboard.begin(), keyboard.end(), [&](size_t i) {
        return map.bindings()[i].source.kind == kke::SourceKind::Key && map.bindings()[i].source.code == SDL_SCANCODE_J;
    }));

    ASSERT_TRUE(GameShellModule::rebind(map, "jump", DC::Controller, InputModule::pad(SDL_GAMEPAD_BUTTON_NORTH)));
    EXPECT_EQ(countFor(map, "jump", DC::Controller), 1u);
    EXPECT_EQ(countFor(map, "jump", DC::KeyboardMouse), 1u);

    // A key isn't a controller button; unknown actions stay unknown.
    EXPECT_FALSE(GameShellModule::rebind(map, "jump", DC::Controller, InputModule::key(SDL_SCANCODE_K)));
    EXPECT_FALSE(GameShellModule::rebind(map, "fly", DC::KeyboardMouse, InputModule::key(SDL_SCANCODE_K)));
}

TEST(GameShell, OnlyGameButtonsAreRemappable) {
    kke::InputMap map;
    map.defineAction({ "jump", "Jump", "Movement" });
    map.defineAction({ "move", "Move", "Movement", "game", kke::ActionType::Axis2D });
    map.defineAction({ "ui.accept", "Accept", "Menus", "ui" });
    map.defineAction({ "shell.pause", "Pause", "Menus", "shell" });
    map.defineAction({ "debug.fly", "Fly", "Debug" });
    EXPECT_EQ(GameShellModule::remappable(map), std::vector<std::string>{ "jump" });
}

TEST(GameShell, FrameCapsSnapToTheNearestChoice) {
    EXPECT_EQ(GameShellModule::frameCaps()[0], 0);
    EXPECT_EQ(GameShellModule::frameCapIndex(0.0f), 0);
    EXPECT_EQ(GameShellModule::frameCaps()[static_cast<size_t>(GameShellModule::frameCapIndex(60.0f))], 60);
    EXPECT_EQ(GameShellModule::frameCaps()[static_cast<size_t>(GameShellModule::frameCapIndex(143.9f))], 144);
}

TEST(GameShell, TypedAddressesSplitIntoAddressAndPort) {
    std::string address;
    uint16_t port = 1;
    EXPECT_TRUE(kke::NetModule::splitTypedAddress("192.168.1.20", address, port));
    EXPECT_EQ(address, "192.168.1.20");
    EXPECT_EQ(port, 1);
    EXPECT_TRUE(kke::NetModule::splitTypedAddress(" 100.64.0.3:27961 ", address, port));
    EXPECT_EQ(address, "100.64.0.3");
    EXPECT_EQ(port, 27961);
    EXPECT_TRUE(kke::NetModule::splitTypedAddress("[fd7a::1]:27962", address, port));
    EXPECT_EQ(address, "fd7a::1");
    EXPECT_EQ(port, 27962);
    port = 5;
    EXPECT_TRUE(kke::NetModule::splitTypedAddress("fd7a::1", address, port)); // a bare IPv6 has no port
    EXPECT_EQ(address, "fd7a::1");
    EXPECT_EQ(port, 5);
    EXPECT_TRUE(kke::NetModule::splitTypedAddress("my-pc.lan", address, port));
    EXPECT_EQ(address, "my-pc.lan");
    EXPECT_FALSE(kke::NetModule::splitTypedAddress("my-pc.lan:http", address, port));
    EXPECT_FALSE(kke::NetModule::splitTypedAddress("my-pc.lan:70000", address, port));
}
