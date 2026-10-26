// Save slots (kke/SaveSlots.h, ENG-F53): the file format, the slot list, and
// the cloud hook's import.

#include "kke/SaveSlots.h"
#include "kke/storage/Store.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace kke;

namespace {

struct Slots {
    std::unique_ptr<storage::Store> store = storage::openStore("memory:");
    SaveSlots slots{ *store, "My Game" };
    int64_t clock = 1'700'000'000'000;
    Slots() {
        slots.now = [this] { return clock; };
    }
};

} // namespace

TEST(SaveSlots, WritesReadsAndListsNewestFirst) {
    Slots s;
    EXPECT_EQ(s.slots.collection(), "saves.my_game");
    std::string err;
    ASSERT_TRUE(s.slots.write("slot 1", std::string("first\0bytes", 11), "The docks", 3600.5, &err)) << err;
    s.clock += 60'000;
    s.slots.setVersion(3);
    ASSERT_TRUE(s.slots.write("auto", "second", "Chapter 2", 4000.0));

    const std::optional<SaveFile> one = s.slots.read("slot 1", &err);
    ASSERT_TRUE(one) << err;
    EXPECT_EQ(one->data, std::string("first\0bytes", 11));
    EXPECT_EQ(one->info.title, "The docks");
    EXPECT_EQ(one->info.version, 1u);
    EXPECT_EQ(one->info.playSeconds, 3600.5);
    EXPECT_EQ(one->info.savedAtMs, 1'700'000'000'000);
    EXPECT_EQ(one->info.dataBytes, 11u);

    const std::vector<SaveInfo> list = s.slots.list();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].slot, "auto"); // newest first
    EXPECT_EQ(list[0].version, 3u);
    EXPECT_EQ(list[1].slot, "slot 1");
    EXPECT_EQ(list[1].dataBytes, 11u);

    // Writing a slot again replaces it; removing empties it.
    s.clock += 1000;
    ASSERT_TRUE(s.slots.write("slot 1", "again", "", 0.0));
    EXPECT_EQ(s.slots.read("slot 1")->data, "again");
    EXPECT_EQ(s.slots.list()[0].slot, "slot 1");
    EXPECT_TRUE(s.slots.exists("slot 1"));
    ASSERT_TRUE(s.slots.remove("slot 1"));
    EXPECT_FALSE(s.slots.exists("slot 1"));
    EXPECT_FALSE(s.slots.read("slot 1", &err));
    EXPECT_NE(err.find("nothing is saved"), std::string::npos) << err;
    EXPECT_EQ(s.slots.list().size(), 1u);
}

TEST(SaveSlots, RefusesBadNamesTooMuchDataAndTooManySlots) {
    Slots s;
    std::string err;
    for (const char* bad : { "", " lead", "trail ", "a/b", "dots..ok?" }) EXPECT_FALSE(s.slots.write(bad, "x", "", 0, &err)) << bad;
    EXPECT_FALSE(s.slots.write(std::string(65, 'a'), "x", "", 0));
    EXPECT_TRUE(s.slots.write(std::string(64, 'a'), "x", "", 0));
    EXPECT_FALSE(s.slots.write("big", std::string(SaveSlots::kMaxData + 1, 'x'), "", 0, &err));
    EXPECT_NE(err.find("MB"), std::string::npos) << err;
    for (size_t i = 1; i < SaveSlots::kMaxSlots; ++i) ASSERT_TRUE(s.slots.write("s" + std::to_string(i), "x", "", 0)) << i;
    EXPECT_FALSE(s.slots.write("one more", "x", "", 0, &err));
    EXPECT_NE(err.find("remove one first"), std::string::npos) << err;
    EXPECT_TRUE(s.slots.write("s1", "replacing is fine", "", 0)); // a used slot can still be written
    // A long title is cut, not refused; NaN play time is 0.
    ASSERT_TRUE(s.slots.write("s2", "x", std::string(500, 't'), std::nan("")));
    EXPECT_EQ(s.slots.read("s2")->info.title.size(), SaveSlots::kMaxTitle);
    EXPECT_EQ(s.slots.read("s2")->info.playSeconds, 0.0);
}

TEST(SaveSlots, FindsEveryDamagedByte) {
    SaveInfo info;
    info.slot = "slot 1";
    info.title = "t";
    info.version = 2;
    info.savedAtMs = 5;
    info.dataBytes = 4;
    const std::string file = SaveSlots::encode(info, "data");
    ASSERT_TRUE(SaveSlots::decode(file));
    EXPECT_EQ(SaveSlots::crc32("123456789", 9), 0xCBF43926u); // the standard check value
    for (size_t i = 0; i < file.size(); ++i) {
        std::string bad = file;
        bad[i] = static_cast<char>(bad[i] ^ 0x10);
        EXPECT_FALSE(SaveSlots::decode(bad)) << "byte " << i;
    }
    for (size_t n = 0; n < file.size(); ++n) EXPECT_FALSE(SaveSlots::decode(file.substr(0, n))) << "cut at " << n;
    std::string err;
    EXPECT_FALSE(SaveSlots::decode(file + "x", &err));
    EXPECT_FALSE(SaveSlots::decode("KKSAVE nonsense", &err));
}

TEST(SaveSlots, CloudHookUploadsAndImportsTheNewer) {
    Slots pc, phone;
    std::vector<std::string> uploaded;
    std::vector<std::string> removed;
    pc.slots.onWritten = [&](const SaveInfo& info, const std::string& file) {
        EXPECT_EQ(info.slot, "slot 1");
        uploaded.push_back(file);
    };
    pc.slots.onRemoved = [&](const std::string& slot) { removed.push_back(slot); };
    ASSERT_TRUE(pc.slots.write("slot 1", "from the pc", "PC", 10));
    ASSERT_EQ(uploaded.size(), 1u);
    EXPECT_EQ(pc.slots.fileOf("slot 1"), uploaded[0]);

    // The phone takes it into an empty slot ...
    std::string err;
    EXPECT_EQ(phone.slots.importFile(uploaded[0], &err), SaveSlots::Import::Taken) << err;
    EXPECT_EQ(phone.slots.read("slot 1")->data, "from the pc");
    EXPECT_EQ(phone.slots.list().size(), 1u);
    // ... not again (the same age), not an older one ...
    EXPECT_EQ(phone.slots.importFile(uploaded[0]), SaveSlots::Import::Older);
    phone.clock += 5000;
    ASSERT_TRUE(phone.slots.write("slot 1", "played on the phone", "Phone", 20));
    EXPECT_EQ(phone.slots.importFile(uploaded[0]), SaveSlots::Import::Older);
    EXPECT_EQ(phone.slots.read("slot 1")->data, "played on the phone");
    // ... and the pc takes the phone's newer one.
    EXPECT_EQ(pc.slots.importFile(*phone.slots.fileOf("slot 1")), SaveSlots::Import::Taken);
    EXPECT_EQ(pc.slots.read("slot 1")->info.title, "Phone");
    // Damaged files and slot headers are refused.
    std::string bad = uploaded[0];
    bad[bad.size() / 2] ^= 1;
    EXPECT_EQ(pc.slots.importFile(bad), SaveSlots::Import::Damaged);
    SaveInfo header = pc.slots.list()[0];
    EXPECT_EQ(pc.slots.importFile(SaveSlots::encode(header, {})), SaveSlots::Import::Damaged);

    ASSERT_TRUE(pc.slots.remove("slot 1"));
    EXPECT_EQ(removed, std::vector<std::string>{ "slot 1" });
}
