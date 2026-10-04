#include "kke/Inventory.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace kke {

namespace {
// How data files and saves name the equipment slots (EquipSlot order).
constexpr const char* kSlotKeys[kEquipSlots] = { "LeftHand", "RightHand", "Back", "HipLeft", "HipRight", "Head" };
int slotFromKey(const std::string& key) {
    for (size_t k = 0; k < kEquipSlots; ++k)
        if (key == kSlotKeys[k]) return static_cast<int>(k);
    return -1;
}
} // namespace

// ---------------------------------------------------------------- catalog

bool ItemCatalog::add(ItemDef def) {
    if (def.id.empty() || find(def.id)) return false;
    def.width = std::max(1, def.width);
    def.height = std::max(1, def.height);
    def.maxStack = std::max(1, def.maxStack);
    def.weight = std::max(0.0f, def.weight);
    m_items.push_back(std::move(def));
    return true;
}

const ItemDef* ItemCatalog::find(const std::string& id) const {
    for (const ItemDef& d : m_items)
        if (d.id == id) return &d;
    return nullptr;
}

bool ItemCatalog::load(const nlohmann::json& data, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (!data.is_object() || !data.contains("items") || !data["items"].is_array()) return fail("no \"items\" list");
    for (const nlohmann::json& j : data["items"]) {
        if (!j.is_object()) return fail("an item that isn't an object");
        ItemDef d;
        d.id = j.value("id", "");
        if (d.id.empty()) return fail("an item without an id");
        d.name = j.value("name", d.id);
        d.description = j.value("description", "");
        d.category = j.value("category", "");
        d.icon = j.value("icon", "");
        d.weight = j.value("weight", 0.0f);
        d.maxStack = j.value("stack", 1);
        if (j.contains("size")) {
            const nlohmann::json& s = j["size"];
            if (!s.is_array() || s.size() != 2 || !s[0].is_number_integer() || !s[1].is_number_integer())
                return fail(d.id + ": \"size\" must be [width, height] in cells");
            d.width = s[0].get<int>();
            d.height = s[1].get<int>();
        }
        if (j.contains("color")) {
            const nlohmann::json& c = j["color"];
            if (!c.is_array() || c.size() != 3 || !c[0].is_number() || !c[1].is_number() || !c[2].is_number())
                return fail(d.id + ": \"color\" must be [r, g, b]");
            d.color = glm::vec3(c[0].get<float>(), c[1].get<float>(), c[2].get<float>());
        }
        if (j.contains("equip")) {
            if (!j["equip"].is_array()) return fail(d.id + ": \"equip\" must be a list of slots");
            for (const nlohmann::json& slot : j["equip"]) {
                const std::string name = slot.is_string() ? slot.get<std::string>() : "";
                const int k = slotFromKey(name);
                if (k < 0) return fail(d.id + ": no equipment slot called \"" + name + "\" (LeftHand, RightHand, Back, HipLeft, HipRight, Head)");
                d.equipSlots |= slotBit(static_cast<EquipSlot>(k));
            }
        }
        const std::string id = d.id;
        if (!add(std::move(d))) return fail("the id \"" + id + "\" is there twice");
    }
    return true;
}

// ---------------------------------------------------------------- inventory

Inventory::Inventory(int columns, int rows, float maxWeight) : m_cols(std::max(1, columns)), m_rows(std::max(1, rows)), m_maxWeight(maxWeight) {}

InventoryItem* Inventory::findMutable(uint32_t uid) {
    for (InventoryItem& it : m_items)
        if (it.uid == uid) return &it;
    return nullptr;
}

const InventoryItem* Inventory::find(uint32_t uid) const {
    for (const InventoryItem& it : m_items)
        if (it.uid == uid) return &it;
    return nullptr;
}

const InventoryItem* Inventory::at(int x, int y) const {
    for (const InventoryItem& it : m_items)
        if (it.slot < 0 && x >= it.x && x < it.x + it.w && y >= it.y && y < it.y + it.h) return &it;
    return nullptr;
}

const InventoryItem* Inventory::equipped(EquipSlot slot) const {
    for (const InventoryItem& it : m_items)
        if (it.slot == static_cast<int>(slot)) return &it;
    return nullptr;
}

bool Inventory::fits(const ItemDef& def, int x, int y, bool rotated, uint32_t ignore) const {
    const int w = rotated ? def.height : def.width, h = rotated ? def.width : def.height;
    if (x < 0 || y < 0 || x + w > m_cols || y + h > m_rows) return false;
    for (const InventoryItem& it : m_items) {
        if (it.uid == ignore || it.slot >= 0) continue;
        if (x < it.x + it.w && it.x < x + w && y < it.y + it.h && it.y < y + h) return false;
    }
    return true;
}

bool Inventory::firstFree(const ItemDef& def, int& x, int& y, bool& rotated, uint32_t ignore) const {
    // Unturned anywhere first (a reader expects things the way they are
    // drawn), then turned.
    for (int turn = 0; turn < 2; ++turn) {
        if (turn == 1 && def.width == def.height) break;
        for (int yy = 0; yy < m_rows; ++yy)
            for (int xx = 0; xx < m_cols; ++xx)
                if (fits(def, xx, yy, turn == 1, ignore)) {
                    x = xx;
                    y = yy;
                    rotated = turn == 1;
                    return true;
                }
    }
    return false;
}

int Inventory::add(const ItemCatalog& catalog, const std::string& id, int count) {
    const ItemDef* def = catalog.find(id);
    if (!def || count <= 0) return std::max(0, count);
    // Onto stacks with room (in the grid; an equipped stack too, the
    // way a quiver takes arrows).
    for (InventoryItem& it : m_items) {
        if (count == 0) break;
        if (it.id != id || it.count >= def->maxStack) continue;
        const int take = std::min(count, def->maxStack - it.count);
        it.count += take;
        count -= take;
    }
    while (count > 0) {
        InventoryItem it;
        bool rotated = false;
        if (!firstFree(*def, it.x, it.y, rotated)) break;
        it.uid = m_nextUid++;
        it.id = id;
        it.rotated = rotated;
        it.w = rotated ? def->height : def->width;
        it.h = rotated ? def->width : def->height;
        it.count = std::min(count, def->maxStack);
        count -= it.count;
        m_items.push_back(std::move(it));
    }
    return count;
}

bool Inventory::move(const ItemCatalog& catalog, uint32_t uid, int x, int y, bool rotated) {
    InventoryItem* it = findMutable(uid);
    const ItemDef* def = it ? catalog.find(it->id) : nullptr;
    if (!def) return false;
    // Onto a stack of the same kind: merge what fits.
    if (const InventoryItem* there = at(x, y); there && there->uid != uid && there->id == it->id && def->maxStack > 1) {
        InventoryItem* stack = findMutable(there->uid);
        const int take = std::min(it->count, def->maxStack - stack->count);
        if (take <= 0) return false;
        stack->count += take;
        it->count -= take;
        if (it->count == 0) m_items.erase(m_items.begin() + (it - m_items.data()));
        return true;
    }
    if (!fits(*def, x, y, rotated, uid)) return false;
    it->x = x;
    it->y = y;
    it->rotated = rotated;
    it->w = rotated ? def->height : def->width;
    it->h = rotated ? def->width : def->height;
    it->slot = -1;
    return true;
}

int Inventory::remove(uint32_t uid, int count) {
    InventoryItem* it = findMutable(uid);
    if (!it) return 0;
    if (count < 0 || count >= it->count) {
        const int all = it->count;
        m_items.erase(m_items.begin() + (it - m_items.data()));
        return all;
    }
    it->count -= count;
    return count;
}

int Inventory::removeById(const std::string& id, int count) {
    int taken = 0;
    // Smallest stacks first, so the big ones stay whole.
    while (taken < count) {
        InventoryItem* smallest = nullptr;
        for (InventoryItem& it : m_items)
            if (it.id == id && (!smallest || it.count < smallest->count)) smallest = &it;
        if (!smallest) break;
        taken += remove(smallest->uid, count - taken);
    }
    return taken;
}

void Inventory::clear() { m_items.clear(); }

bool Inventory::equip(const ItemCatalog& catalog, uint32_t uid, EquipSlot slot) {
    InventoryItem* it = findMutable(uid);
    const ItemDef* def = it ? catalog.find(it->id) : nullptr;
    if (!def || !(def->equipSlots & slotBit(slot))) return false;
    if (it->slot == static_cast<int>(slot)) return true;
    // What's in the slot goes where this one was (or anywhere free).
    if (const InventoryItem* old = equipped(slot)) {
        const ItemDef* oldDef = catalog.find(old->id);
        const uint32_t oldUid = old->uid;
        int x = 0, y = 0;
        bool rotated = false;
        // Free the cells this one takes for the swap.
        const int keepSlot = it->slot;
        it->slot = 0x7fff; // out of the grid for the search
        const bool room = oldDef && firstFree(*oldDef, x, y, rotated);
        it->slot = keepSlot;
        if (!room) return false;
        InventoryItem* o = findMutable(oldUid);
        o->slot = -1;
        o->x = x;
        o->y = y;
        o->rotated = rotated;
        o->w = rotated ? oldDef->height : oldDef->width;
        o->h = rotated ? oldDef->width : oldDef->height;
        it = findMutable(uid);
    }
    it->slot = static_cast<int>(slot);
    it->x = it->y = -1;
    return true;
}

bool Inventory::unequip(const ItemCatalog& catalog, EquipSlot slot) {
    const InventoryItem* in = equipped(slot);
    const ItemDef* def = in ? catalog.find(in->id) : nullptr;
    if (!def) return false;
    InventoryItem* it = findMutable(in->uid);
    int x = 0, y = 0;
    bool rotated = false;
    if (!firstFree(*def, x, y, rotated)) return false;
    it->slot = -1;
    it->x = x;
    it->y = y;
    it->rotated = rotated;
    it->w = rotated ? def->height : def->width;
    it->h = rotated ? def->width : def->height;
    return true;
}

int Inventory::count(const std::string& id) const {
    int n = 0;
    for (const InventoryItem& it : m_items)
        if (it.id == id) n += it.count;
    return n;
}

float Inventory::weight(const ItemCatalog& catalog) const {
    float kg = 0.0f;
    for (const InventoryItem& it : m_items)
        if (const ItemDef* d = catalog.find(it.id)) kg += d->weight * static_cast<float>(it.count);
    return kg;
}

bool Inventory::sort(const ItemCatalog& catalog) {
    std::vector<InventoryItem> before = m_items;
    std::vector<size_t> order;
    for (size_t k = 0; k < m_items.size(); ++k)
        if (m_items[k].slot < 0) order.push_back(k);
    // Biggest first, then by category and name, so like sits with like.
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const ItemDef* da = catalog.find(m_items[a].id);
        const ItemDef* db = catalog.find(m_items[b].id);
        const int sa = da ? da->width * da->height : 1, sb = db ? db->width * db->height : 1;
        if (sa != sb) return sa > sb;
        if (da && db && da->category != db->category) return da->category < db->category;
        return m_items[a].id < m_items[b].id;
    });
    // Everything out of the grid, then back in that order.
    for (size_t k : order) m_items[k].slot = 0x7fff;
    for (size_t k : order) {
        InventoryItem& it = m_items[k];
        const ItemDef* def = catalog.find(it.id);
        bool rotated = false;
        it.slot = -1;
        if (!def || !firstFree(*def, it.x, it.y, rotated, it.uid)) {
            m_items = std::move(before);
            return false;
        }
        it.rotated = rotated;
        it.w = rotated ? def->height : def->width;
        it.h = rotated ? def->width : def->height;
    }
    return true;
}

nlohmann::json Inventory::save() const {
    nlohmann::json items = nlohmann::json::array();
    for (const InventoryItem& it : m_items) {
        nlohmann::json j{ { "id", it.id }, { "count", it.count } };
        if (it.slot >= 0) j["slot"] = kSlotKeys[it.slot];
        else {
            j["x"] = it.x;
            j["y"] = it.y;
            if (it.rotated) j["rotated"] = true;
        }
        items.push_back(std::move(j));
    }
    return { { "columns", m_cols }, { "rows", m_rows }, { "items", std::move(items) } };
}

int Inventory::load(const ItemCatalog& catalog, const nlohmann::json& data) {
    m_items.clear();
    int dropped = 0;
    if (!data.is_object() || !data.contains("items") || !data["items"].is_array()) return 0;
    for (const nlohmann::json& j : data["items"]) {
        const std::string id = j.is_object() ? j.value("id", "") : "";
        const ItemDef* def = catalog.find(id);
        if (!def) {
            ++dropped;
            continue;
        }
        InventoryItem it;
        it.uid = m_nextUid++;
        it.id = id;
        it.count = std::clamp(j.value("count", 1), 1, def->maxStack);
        if (j.contains("slot") && j["slot"].is_string()) {
            const int slot = slotFromKey(j["slot"].get<std::string>());
            if (slot < 0 || !(def->equipSlots & (1u << static_cast<uint32_t>(slot))) || equipped(static_cast<EquipSlot>(slot))) {
                ++dropped;
                continue;
            }
            it.slot = slot;
            it.x = it.y = -1;
            m_items.push_back(std::move(it));
            continue;
        }
        it.rotated = j.value("rotated", false);
        it.x = j.value("x", 0);
        it.y = j.value("y", 0);
        if (!fits(*def, it.x, it.y, it.rotated)) {
            ++dropped;
            continue;
        }
        it.w = it.rotated ? def->height : def->width;
        it.h = it.rotated ? def->width : def->height;
        m_items.push_back(std::move(it));
    }
    return dropped;
}

float loadSpeedFactor(float weight, float maxWeight) {
    if (maxWeight <= 0.0f) return 1.0f;
    const float share = weight / maxWeight;
    if (share <= 0.5f) return 1.0f;
    if (share <= 1.0f) return 1.0f - (share - 0.5f) * 0.8f; // 1 at half, 0.6 at the limit
    return 0.35f;
}

} // namespace kke
