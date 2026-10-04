#include "kke/Inventory.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

// kke::Inventory: a Tarkov-style grid of items that take several cells,
// turn to fit, stack, weigh something, and go into equipment slots.

namespace {

using kke::EquipSlot;
using kke::Inventory;
using kke::ItemCatalog;

ItemCatalog catalog() {
    ItemCatalog c;
    std::string error;
    const bool ok = c.load(nlohmann::json::parse(R"({ "items": [
        { "id": "rifle", "name": "Rifle", "size": [4, 1], "weight": 3.5, "equip": ["RightHand", "Back"] },
        { "id": "axe", "size": [1, 3], "weight": 1.6, "equip": ["RightHand", "Back"] },
        { "id": "ammo", "size": [1, 1], "weight": 0.02, "stack": 30 },
        { "id": "box", "size": [2, 2], "weight": 5 }
    ] })"),
                           &error);
    EXPECT_TRUE(ok) << error;
    return c;
}

TEST(Inventory, CatalogRejectsBadItems) {
    ItemCatalog c;
    std::string error;
    EXPECT_FALSE(c.load(nlohmann::json::parse(R"({ "items": [ { "name": "no id" } ] })"), &error));
    EXPECT_FALSE(c.load(nlohmann::json::parse(R"({ "items": [ { "id": "a", "equip": ["Pocket"] } ] })"), &error));
    EXPECT_NE(error.find("Pocket"), std::string::npos);
    EXPECT_TRUE(c.load(nlohmann::json::parse(R"({ "items": [ { "id": "b" } ] })")));
    EXPECT_FALSE(c.load(nlohmann::json::parse(R"({ "items": [ { "id": "b" } ] })"), &error)); // twice
}

TEST(Inventory, ItemsTakeTheirCellsAndNeverOverlap) {
    const ItemCatalog c = catalog();
    Inventory inv(6, 3);
    EXPECT_EQ(inv.add(c, "rifle"), 0);
    EXPECT_EQ(inv.add(c, "box"), 0);
    // The rifle along the top row, the box in the two columns after it.
    const kke::InventoryItem* rifle = inv.at(0, 0);
    ASSERT_NE(rifle, nullptr);
    EXPECT_EQ(rifle->id, "rifle");
    EXPECT_EQ(inv.at(3, 0), rifle);
    const kke::InventoryItem* box = inv.at(4, 0);
    ASSERT_NE(box, nullptr);
    EXPECT_EQ(box->id, "box");
    EXPECT_EQ(inv.at(5, 1), box);
    // Every cell is covered at most once.
    for (int y = 0; y < inv.rows(); ++y)
        for (int x = 0; x < inv.columns(); ++x) {
            int covering = 0;
            for (const auto& it : inv.items())
                covering += it.slot < 0 && x >= it.x && x < it.x + it.w && y >= it.y && y < it.y + it.h;
            EXPECT_LE(covering, 1) << x << "," << y;
        }
}

TEST(Inventory, TurnsAnItemThatOnlyFitsTurned) {
    const ItemCatalog c = catalog();
    Inventory inv(3, 4); // a rifle (4 x 1) only fits standing up
    EXPECT_EQ(inv.add(c, "rifle"), 0);
    ASSERT_EQ(inv.items().size(), 1u);
    EXPECT_TRUE(inv.items()[0].rotated);
    EXPECT_EQ(inv.items()[0].w, 1);
    EXPECT_EQ(inv.items()[0].h, 4);
}

TEST(Inventory, StacksAndReportsWhatDidNotFit) {
    const ItemCatalog c = catalog();
    Inventory inv(2, 1);
    EXPECT_EQ(inv.add(c, "ammo", 45), 0); // 30 + 15 in two cells
    EXPECT_EQ(inv.items().size(), 2u);
    EXPECT_EQ(inv.add(c, "ammo", 20), 5); // 15 fit on the second stack
    EXPECT_EQ(inv.count("ammo"), 60);
    EXPECT_EQ(inv.removeById("ammo", 40), 40);
    EXPECT_EQ(inv.count("ammo"), 20);
    EXPECT_EQ(inv.add(c, "nothing"), 1); // unknown: not added
}

TEST(Inventory, MovesOnlyWhereThereIsRoom) {
    const ItemCatalog c = catalog();
    Inventory inv(4, 4);
    inv.add(c, "box");
    inv.add(c, "axe");
    const uint32_t box = inv.at(0, 0)->uid;
    const uint32_t axe = inv.at(2, 0)->uid;
    EXPECT_FALSE(inv.move(c, box, 1, 0, false)); // onto the axe
    EXPECT_FALSE(inv.move(c, box, 3, 0, false)); // off the edge
    EXPECT_TRUE(inv.move(c, box, 0, 2, false));
    EXPECT_TRUE(inv.move(c, axe, 0, 1, true)); // lying down: 3 x 1
    EXPECT_EQ(inv.at(2, 1)->uid, axe);
    EXPECT_EQ(inv.at(0, 3)->uid, box);
}

TEST(Inventory, EquipsOutOfTheGridAndSwaps) {
    const ItemCatalog c = catalog();
    Inventory inv(6, 3);
    inv.add(c, "rifle");
    inv.add(c, "axe");
    inv.add(c, "box");
    const uint32_t rifle = inv.at(0, 0)->uid;
    EXPECT_FALSE(inv.equip(c, rifle, EquipSlot::Head)); // not allowed there
    EXPECT_TRUE(inv.equip(c, rifle, EquipSlot::RightHand));
    EXPECT_EQ(inv.at(0, 0), nullptr); // its cells are free
    ASSERT_NE(inv.equipped(EquipSlot::RightHand), nullptr);
    // The axe into the same hand: the rifle goes back into the grid.
    uint32_t axe = 0;
    for (const auto& it : inv.items())
        if (it.id == "axe") axe = it.uid;
    EXPECT_TRUE(inv.equip(c, axe, EquipSlot::RightHand));
    EXPECT_EQ(inv.equipped(EquipSlot::RightHand)->id, "axe");
    EXPECT_GE(inv.find(rifle)->x, 0);
    EXPECT_TRUE(inv.unequip(c, EquipSlot::RightHand));
    EXPECT_EQ(inv.equipped(EquipSlot::RightHand), nullptr);
    EXPECT_NEAR(inv.weight(c), 3.5f + 1.6f + 5.0f, 1e-4f);
}

TEST(Inventory, SortsBiggestFirstAndSavesAndLoads) {
    const ItemCatalog c = catalog();
    Inventory inv(6, 4);
    inv.add(c, "ammo", 10);
    inv.add(c, "axe");
    inv.add(c, "box");
    EXPECT_TRUE(inv.sort(c));
    EXPECT_EQ(inv.at(0, 0)->id, "box"); // 4 cells, then the axe's 3, then the ammo
    const nlohmann::json saved = inv.save();
    Inventory back(6, 4);
    EXPECT_EQ(back.load(c, saved), 0);
    EXPECT_EQ(back.count("ammo"), 10);
    EXPECT_EQ(back.count("axe"), 1);
    EXPECT_EQ(back.count("box"), 1);
    Inventory small(2, 2); // the axe (1 x 3) no longer fits where it was
    EXPECT_GE(small.load(c, saved), 1);
}

TEST(Inventory, HeavyLoadsSlowYouDown) {
    EXPECT_FLOAT_EQ(kke::loadSpeedFactor(5.0f, 30.0f), 1.0f);
    EXPECT_FLOAT_EQ(kke::loadSpeedFactor(15.0f, 30.0f), 1.0f);
    EXPECT_NEAR(kke::loadSpeedFactor(30.0f, 30.0f), 0.6f, 1e-5f);
    EXPECT_LT(kke::loadSpeedFactor(22.5f, 30.0f), 1.0f);
    EXPECT_FLOAT_EQ(kke::loadSpeedFactor(40.0f, 30.0f), 0.35f);
}

} // namespace
