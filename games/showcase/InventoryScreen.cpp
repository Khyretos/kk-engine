// kke_demo's bag (Kees, 2026-09-28: "a grid like escape from tarkof or
// resident evil where you see a description of each item weight and
// etc."): Tab, I or View opens it over the game, which keeps
// running (View on a controller: the pause menu is Start). On the left what you wear and hold, in the middle the grid
// where every item takes its cells, on the right the details of the one
// under the cursor, below the weight you carry.
//
// The arrows, WASD, the d-pad, the left stick or the mouse move the
// cursor; A, Space, Enter or a click takes the item there and places it
// again (on a cell, or on an equipment slot to equip it); R or Y turns
// what you're moving; E, X or a right-click equips or takes off; Q or RB
// drops it in front of you; T or the right stick sorts the bag; B, View,
// Tab, Backspace or Esc closes it (or first puts back what you're moving).

#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Event.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kke_showcase {

namespace {

constexpr int kCell = 46; // dp per cell, the 1 dp gap included
// The equipment column, top to bottom.
constexpr kke::EquipSlot kSlotOrder[] = { kke::EquipSlot::Head,      kke::EquipSlot::Back,   kke::EquipSlot::LeftHand,
                                          kke::EquipSlot::RightHand, kke::EquipSlot::HipLeft, kke::EquipSlot::HipRight };
constexpr int kSlotRows = static_cast<int>(std::size(kSlotOrder));
const char* const kSlotTitles[] = { "Head", "Back", "Left hand", "Right hand", "Left hip", "Right hip" };

std::string hexColor(const glm::vec3& c) {
    char buf[16];
    const auto u = [](float f) { return static_cast<unsigned>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", u(c.r), u(c.g), u(c.b));
    return buf;
}

std::string kg(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), v < 0.1f ? "%.3f kg" : "%.1f kg", static_cast<double>(v));
    return buf;
}

} // namespace

void ShowcaseModule::buildInventoryScreen() {
    kke::InputMap& in = m_input->map(0);
    using IM = kke::InputModule;
    in.defineAction({ "inv.open", "Open the bag", "Showcase", "invclosed" });
    in.addBinding(IM::bind("inv.open", IM::key(SDL_SCANCODE_TAB)));
    in.addBinding(IM::bind("inv.open", IM::key(SDL_SCANCODE_I)));
    in.addBinding(IM::bind("inv.open", IM::pad(SDL_GAMEPAD_BUTTON_BACK))); // View: the pause menu is Start (Hud.cpp)
    struct A { const char* id; const char* label; };
    for (A a : { A{ "inv.close", "Bag: close" }, A{ "inv.up", "Bag: up" }, A{ "inv.down", "Bag: down" }, A{ "inv.left", "Bag: left" },
                 A{ "inv.right", "Bag: right" }, A{ "inv.pick", "Bag: take / place" }, A{ "inv.rotate", "Bag: turn" },
                 A{ "inv.equip", "Bag: equip / take off" }, A{ "inv.drop", "Bag: drop" }, A{ "inv.sort", "Bag: sort" } })
        in.defineAction({ a.id, a.label, "Showcase", "invlist" });
    for (SDL_Scancode k : { SDL_SCANCODE_TAB, SDL_SCANCODE_I, SDL_SCANCODE_BACKSPACE }) in.addBinding(IM::bind("inv.close", IM::key(k)));
    in.addBinding(IM::bind("inv.close", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
    in.addBinding(IM::bind("inv.close", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));
    struct Dir { const char* id; SDL_Scancode a, b; SDL_GamepadButton pad; SDL_GamepadAxis axis; int sign; };
    for (Dir d : { Dir{ "inv.up", SDL_SCANCODE_W, SDL_SCANCODE_UP, SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_AXIS_LEFTY, -1 },
                   Dir{ "inv.down", SDL_SCANCODE_S, SDL_SCANCODE_DOWN, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_AXIS_LEFTY, 1 },
                   Dir{ "inv.left", SDL_SCANCODE_A, SDL_SCANCODE_LEFT, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_AXIS_LEFTX, -1 },
                   Dir{ "inv.right", SDL_SCANCODE_D, SDL_SCANCODE_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_AXIS_LEFTX, 1 } }) {
        in.addBinding(IM::bind(d.id, IM::key(d.a)));
        in.addBinding(IM::bind(d.id, IM::key(d.b)));
        in.addBinding(IM::bind(d.id, IM::pad(d.pad)));
        in.addBinding(IM::bind(d.id, IM::padAxis(d.axis, d.sign)));
    }
    for (SDL_Scancode k : { SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER }) in.addBinding(IM::bind("inv.pick", IM::key(k)));
    in.addBinding(IM::bind("inv.pick", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    in.addBinding(IM::bind("inv.rotate", IM::key(SDL_SCANCODE_R)));
    in.addBinding(IM::bind("inv.rotate", IM::pad(SDL_GAMEPAD_BUTTON_NORTH)));
    in.addBinding(IM::bind("inv.equip", IM::key(SDL_SCANCODE_E)));
    in.addBinding(IM::bind("inv.equip", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
    in.addBinding(IM::bind("inv.drop", IM::key(SDL_SCANCODE_Q)));
    in.addBinding(IM::bind("inv.drop", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
    in.addBinding(IM::bind("inv.sort", IM::key(SDL_SCANCODE_T)));
    in.addBinding(IM::bind("inv.sort", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK)));
    in.setContextEnabled("invlist", false);

    if (!m_ui || !m_ui->context()) return;
    Rml::Context* ctx = m_ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("inv");
    if (!c) return;
    if (auto s = c.RegisterStruct<InvCell>()) {
        s.RegisterMember("cursor", &InvCell::cursor);
        s.RegisterMember("ok", &InvCell::ok);
        s.RegisterMember("bad", &InvCell::bad);
    }
    c.RegisterArray<std::vector<InvCell>>();
    if (auto s = c.RegisterStruct<InvTile>()) {
        s.RegisterMember("style", &InvTile::style);
        s.RegisterMember("label", &InvTile::label);
        s.RegisterMember("count", &InvTile::count);
        s.RegisterMember("moving", &InvTile::moving);
    }
    c.RegisterArray<std::vector<InvTile>>();
    if (auto s = c.RegisterStruct<InvSlot>()) {
        s.RegisterMember("name", &InvSlot::name);
        s.RegisterMember("item", &InvSlot::item);
        s.RegisterMember("style", &InvSlot::style);
        s.RegisterMember("cursor", &InvSlot::cursor);
        s.RegisterMember("fits", &InvSlot::fits);
    }
    c.RegisterArray<std::vector<InvSlot>>();
    c.Bind("open", &m_invOpen);
    c.Bind("cells", &m_invCells);
    c.Bind("tiles", &m_invTiles);
    c.Bind("slots", &m_invSlots);
    c.Bind("info_any", &m_invInfo.any);
    c.Bind("info_name", &m_invInfo.name);
    c.Bind("info_category", &m_invInfo.category);
    c.Bind("info_desc", &m_invInfo.description);
    c.Bind("info_weight", &m_invInfo.weight);
    c.Bind("info_size", &m_invInfo.size);
    c.Bind("info_equip", &m_invInfo.equip);
    c.Bind("weight", &m_invWeight);
    c.Bind("weight_bar", &m_invWeightBar);
    c.Bind("heavy", &m_invHeavy);
    c.Bind("note", &m_invNote);
    c.Bind("keys", &m_invKeys);
    auto cellOf = [this](const Rml::VariantList& args, int& x, int& y) {
        if (args.empty()) return false;
        const int i = args[0].Get<int>();
        x = i % m_inv.columns();
        y = i / m_inv.columns();
        return true;
    };
    c.BindEventCallback("cellhover", [this, cellOf](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        int x = 0, y = 0;
        if (!cellOf(args, x, y) || (x == m_invX && y == m_invY)) return;
        m_invX = x;
        m_invY = y;
        m_invDirty = true;
    });
    c.BindEventCallback("celldown", [this, cellOf](Rml::DataModelHandle, Rml::Event& e, const Rml::VariantList& args) {
        int x = 0, y = 0;
        if (!cellOf(args, x, y)) return;
        m_invX = x;
        m_invY = y;
        if (e.GetParameter<int>("button", 0) == 1) invEquipToggle();
        else invPress();
    });
    c.BindEventCallback("slothover", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty() || (m_invX == -1 && m_invY == args[0].Get<int>())) return;
        m_invX = -1;
        m_invY = args[0].Get<int>();
        m_invDirty = true;
    });
    c.BindEventCallback("slotdown", [this](Rml::DataModelHandle, Rml::Event& e, const Rml::VariantList& args) {
        if (args.empty()) return;
        m_invX = -1;
        m_invY = args[0].Get<int>();
        if (e.GetParameter<int>("button", 0) == 1) invEquipToggle();
        else invPress();
    });
    c.BindEventCallback("sort", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) {
        m_invNote = m_inv.sort(m_items) ? "Sorted" : "Not enough room to sort";
        m_invDirty = true;
    });
    c.BindEventCallback("close", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { openInventory(false); });
    m_invModel = c.GetModelHandle();
    const char* base = SDL_GetBasePath();
    const std::string root = std::string(base ? base : "") + "ui/";
    m_invDoc = ctx->LoadDocument(root + "showcase_inventory.rml");
    if (!m_invDoc) {
        kke::log::get(name())->warn("bag: could not load {}showcase_inventory.rml", root);
        return;
    }
    m_invDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void ShowcaseModule::openInventory(bool open) {
    if (open == m_invOpen) return;
    if (open) openSpawnMenu(false);
    m_invOpen = open;
    kke::InputMap& in = m_input->map(0);
    in.setContextEnabled("invlist", open);
    in.setContextEnabled("invclosed", !open);
    if (open) {
        m_invRecapture = m_captured;
        if (m_captured) setCaptured(false);
        m_invNote.clear();
        if (m_invX >= m_inv.columns() || m_invY >= m_inv.rows()) m_invX = m_invY = 0;
    } else {
        m_invMoving = 0; // whatever was being moved stays where it was
        if (m_invRecapture) {
            setCaptured(true);
            m_swallowFire = true;
        }
    }
    m_invHeldDir = glm::ivec2(0);
    m_invRepeat = 0.0f;
    m_invDirty = true;
    if (m_invModel) m_invModel.DirtyVariable("open");
}

const kke::InventoryItem* ShowcaseModule::invUnderCursor() const {
    if (m_invX < 0) return m_invY >= 0 && m_invY < kSlotRows ? m_inv.equipped(kSlotOrder[m_invY]) : nullptr;
    return m_inv.at(m_invX, m_invY);
}

void ShowcaseModule::invPress() {
    m_invDirty = true;
    if (!m_invMoving) {
        if (const kke::InventoryItem* it = invUnderCursor()) {
            m_invMoving = it->uid;
            m_invMovingRotated = it->rotated;
            m_invNote.clear();
            // Grab it by its corner: the cursor goes there, so the ghost starts where the item is.
            if (m_invX >= 0) {
                m_invX = it->x;
                m_invY = it->y;
            }
        }
        return;
    }
    const kke::InventoryItem* it = m_inv.find(m_invMoving);
    const kke::ItemDef* def = it ? m_items.find(it->id) : nullptr;
    if (!def) {
        m_invMoving = 0;
        return;
    }
    bool done = false;
    if (m_invX < 0) {
        const kke::EquipSlot slot = kSlotOrder[std::clamp(m_invY, 0, kSlotRows - 1)];
        if (!(def->equipSlots & kke::slotBit(slot))) m_invNote = def->name + " doesn't go there";
        else if (!(done = m_inv.equip(m_items, m_invMoving, slot))) m_invNote = "No room in the bag for what's there";
    } else {
        done = m_inv.move(m_items, m_invMoving, m_invX, m_invY, m_invMovingRotated);
        if (!done) m_invNote = "It doesn't fit there";
    }
    kke::log::get(name())->info("bag: {} {} to {}", done ? "placed" : "could not place", def->id,
                                m_invX < 0 ? std::string(kSlotTitles[std::clamp(m_invY, 0, kSlotRows - 1)]) : fmt::format("{},{}{}", m_invX, m_invY, m_invMovingRotated ? " turned" : ""));
    if (done) {
        m_invMoving = 0;
        m_invNote.clear();
        syncEquipment();
    }
}

void ShowcaseModule::invEquipToggle() {
    m_invDirty = true;
    const kke::InventoryItem* it = m_invMoving ? m_inv.find(m_invMoving) : invUnderCursor();
    const kke::ItemDef* def = it ? m_items.find(it->id) : nullptr;
    if (!def) return;
    const uint32_t uid = it->uid;
    if (it->slot >= 0) {
        if (!m_inv.unequip(m_items, static_cast<kke::EquipSlot>(it->slot))) m_invNote = "No room in the bag to take it off";
    } else if (def->equipSlots == 0) {
        m_invNote = def->name + " can't be equipped";
    } else {
        // The first slot it fits that's empty, else the first it fits (a swap).
        int pick = -1;
        for (size_t s = 0; s < kke::kEquipSlots && pick < 0; ++s)
            if ((def->equipSlots & kke::slotBit(static_cast<kke::EquipSlot>(s))) && !m_inv.equipped(static_cast<kke::EquipSlot>(s))) pick = static_cast<int>(s);
        for (size_t s = 0; s < kke::kEquipSlots && pick < 0; ++s)
            if (def->equipSlots & kke::slotBit(static_cast<kke::EquipSlot>(s))) pick = static_cast<int>(s);
        if (!m_inv.equip(m_items, uid, static_cast<kke::EquipSlot>(pick))) m_invNote = "No room in the bag for what's there";
        else m_invNote = def->name + ": " + kke::equipSlotName(static_cast<kke::EquipSlot>(pick));
    }
    m_invMoving = 0;
    syncEquipment();
}

void ShowcaseModule::invDrop() {
    m_invDirty = true;
    const kke::InventoryItem* it = m_invMoving ? m_inv.find(m_invMoving) : invUnderCursor();
    if (!it) return;
    const std::string id = it->id;
    const int count = it->count;
    m_inv.remove(it->uid);
    m_invMoving = 0;
    dropItem(id, count, spawnSpot(0.9f) + glm::vec3(0.0f, 0.3f, 0.0f));
    kke::log::get(name())->info("bag: dropped {} x{}", id, count);
    if (const kke::ItemDef* def = m_items.find(id)) m_invNote = "Dropped " + (count > 1 ? std::to_string(count) + " x " : std::string()) + def->name;
    syncEquipment();
}

void ShowcaseModule::updateInventory(float dt) {
    kke::InputMap& in = m_input->map(0);
    if (m_menuOpen) {
        openInventory(false);
        in.setContextEnabled("invclosed", false);
        return;
    }
    if (!m_invOpen) {
        in.setContextEnabled("invclosed", !m_spawnOpen);
        if (in.pressed("inv.open")) openInventory(true);
        return; // opened this frame: the key that opened it isn't also a close
    }
    if (in.pressed("inv.close")) {
        if (m_invMoving) { // put back first
            m_invMoving = 0;
            m_invDirty = true;
        } else {
            openInventory(false);
            return;
        }
    }
    // The cursor: one step on press, then repeating while held.
    const glm::ivec2 dir(in.held("inv.right") ? 1 : in.held("inv.left") ? -1 : 0, in.held("inv.down") ? 1 : in.held("inv.up") ? -1 : 0);
    glm::ivec2 step(0);
    if (dir != m_invHeldDir) {
        m_invHeldDir = dir;
        m_invRepeat = 0.35f;
        step = dir;
    } else if (dir != glm::ivec2(0) && (m_invRepeat -= dt) <= 0.0f) {
        m_invRepeat = 0.1f;
        step = dir;
    }
    if (step != glm::ivec2(0)) {
        m_invX = std::clamp(m_invX + step.x, -1, m_inv.columns() - 1);
        m_invY = std::clamp(m_invY + step.y, 0, (m_invX < 0 ? kSlotRows : m_inv.rows()) - 1);
        m_invDirty = true;
    }
    if (in.pressed("inv.pick")) invPress();
    if (in.pressed("inv.rotate") && m_invMoving) {
        m_invMovingRotated = !m_invMovingRotated;
        m_invDirty = true;
    }
    if (in.pressed("inv.equip")) invEquipToggle();
    if (in.pressed("inv.drop")) invDrop();
    if (in.pressed("inv.sort")) {
        m_invNote = m_inv.sort(m_items) ? "Sorted" : "Not enough room to sort";
        m_invDirty = true;
    }
    if (m_invDirty) refreshInventory();
}

// Everything the screen shows, from m_inv and the cursor.
void ShowcaseModule::refreshInventory() {
    m_invDirty = false;
    if (!m_invModel) return;
    const int cols = m_inv.columns(), rows = m_inv.rows();
    const kke::InventoryItem* moving = m_invMoving ? m_inv.find(m_invMoving) : nullptr;
    const kke::ItemDef* movingDef = moving ? m_items.find(moving->id) : nullptr;
    if (!movingDef) m_invMoving = 0;
    // Cells: the cursor, or where what's being moved would land.
    m_invCells.assign(static_cast<size_t>(cols * rows), InvCell{});
    if (movingDef && m_invX >= 0) {
        const int w = m_invMovingRotated ? movingDef->height : movingDef->width, h = m_invMovingRotated ? movingDef->width : movingDef->height;
        const kke::InventoryItem* there = m_inv.at(m_invX, m_invY);
        const bool merge = there && there->uid != moving->uid && there->id == moving->id && movingDef->maxStack > 1;
        const bool fits = merge || m_inv.fits(*movingDef, m_invX, m_invY, m_invMovingRotated, moving->uid);
        for (int y = m_invY; y < std::min(rows, m_invY + h); ++y)
            for (int x = m_invX; x < std::min(cols, m_invX + w); ++x) (fits ? m_invCells[static_cast<size_t>(y * cols + x)].ok : m_invCells[static_cast<size_t>(y * cols + x)].bad) = true;
    } else if (m_invX >= 0) {
        // The whole item under the cursor lights up, or the one cell.
        if (const kke::InventoryItem* it = m_inv.at(m_invX, m_invY)) {
            for (int y = it->y; y < it->y + it->h; ++y)
                for (int x = it->x; x < it->x + it->w; ++x) m_invCells[static_cast<size_t>(y * cols + x)].cursor = true;
        } else {
            m_invCells[static_cast<size_t>(m_invY * cols + m_invX)].cursor = true;
        }
    }
    // Tiles: one per item in the grid.
    m_invTiles.clear();
    for (const kke::InventoryItem& it : m_inv.items()) {
        if (it.slot >= 0) continue;
        const kke::ItemDef* def = m_items.find(it.id);
        if (!def) continue;
        char style[160];
        std::snprintf(style, sizeof(style), "left: %ddp; top: %ddp; width: %ddp; height: %ddp; background-color: %s;", it.x * kCell + 1, it.y * kCell + 1,
                      it.w * kCell - 2, it.h * kCell - 2, hexColor(def->color * 0.75f).c_str());
        InvTile t;
        t.style = style;
        t.label = def->name;
        t.count = it.count > 1 ? std::to_string(it.count) : std::string();
        t.moving = it.uid == m_invMoving;
        m_invTiles.push_back(std::move(t));
    }
    // The equipment column.
    m_invSlots.clear();
    for (int k = 0; k < kSlotRows; ++k) {
        InvSlot s;
        s.name = kSlotTitles[k];
        const kke::InventoryItem* it = m_inv.equipped(kSlotOrder[k]);
        const kke::ItemDef* def = it ? m_items.find(it->id) : nullptr;
        s.item = def ? def->name : std::string();
        s.style = def ? "background-color: " + hexColor(def->color * 0.75f) + ";" : std::string();
        s.cursor = m_invX < 0 && m_invY == k;
        s.fits = movingDef && (movingDef->equipSlots & kke::slotBit(kSlotOrder[k]));
        m_invSlots.push_back(std::move(s));
    }
    // Details of what's being moved, else what's under the cursor.
    const kke::InventoryItem* shown = moving ? moving : invUnderCursor();
    const kke::ItemDef* def = shown ? m_items.find(shown->id) : nullptr;
    m_invInfo = InvDetails{};
    if (def) {
        m_invInfo.any = true;
        m_invInfo.name = shown->count > 1 ? def->name + " (" + std::to_string(shown->count) + ")" : def->name;
        m_invInfo.category = def->category;
        m_invInfo.description = def->description;
        m_invInfo.weight = shown->count > 1 ? kg(def->weight * static_cast<float>(shown->count)) + " (" + kg(def->weight) + " each)" : kg(def->weight);
        m_invInfo.size = std::to_string(def->width) + " x " + std::to_string(def->height) + " cells" +
                         (def->maxStack > 1 ? ", " + std::to_string(def->maxStack) + " to a cell" : std::string());
        std::string slots;
        for (size_t s = 0; s < kke::kEquipSlots; ++s)
            if (def->equipSlots & kke::slotBit(static_cast<kke::EquipSlot>(s))) slots += (slots.empty() ? "" : ", ") + std::string(kke::equipSlotName(static_cast<kke::EquipSlot>(s)));
        if (shown->slot >= 0) m_invInfo.equip = std::string("Equipped: ") + kke::equipSlotName(static_cast<kke::EquipSlot>(shown->slot));
        else if (!slots.empty()) m_invInfo.equip = "Equips to: " + slots;
    }
    // Weight.
    const float w = m_inv.weight(m_items), max = m_inv.maxWeight();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.1f / %.0f kg", static_cast<double>(w), static_cast<double>(max));
    m_invWeight = buf;
    std::snprintf(buf, sizeof(buf), "width: %.0f%%;", static_cast<double>(std::min(100.0f, w / std::max(max, 0.1f) * 100.0f)));
    m_invWeightBar = buf;
    const float f = loadFactor();
    m_invHeavy = f < 1.0f;
    if (m_invNote.empty() && f < 1.0f) m_invNote = f < 0.5f ? "Overloaded: you can barely move" : "Heavy: you move slower";
    // The keys for the device in use.
    const kke::InputModule* input = m_app->getModule<kke::InputModule>();
    const std::string keys = moving ? "{inv.pick} place   {inv.rotate} turn   {inv.equip} equip   {inv.drop} drop   {inv.close} put back"
                                    : "{inv.pick} take   {inv.equip} equip or take off   {inv.drop} drop   {inv.sort} sort   {inv.close} close";
    m_invKeys = input ? input->promptText(keys) : keys;
    m_invModel.DirtyAllVariables();
}

} // namespace kke_showcase
