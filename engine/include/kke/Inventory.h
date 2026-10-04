#pragma once

#include "kke/Equipment.h"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// A grid inventory, the Escape from Tarkov / Resident Evil kind (Kees,
// 2026-09-28): every item takes a rectangle of cells (a pistol 2 x 1, a
// rifle 4 x 1, a first-aid kit 1 x 1), may be turned a quarter turn to
// fit, has a weight, a description and a picture; things of one kind
// stack in one place up to their stack size. What a character wears or
// holds goes in equipment slots (kke::EquipSlot: hands, back, hips, head)
// out of the grid. The weight of everything slows the character down
// (loadSpeedFactor).
//
// Pure data, no engine: tests/test_inventory.cpp checks it on its own. A
// game shows it with its own screen (games/showcase/InventoryScreen.cpp
// is an RmlUi one) and saves it with save() / load().

struct ItemDef {
    std::string id;           // "rifle", "axe", "flower_red"
    std::string name;         // "Hunting rifle"
    std::string description;  // a line or two for the details panel
    std::string category;     // "Weapon", "Tool", "Food", "Material", ...
    std::string icon;         // a sprite name (kke::resolveSprite), or "" for a coloured tile
    glm::vec3 color{0.55f};   // the tile's colour when there's no icon
    int width = 1, height = 1; // cells, unturned
    float weight = 0.0f;      // kg, one of it
    int maxStack = 1;         // how many share one place in the grid
    uint32_t equipSlots = 0;  // slotBit(EquipSlot::...) where it may be equipped; 0 = it can't
};

// Every kind of item a game has, by id. Loads from a data file (JSON or
// YAML via kke::datafile): { "items": [ { "id": "axe", "name": "Axe",
// "size": [1, 3], "weight": 1.6, "equip": ["RightHand", "Back"], ... } ] }.
class ItemCatalog {
public:
    // False (and nothing added) when the id is empty or already there.
    bool add(ItemDef def);
    const ItemDef* find(const std::string& id) const;
    const std::vector<ItemDef>& all() const { return m_items; }
    // Adds every item of `data` ({ "items": [...] }); false with `error`
    // at the first bad one (the items before it stay added).
    bool load(const nlohmann::json& data, std::string* error = nullptr);

private:
    std::vector<ItemDef> m_items;
};

// One thing in the inventory: a stack of `count` items of one kind, at
// cell (x, y) of the grid (its top-left corner), or in an equipment slot.
struct InventoryItem {
    uint32_t uid = 0;         // unique in its inventory, never 0
    std::string id;           // ItemDef::id
    int count = 1;
    int x = 0, y = 0;         // grid cell of its top-left corner (-1 when equipped)
    int w = 1, h = 1;         // the cells it covers, turned or not
    bool rotated = false;     // a quarter turn: ItemDef's width and height swapped
    int slot = -1;            // EquipSlot when equipped, else -1
};

class Inventory {
public:
    Inventory(int columns = 10, int rows = 6, float maxWeight = 30.0f);

    int columns() const { return m_cols; }
    int rows() const { return m_rows; }
    float maxWeight() const { return m_maxWeight; }
    void setMaxWeight(float kg) { m_maxWeight = kg; }

    // Adds `count` items: first onto stacks of the same kind that have
    // room, then into the first free space (top-left first, row by row,
    // turned if it fits only that way). Returns how many did NOT fit (0 =
    // all in). Unknown ids add nothing.
    int add(const ItemCatalog& catalog, const std::string& id, int count = 1);
    // Would an item of `def` fit with its corner at (x, y)? `ignore` is
    // an item not to count (the one being moved).
    bool fits(const ItemDef& def, int x, int y, bool rotated, uint32_t ignore = 0) const;
    // Moves an item in the grid (or out of its equipment slot into the
    // grid). Dropped on a stack of the same kind it merges as much as
    // the stack takes. False, changing nothing, when it doesn't fit.
    bool move(const ItemCatalog& catalog, uint32_t uid, int x, int y, bool rotated);
    // Takes `count` from an item (all of it with count < 0 or >= its
    // count). Returns how many it took.
    int remove(uint32_t uid, int count = -1);
    // Takes `count` of a kind from wherever it is. Returns how many.
    int removeById(const std::string& id, int count = 1);
    void clear();

    // Equipment: moves an item from the grid into `slot` (it must be
    // allowed there: ItemDef::equipSlots). What was in the slot goes back
    // into the grid if it fits; if it doesn't, nothing changes (false).
    bool equip(const ItemCatalog& catalog, uint32_t uid, EquipSlot slot);
    // Back into the grid (first free space). False when there's no room.
    bool unequip(const ItemCatalog& catalog, EquipSlot slot);
    // What's in a slot (nullptr = empty).
    const InventoryItem* equipped(EquipSlot slot) const;

    const std::vector<InventoryItem>& items() const { return m_items; }
    const InventoryItem* find(uint32_t uid) const;
    // The item covering a cell (nullptr = empty, or outside the grid).
    const InventoryItem* at(int x, int y) const;
    int count(const std::string& id) const;
    // Everything, equipped things included, in kg.
    float weight(const ItemCatalog& catalog) const;
    // Rearranges the grid: biggest first, top-left first. False (nothing
    // moved) if that wouldn't fit everything, which can happen when the
    // grid is nearly full of odd shapes.
    bool sort(const ItemCatalog& catalog);

    nlohmann::json save() const;
    // Replaces the contents. Items whose id the catalog doesn't know, or
    // that no longer fit (a smaller grid), are left out; returns how many.
    int load(const ItemCatalog& catalog, const nlohmann::json& data);

private:
    InventoryItem* findMutable(uint32_t uid);
    bool firstFree(const ItemDef& def, int& x, int& y, bool& rotated, uint32_t ignore = 0) const;
    int m_cols, m_rows;
    float m_maxWeight;
    uint32_t m_nextUid = 1;
    std::vector<InventoryItem> m_items;
};

// How fast someone carrying `weight` of `maxWeight` moves, 0..1: full
// speed up to half the limit, then slower, 0.6 at the limit, 0.35 over it.
float loadSpeedFactor(float weight, float maxWeight);

} // namespace kke
