#include "kke/Lobby.h"

#include <gtest/gtest.h>

using kke::Lobby;

namespace {

Lobby::Press padPress(uint32_t pad) {
    Lobby::Press p;
    p.device = Lobby::Device::Pad;
    p.pad = pad;
    return p;
}
Lobby::Press keys() {
    Lobby::Press p;
    p.device = Lobby::Device::KeyboardMouse;
    return p;
}
Lobby::Press with(Lobby::Press p, bool Lobby::Press::*button) {
    p.*button = true;
    return p;
}

Lobby colourLobby() {
    Lobby l;
    l.addLookField({ "colour", "Colour", { "Sky", "Ember", "Moss", "Plum" }, {} });
    return l;
}

} // namespace

TEST(Lobby, PlayerOneIsInAndPinnedByTheirFirstPress) {
    Lobby l = colourLobby();
    EXPECT_EQ(l.joinedCount(), 1);
    EXPECT_EQ(l.seat(0).device, Lobby::Device::Any);
    l.handle(with(keys(), &Lobby::Press::confirm));
    EXPECT_EQ(l.seat(0).device, Lobby::Device::KeyboardMouse);
    EXPECT_EQ(l.joinedCount(), 1);
    EXPECT_EQ(l.seat(0).row, 0) << "the pinning press does nothing else";
}

TEST(Lobby, OtherControllersJoinWithA) {
    Lobby l = colourLobby();
    int joined = -1;
    l.onJoin = [&](int s) { joined = s; };
    l.handle(with(padPress(11), &Lobby::Press::confirm)); // player 1 on pad 11
    l.handle(with(padPress(22), &Lobby::Press::left));    // not A: nothing
    EXPECT_EQ(l.joinedCount(), 1);
    l.handle(with(padPress(22), &Lobby::Press::confirm));
    EXPECT_EQ(joined, 1);
    EXPECT_EQ(l.seatOfPad(22), 1);
    // The keyboard is free (player 1 is on a pad): Enter joins too.
    l.handle(with(keys(), &Lobby::Press::confirm));
    EXPECT_EQ(l.seatOfKeyboard(), 2);
    EXPECT_EQ(l.seatName(2), "Player 3");
    EXPECT_FALSE(l.toasts().empty());
}

TEST(Lobby, FullAfterFourAndBackLeaves) {
    Lobby l;
    l.handle(with(padPress(1), &Lobby::Press::confirm));
    for (uint32_t p = 2; p <= 5; ++p) l.handle(with(padPress(p), &Lobby::Press::confirm));
    EXPECT_EQ(l.joinedCount(), 4);
    EXPECT_EQ(l.seatOfPad(5), -1);
    l.handle(with(padPress(3), &Lobby::Press::back));
    EXPECT_EQ(l.joinedCount(), 3);
    EXPECT_FALSE(l.seat(2).joined);
    // Player 1 can't leave.
    l.handle(with(padPress(1), &Lobby::Press::back));
    EXPECT_TRUE(l.seat(0).joined);
    // The free seat is taken again first.
    l.handle(with(padPress(5), &Lobby::Press::confirm));
    EXPECT_EQ(l.seatOfPad(5), 2);
}

TEST(Lobby, LooksCycleAndSeatsStartDifferent) {
    Lobby l = colourLobby();
    EXPECT_EQ(l.seat(0).look[0], 0);
    EXPECT_EQ(l.seat(1).look[0], 1);
    l.handle(with(keys(), &Lobby::Press::confirm));
    l.handle(with(keys(), &Lobby::Press::left));
    EXPECT_EQ(l.seat(0).look[0], 3) << "looks wrap around";
    l.handle(with(keys(), &Lobby::Press::right));
    l.handle(with(keys(), &Lobby::Press::right));
    EXPECT_EQ(l.seat(0).look[0], 1);
}

TEST(Lobby, PlayerOneSetsCpusAndTheirDifficulty) {
    Lobby l = colourLobby();
    // Rows: colour, CPU players, then Start (no CPU rows while 0).
    EXPECT_EQ(l.rows(0).size(), 3u);
    EXPECT_EQ(l.rows(1).size(), 1u) << "only player 1 has the settings";
    l.handle(with(keys(), &Lobby::Press::confirm));
    l.handle(with(keys(), &Lobby::Press::down));
    for (int i = 0; i < 9; ++i) l.handle(with(keys(), &Lobby::Press::right));
    EXPECT_EQ(l.cpuCount(), 5) << "clamped at five";
    EXPECT_EQ(l.rows(0).size(), 8u);
    l.handle(with(keys(), &Lobby::Press::down)); // CPU 1
    l.handle(with(keys(), &Lobby::Press::right));
    EXPECT_EQ(l.cpuDifficulty(0), 2);
    EXPECT_EQ(l.difficulties()[2], "Hard");
    EXPECT_EQ(l.cpuDifficulty(1), 1);
    // Fewer, then more again: new ones start as hard as the one before.
    l.setCpuCount(1);
    l.setCpuCount(3);
    EXPECT_EQ(l.cpuDifficulty(1), 2);
    EXPECT_EQ(l.cpuDifficulty(2), 2);
    l.setMaxCpus(0);
    EXPECT_EQ(l.cpuCount(), 0);
    EXPECT_EQ(l.rows(0).size(), 2u);
}

TEST(Lobby, StartFromPlayerOneOnly) {
    Lobby l = colourLobby();
    l.handle(with(padPress(1), &Lobby::Press::confirm));
    l.handle(with(padPress(2), &Lobby::Press::confirm));
    l.handle(with(padPress(2), &Lobby::Press::start));
    EXPECT_FALSE(l.takeStart());
    l.handle(with(padPress(1), &Lobby::Press::start));
    EXPECT_TRUE(l.takeStart());
    EXPECT_FALSE(l.takeStart()) << "once";
    // A on the Start row starts too.
    l.handle(with(padPress(1), &Lobby::Press::down));
    l.handle(with(padPress(1), &Lobby::Press::down));
    l.handle(with(padPress(1), &Lobby::Press::confirm));
    EXPECT_TRUE(l.takeStart());
}

TEST(Lobby, ActionRowsAreForOtherModules) {
    Lobby l;
    int hosted = 0;
    Lobby::Option host;
    host.id = "net.host";
    host.label = "Host game";
    host.onPress = [&] { ++hosted; };
    l.addOption(host);
    l.setMaxCpus(0);
    l.handle(with(keys(), &Lobby::Press::confirm));
    l.handle(with(keys(), &Lobby::Press::confirm)); // the only row is Host game (no looks)
    EXPECT_EQ(hosted, 1);
    ASSERT_NE(l.option("net.host"), nullptr);
}

TEST(Lobby, InGameJoinsLeavePlayerOnesDevicesAlone) {
    Lobby l;
    l.setOpen(false);
    const std::vector<uint32_t> pads{ 7, 8 };
    // Player 1 wasn't pinned (no menu): the keyboard and pad 7 are theirs.
    l.handle(with(keys(), &Lobby::Press::confirm), pads);
    l.handle(with(padPress(7), &Lobby::Press::confirm), pads);
    EXPECT_EQ(l.joinedCount(), 1);
    l.handle(with(padPress(8), &Lobby::Press::confirm), pads);
    EXPECT_EQ(l.seatOfPad(8), 1);
    // In game, a joined player's buttons are the game's.
    l.handle(with(padPress(8), &Lobby::Press::back), pads);
    EXPECT_EQ(l.joinedCount(), 2);
}

TEST(Lobby, DevicesPerSeat) {
    Lobby l;
    const std::vector<uint32_t> pads{ 7, 8 }, kbm{ 100, 101 };
    EXPECT_TRUE(l.devicesFor(0, pads, kbm).empty()) << "alone: every device";
    EXPECT_EQ(l.devicesFor(1, pads, kbm), std::vector<uint32_t>{ Lobby::kNoDevice });
    l.handle(with(padPress(8), &Lobby::Press::confirm)); // player 1 is pad 8
    l.handle(with(keys(), &Lobby::Press::confirm));      // player 2 the keyboard
    EXPECT_EQ(l.devicesFor(0, pads, kbm), std::vector<uint32_t>{ 8 });
    EXPECT_EQ(l.devicesFor(1, pads, kbm), kbm);
}

TEST(Lobby, UnpinnedPlayerOneGetsWhatNobodyClaimed) {
    Lobby l;
    l.setOpen(false);
    const std::vector<uint32_t> pads{ 7, 8 }, kbm{ 100 };
    l.handle(with(padPress(8), &Lobby::Press::confirm), pads);
    const std::vector<uint32_t> one = l.devicesFor(0, pads, kbm);
    EXPECT_EQ(one, (std::vector<uint32_t>{ 7, 100 }));
}

TEST(Lobby, HotPlugToasts) {
    Lobby l;
    l.padConnected(5, true);
    EXPECT_TRUE(l.toasts().empty()) << "no toast for controllers there at the start";
    l.padConnected(6);
    ASSERT_EQ(l.toasts().size(), 1u);
    EXPECT_NE(l.toasts()[0].text.find("press A to join"), std::string::npos);
    l.handle(with(keys(), &Lobby::Press::confirm));
    l.handle(with(padPress(6), &Lobby::Press::confirm));
    l.padDisconnected(6);
    EXPECT_FALSE(l.seat(1).padPresent);
    EXPECT_TRUE(l.seat(1).joined) << "keeps the seat";
    l.padConnected(6);
    EXPECT_TRUE(l.seat(1).padPresent);
    l.update(10.0f);
    EXPECT_TRUE(l.toasts().empty());
}

TEST(Lobby, SavesLooksAndSettingsByName) {
    Lobby a = colourLobby();
    a.setLook(1, 0, 3);
    a.setCpuCount(2);
    a.setCpuDifficulty(1, 3);
    const nlohmann::json j = a.save();
    EXPECT_EQ(j["seats"][1]["look"]["colour"], "Plum");
    EXPECT_EQ(j["options"]["cpu.2"], "Expert");

    Lobby b;
    b.addLookField({ "colour", "Colour", { "Plum", "Sky" }, {} }); // other order, fewer
    b.load(j);
    EXPECT_EQ(b.seat(1).look[0], 0);
    EXPECT_EQ(b.cpuCount(), 2);
    EXPECT_EQ(b.cpuDifficulty(1), 3);
    b.load(nlohmann::json("nonsense"));
    EXPECT_EQ(b.cpuCount(), 2);
}

TEST(Lobby, NameFieldNamesTheSeat) {
    Lobby l;
    l.addLookField({ "name", "Name", { "Pip", "Juno" }, {} });
    EXPECT_EQ(l.seatName(0), "Pip");
    EXPECT_EQ(l.seatName(1), "Juno");
    EXPECT_EQ(l.seatName(2), "Pip");
}

TEST(Lobby, SeatsMoveOnlyToFreeDevices) {
    Lobby l;
    Lobby::Press pad1;
    pad1.device = Lobby::Device::Pad;
    pad1.pad = 11;
    pad1.confirm = true;
    l.handle(pad1); // player 1 takes pad 11
    ASSERT_EQ(l.join(Lobby::Device::Pad, 22), 1);
    ASSERT_EQ(l.join(Lobby::Device::KeyboardMouse), 2);
    EXPECT_EQ(l.seatOfDevice(Lobby::Device::Pad, 22), 1);
    EXPECT_EQ(l.seatOfDevice(Lobby::Device::Pad, 33), -1);

    // Nobody takes a device another player holds.
    EXPECT_FALSE(l.setSeatDevice(0, Lobby::Device::Pad, 22));
    EXPECT_FALSE(l.setSeatDevice(1, Lobby::Device::KeyboardMouse));
    EXPECT_EQ(l.seat(0).pad, 11u);
    // A free one (a flight stick plugged in later) is fine, and the old
    // one is free for someone else then.
    EXPECT_TRUE(l.setSeatDevice(0, Lobby::Device::Pad, 33));
    EXPECT_EQ(l.seatOfDevice(Lobby::Device::Pad, 33), 0);
    EXPECT_TRUE(l.setSeatDevice(2, Lobby::Device::Pad, 11));
    EXPECT_EQ(l.seatOfKeyboard(), -1);
    EXPECT_TRUE(l.setSeatDevice(1, Lobby::Device::KeyboardMouse));
    EXPECT_TRUE(l.setSeatDevice(1, Lobby::Device::KeyboardMouse)); // already theirs
    // Only joined seats, only real devices.
    EXPECT_FALSE(l.setSeatDevice(3, Lobby::Device::Pad, 44));
    EXPECT_FALSE(l.setSeatDevice(0, Lobby::Device::Any));
}

TEST(Lobby, TextRowsTypeKeepAndCancel) {
    Lobby l;
    std::string joined;
    l.addTextOption("net.address", "Address or code", "type it", [&](const std::string& t) { joined = t; });
    EXPECT_TRUE(l.isTextOption("net.address"));
    EXPECT_EQ(l.placeholder("net.address"), "type it");

    // Only what an address or a code can hold.
    l.startEditing("net.address");
    ASSERT_TRUE(l.editing());
    l.typeText("10.0.0.7:27961 <b>");
    EXPECT_EQ(l.text("net.address"), "10.0.0.7:27961b");
    l.backspace();
    l.finishEditing(true);
    EXPECT_FALSE(l.editing());
    EXPECT_EQ(joined, "10.0.0.7:27961");

    // Cancelling puts back what was there.
    l.startEditing("net.address");
    l.typeText("xyz");
    l.finishEditing(false);
    EXPECT_EQ(l.text("net.address"), "10.0.0.7:27961");
}

TEST(Lobby, OnScreenKeyboardTypesWithAPad) {
    Lobby l;
    std::string joined;
    l.addTextOption("net.address", "Address or code", {}, [&](const std::string& t) { joined = t; });
    const Lobby::Press pad = padPress(7);
    l.handle(with(pad, &Lobby::Press::confirm)); // player 1 takes pad 7
    // Down to the text row (after the CPU row) and A opens the keyboard.
    int guard = 0;
    while (!l.editing() && guard++ < 20) {
        const std::vector<Lobby::Row> rows = l.rows(0);
        const Lobby::Row row = rows[static_cast<size_t>(l.seat(0).row)];
        if (row.kind == Lobby::Row::Kind::Option && l.options()[static_cast<size_t>(row.index)].id == "net.address")
            l.handle(with(pad, &Lobby::Press::confirm));
        else
            l.handle(with(pad, &Lobby::Press::down));
    }
    ASSERT_TRUE(l.editing());
    // A types the key under the cursor: "1", then right twice: "3".
    l.handle(with(pad, &Lobby::Press::confirm));
    l.handle(with(pad, &Lobby::Press::right));
    l.handle(with(pad, &Lobby::Press::right));
    l.handle(with(pad, &Lobby::Press::confirm));
    EXPECT_EQ(l.text("net.address"), "13");
    // B deletes a letter; Start is Done.
    l.handle(with(pad, &Lobby::Press::back));
    EXPECT_EQ(l.text("net.address"), "1");
    l.handle(with(pad, &Lobby::Press::start));
    EXPECT_FALSE(l.editing());
    EXPECT_EQ(joined, "1");
    // Start while typing finished the typing, it didn't start the game.
    EXPECT_FALSE(l.takeStart());
    // The last row: Delete, Clear, Done.
    const auto& keys = Lobby::keyboardKeys();
    EXPECT_EQ(keys.back().back(), Lobby::kKeyDone);
}

TEST(Lobby, TextRowsAreSavedEvenWhenAddedAfterLoading) {
    Lobby a;
    a.addTextOption("net.address", "Address or code");
    a.setText("net.address", "K7M-Q2P");
    const nlohmann::json saved = a.save();

    Lobby b;
    b.load(saved); // before the game adds its online rows
    EXPECT_EQ(b.save()["options"]["net.address"], "K7M-Q2P"); // kept meanwhile
    b.addTextOption("net.address", "Address or code");
    EXPECT_EQ(b.text("net.address"), "K7M-Q2P");
}
