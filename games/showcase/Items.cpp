// Items in kke_demo (Kees, 2026-09-28: "an inventory something akin to a
// grid like escape from tarkof or resident evil where you see a
// description of each item weight and etc.; equipment to pickup and
// equip"). Things lie about the world (the supply table by the start for
// now); pickup (X, F) puts the one in front of you in your bag, a
// kke::Inventory grid of 10 x 6 cells (InventoryScreen.cpp shows it).
// What you equip there is drawn on the character with kke::Equipment: an
// axe or a rifle in the right hand or across the back, a pistol or a
// canteen on a hip, a lantern in the left hand, a helmet on the head;
// fingers close round what a hand holds. A heavy bag slows you down
// (kke::loadSpeedFactor).
//
// The kinds of item are data (data/items.json: name, description, size in
// cells, weight, stack, slots); what each looks like is made here from
// boxes, cylinders and spheres, with its grip at the origin.

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/DataFile.h"
#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace kke_showcase {

using namespace layout;

namespace {

constexpr float kItemReach = 1.7f; // m, flat, from the chest to the item
constexpr size_t kMaxWorldItems = 200;

glm::mat4 at(float x, float y, float z) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, y, z)); }
glm::mat4 turn(float degrees, const glm::vec3& axis) { return glm::rotate(glm::mat4(1.0f), glm::radians(degrees), axis); }
// A cylinder lying along X (appendCylinder stands on Y).
glm::mat4 alongX(const glm::mat4& m) { return m * turn(-90.0f, glm::vec3(0, 0, 1)); }

const glm::vec3 kSteel(0.62f, 0.64f, 0.68f), kGunMetal(0.18f, 0.19f, 0.21f), kWood(0.5f, 0.33f, 0.2f), kDarkWood(0.36f, 0.24f, 0.15f);

} // namespace

void ShowcaseModule::loadItems() {
    const char* base = SDL_GetBasePath();
    const std::string path = std::string(base ? base : "") + "data/items.json";
    nlohmann::json data;
    std::string error;
    if (!kke::datafile::loadFile(path, data, &error) || !m_items.load(data, &error))
        kke::log::get(name())->warn("items: {} ({})", error, path);
    m_itemBatch = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_equipBatch = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    buildLooks();
    placeItems();
}

// Each kind's mesh in item space: the main grip at the origin, +Y along
// the handle toward the working end (an axe's head), +Z the way the palm
// faces through the handle; a gun's barrel points along +X. Worn frames
// (Equippable::worn) place it in the socket's frame: the back's has Y
// up and Z out of the back, a hip's Y up and Z out to the side, the
// head's Y forward and Z up.
void ShowcaseModule::buildLooks() {
    for (const kke::ItemDef& def : m_items.all()) {
        ItemLook& l = m_looks[def.id];
        l = ItemLook{};
        std::vector<kke::Vertex>& v = l.v;
        std::vector<uint32_t>& i = l.idx;
        kke::ItemGrip grip;
        grip.name = "main";
        bool gun = false; // long along X (a barrel) rather than along Y (a handle)
        const std::string& id = def.id;
        if (id == "axe") {
            appendCylinder(at(0.0f, 0.24f, 0.0f), 0.017f, 0.36f, kWood, v, i);
            appendBox(at(0.045f, 0.56f, 0.0f), glm::vec3(0.07f, 0.045f, 0.014f), kSteel, v, i);
            appendBox(at(0.12f, 0.56f, 0.0f), glm::vec3(0.012f, 0.062f, 0.007f), glm::vec3(0.85f, 0.87f, 0.9f), v, i);
            appendBox(at(-0.04f, 0.56f, 0.0f), glm::vec3(0.022f, 0.03f, 0.018f), kSteel * 0.8f, v, i);
            grip.radius = 0.017f;
            l.metallic = 0.3f;
        } else if (id == "rifle") {
            gun = true;
            appendBox(turn(-12.0f, glm::vec3(0, 0, 1)), glm::vec3(0.016f, 0.055f, 0.014f), kDarkWood, v, i);
            appendBox(at(0.08f, 0.07f, 0.0f), glm::vec3(0.15f, 0.03f, 0.018f), kGunMetal, v, i);
            appendBox(at(-0.26f, 0.04f, 0.0f) * turn(-6.0f, glm::vec3(0, 0, 1)), glm::vec3(0.19f, 0.045f, 0.018f), kWood, v, i);
            appendBox(at(0.36f, 0.058f, 0.0f), glm::vec3(0.13f, 0.022f, 0.02f), kWood, v, i);
            appendCylinder(alongX(at(0.56f, 0.085f, 0.0f)), 0.011f, 0.24f, kGunMetal, v, i);
            appendCylinder(alongX(at(0.08f, 0.125f, 0.0f)), 0.018f, 0.12f, kGunMetal * 0.7f, v, i);
            grip.radius = 0.016f;
            l.metallic = 0.4f;
        } else if (id == "pistol") {
            gun = true;
            appendBox(turn(-12.0f, glm::vec3(0, 0, 1)), glm::vec3(0.014f, 0.05f, 0.013f), kGunMetal * 0.8f, v, i);
            appendBox(at(0.05f, 0.062f, 0.0f), glm::vec3(0.09f, 0.022f, 0.013f), kGunMetal, v, i);
            appendBox(at(0.035f, 0.02f, 0.0f), glm::vec3(0.025f, 0.004f, 0.006f), kGunMetal, v, i);
            grip.radius = 0.014f;
            l.metallic = 0.6f;
        } else if (id == "medkit") {
            appendBox(glm::mat4(1.0f), glm::vec3(0.11f, 0.08f, 0.05f), def.color, v, i);
            for (int s = -1; s <= 1; s += 2) {
                appendBox(at(0.0f, 0.0f, 0.051f * static_cast<float>(s)), glm::vec3(0.045f, 0.012f, 0.002f), glm::vec3(0.95f), v, i);
                appendBox(at(0.0f, 0.0f, 0.051f * static_cast<float>(s)), glm::vec3(0.012f, 0.045f, 0.002f), glm::vec3(0.95f), v, i);
            }
        } else if (id == "canteen") {
            appendCylinder(glm::mat4(1.0f), 0.07f, 0.11f, def.color, v, i);
            appendCylinder(at(0.0f, 0.125f, 0.0f), 0.02f, 0.018f, glm::vec3(0.12f), v, i);
            grip.radius = 0.07f;
        } else if (id == "helmet") {
            // The crown of the shell 3 cm over the item's origin (the top of the head).
            appendSphere(at(0.0f, -0.08f, 0.0f) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.8f, 1.12f)), 0.14f, def.color, v, i);
            appendBox(at(0.0f, -0.1f, -0.16f), glm::vec3(0.11f, 0.008f, 0.035f), def.color * 0.8f, v, i); // the brim, in front (-Z)
            l.metallic = 0.3f;
        } else if (id == "lantern") {
            // Hangs from its handle: the handle at the origin.
            appendBox(at(0.0f, -0.25f, 0.0f), glm::vec3(0.06f, 0.012f, 0.06f), glm::vec3(0.15f), v, i);
            appendBox(at(0.0f, -0.09f, 0.0f), glm::vec3(0.05f, 0.012f, 0.05f), glm::vec3(0.15f), v, i);
            appendSphere(at(0.0f, -0.17f, 0.0f), 0.05f, glm::vec3(1.0f, 0.88f, 0.45f), v, i);
            for (int k = 0; k < 4; ++k) {
                const float x = (k & 1) ? 0.05f : -0.05f, z = (k & 2) ? 0.05f : -0.05f;
                appendBox(at(x, -0.17f, z), glm::vec3(0.005f, 0.075f, 0.005f), glm::vec3(0.15f), v, i);
            }
            appendBox(at(0.0f, -0.04f, 0.0f), glm::vec3(0.004f, 0.04f, 0.035f), glm::vec3(0.2f), v, i);
            grip.radius = 0.006f;
            l.metallic = 0.5f;
        } else if (id == "firewood") {
            appendCylinder(alongX(at(0.0f, 0.0f, 0.055f)), 0.06f, 0.2f, kWood, v, i);
            appendCylinder(alongX(at(0.02f, 0.0f, -0.055f)), 0.055f, 0.19f, kDarkWood, v, i);
            appendCylinder(alongX(at(-0.01f, 0.09f, 0.0f)), 0.05f, 0.2f, kWood * 1.1f, v, i);
        } else if (id == "apple") {
            appendSphere(glm::mat4(1.0f), 0.04f, def.color, v, i);
            appendCylinder(at(0.0f, 0.045f, 0.0f), 0.004f, 0.012f, kDarkWood, v, i);
        } else if (id.rfind("flower_", 0) == 0) {
            const glm::vec3 green(0.25f, 0.55f, 0.2f);
            appendCylinder(at(0.0f, 0.1f, 0.0f), 0.004f, 0.1f, green, v, i);
            appendBox(at(0.022f, 0.07f, 0.0f) * turn(20.0f, glm::vec3(0, 0, 1)), glm::vec3(0.022f, 0.003f, 0.009f), green, v, i);
            appendSphere(at(0.0f, 0.2f, 0.0f) * glm::scale(glm::mat4(1.0f), glm::vec3(1.0f, 0.55f, 1.0f)), 0.03f, def.color, v, i);
            appendSphere(at(0.0f, 0.212f, 0.0f), 0.01f, glm::vec3(0.2f, 0.15f, 0.05f), v, i);
            grip.radius = 0.004f;
        } else if (id.rfind("ammo_", 0) == 0) {
            appendBox(glm::mat4(1.0f), glm::vec3(0.045f, 0.025f, 0.03f), glm::vec3(0.25f, 0.3f, 0.22f), v, i);
            for (int k = 0; k < 4; ++k)
                appendCylinder(at(-0.03f + 0.02f * static_cast<float>(k), 0.035f, 0.0f), 0.005f, 0.012f, def.color, v, i);
            l.metallic = 0.3f;
        } else {
            // Unknown to this file: a box of its colour, a hand span per cell.
            appendBox(glm::mat4(1.0f), glm::vec3(0.05f * static_cast<float>(def.width), 0.05f * static_cast<float>(def.height), 0.03f), def.color, v, i);
        }
        l.lo = glm::vec3(1e9f);
        l.hi = glm::vec3(-1e9f);
        for (const kke::Vertex& p : v) {
            l.lo = glm::min(l.lo, p.position);
            l.hi = glm::max(l.hi, p.position);
        }
        // Equipping: what Equipment needs (the slots, the grip, the worn frames).
        kke::Equippable& e = l.equip;
        e.name = def.name;
        e.slots = def.equipSlots;
        e.grips = { grip };
        const glm::vec3 centre = (l.lo + l.hi) * 0.5f;
        // Long things across the back, the working end up and to the right;
        // the thickness out of the back.
        const glm::mat4 toY = gun ? turn(90.0f, glm::vec3(0, 0, 1)) : glm::mat4(1.0f); // the long axis up
        const float depth = gun ? (l.hi.z - l.lo.z) * 0.5f : (l.hi.x - l.lo.x) * 0.5f;
        e.worn[static_cast<size_t>(kke::EquipSlot::Back)] =
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 0.03f + std::min(depth, 0.06f))) * turn(-35.0f, glm::vec3(0, 0, 1)) * toY *
            glm::translate(glm::mat4(1.0f), -centre);
        // On a hip: guns muzzle down, the rest upright, out from the side.
        const glm::mat4 hip = gun ? turn(-90.0f, glm::vec3(0, 0, 1)) : glm::mat4(1.0f);
        const float out = gun ? (l.hi.z - l.lo.z) * 0.5f : (l.hi.x - l.lo.x) * 0.5f;
        for (kke::EquipSlot s : { kke::EquipSlot::HipLeft, kke::EquipSlot::HipRight })
            e.worn[static_cast<size_t>(s)] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.08f, out + 0.02f)) * hip * glm::translate(glm::mat4(1.0f), -centre);
        // On the head: item Y (up) along the socket's Z (up), item -Z (the front) along its Y (forward).
        e.worn[static_cast<size_t>(kke::EquipSlot::Head)] = turn(90.0f, glm::vec3(1, 0, 0));
    }
}

void ShowcaseModule::clearWorldItems() {
    kke::RigidWorld& w = m_rigid->world();
    for (const WorldItem& it : m_worldItems) w.remove(it.body);
    m_worldItems.clear();
}

void ShowcaseModule::dropItem(const std::string& id, int count, const glm::vec3& p) {
    const ItemLook* look = nullptr;
    if (auto f = m_looks.find(id); f != m_looks.end()) look = &f->second;
    const kke::ItemDef* def = m_items.find(id);
    if (!look || !def || count <= 0) return;
    if (m_worldItems.size() >= kMaxWorldItems) { // the oldest goes
        m_rigid->world().remove(m_worldItems.front().body);
        m_worldItems.erase(m_worldItems.begin());
    }
    // A box round its mesh (Jolt boxes want some thickness).
    kke::RigidWorld::BodyDesc d;
    d.halfExtents = glm::max((look->hi - look->lo) * 0.5f, glm::vec3(0.025f));
    d.rotation = glm::angleAxis(glm::radians(static_cast<float>(m_worldItems.size() % 5) * 37.0f), glm::vec3(0, 1, 0));
    // Long things lie down.
    const glm::vec3 size = look->hi - look->lo;
    if (size.y > std::max(size.x, size.z) * 1.5f) d.rotation = d.rotation * glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
    d.position = p + glm::vec3(0.0f, glm::length(d.halfExtents) + 0.02f, 0.0f);
    d.mass = std::max(0.05f, def->weight * static_cast<float>(count));
    d.friction = 0.8f;
    d.debris = true; // underfoot it's walked through, not stood on (pickup finds it by position)
    d.material = 2; // wood: what a knock sounds like
    const kke::RigidWorld::BodyId body = m_rigid->world().add(d);
    if (body == kke::RigidWorld::kNoBody) return;
    m_worldItems.push_back({ id, count, body });
}

// The supply table by the start: one of everything.
void ShowcaseModule::placeItems() {
    clearWorldItems();
    const float top = kSupplyTableHalf.y * 2.0f;
    struct Spot { const char* id; int count; float x, z; };
    static const Spot kTable[] = {
        { "axe", 1, -1.0f, 0.0f },     { "rifle", 1, -0.4f, -0.15f }, { "pistol", 1, 0.25f, 0.2f }, { "ammo_rifle", 30, 0.55f, -0.2f },
        { "ammo_pistol", 50, 0.75f, 0.15f }, { "medkit", 1, 1.0f, -0.15f }, { "lantern", 1, 0.0f, 0.2f },
    };
    for (const Spot& s : kTable) dropItem(s.id, s.count, kSupply + glm::vec3(s.x, top, s.z));
    // On the ground in front of it.
    static const Spot kGround[] = {
        { "helmet", 1, -0.9f, 1.0f }, { "canteen", 1, -0.2f, 1.1f }, { "firewood", 3, 0.7f, 1.2f }, { "apple", 4, 1.5f, 0.9f },
        { "flower_red", 1, -1.8f, 1.4f }, { "flower_yellow", 1, -1.5f, 1.8f }, { "flower_blue", 1, -2.1f, 1.9f },
    };
    for (const Spot& s : kGround) dropItem(s.id, s.count, kSupply + glm::vec3(s.x, 0.0f, s.z));
}

int ShowcaseModule::itemInReach() const {
    if (m_worldItems.empty() || !m_loco) return -1;
    const kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 chest = w.characterPosition(m_player) + glm::vec3(0, 0.9f, 0);
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    int best = -1;
    float bestScore = 1e9f;
    for (size_t k = 0; k < m_worldItems.size(); ++k) {
        const glm::vec3 p = w.position(m_worldItems[k].body);
        glm::vec3 to = p - chest;
        if (to.y < -1.3f || to.y > 0.8f) continue; // on the floor to over the head
        to.y = 0.0f;
        const float d = glm::length(to);
        if (d > kItemReach) continue;
        const float facing = d > 0.3f ? glm::dot(to / d, f) : 1.0f;
        if (facing < 0.25f) continue;
        const float score = d - facing * 0.6f;
        if (score < bestScore) {
            bestScore = score;
            best = static_cast<int>(k);
        }
    }
    return best;
}

void ShowcaseModule::takeItem(int index) {
    if (index < 0 || static_cast<size_t>(index) >= m_worldItems.size()) return;
    WorldItem& it = m_worldItems[static_cast<size_t>(index)];
    const kke::ItemDef* def = m_items.find(it.id);
    if (!def) return;
    const int left = m_inv.add(m_items, it.id, it.count);
    const int taken = it.count - left;
    const std::string what = taken > 1 ? std::to_string(taken) + " x " + def->name : def->name;
    kke::log::get(name())->info("items: picked up {} {} ({:.1f} kg in the bag)", taken, def->id, m_inv.weight(m_items));
    if (taken == 0) {
        toast("No room in your bag for the " + def->name);
        return;
    }
    if (left > 0) {
        it.count = left;
        toast("Picked up " + what + ": no room for the rest");
    } else {
        m_rigid->world().remove(it.body);
        m_worldItems.erase(m_worldItems.begin() + index);
        toast("Picked up " + what);
    }
    m_invDirty = true;
    syncEquipment();
}

void ShowcaseModule::syncEquipment() {
    if (!m_equipmentBuilt) return;
    for (size_t s = 0; s < kke::kEquipSlots; ++s) {
        const kke::EquipSlot slot = static_cast<kke::EquipSlot>(s);
        const kke::InventoryItem* in = m_inv.equipped(slot);
        const auto look = in ? m_looks.find(in->id) : m_looks.end();
        if (look == m_looks.end() || !m_equipment.equip(slot, look->second.equip, 0)) m_equipment.unequip(slot);
    }
}

float ShowcaseModule::loadFactor() const { return kke::loadSpeedFactor(m_inv.weight(m_items), m_inv.maxWeight()); }

// After the IK: what's equipped, where the posed sockets put it, as one
// mesh in world space; the hands close round what they hold.
void ShowcaseModule::drawEquipped(kke::Pose& pose, const glm::mat4& toWorld) {
    static std::vector<kke::Vertex> v;
    static std::vector<uint32_t> idx;
    v.clear();
    idx.clear();
    if (!m_equipmentBuilt && m_charModel) {
        if (const kke::ModelData* d = m_models->model(m_charModel)) {
            m_equipment = kke::Equipment(*d);
            m_equipmentBuilt = true;
            syncEquipment();
        }
    }
    const bool carrying = m_held.body != kke::RigidWorld::kNoBody; // both hands on a crate: hand items stowed
    const bool shown = m_rig.mode != kke::CameraRig::Mode::FirstPerson;
    if (m_equipmentBuilt && m_equipment.valid() && shown) {
        const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
        for (size_t s = 0; s < kke::kEquipSlots; ++s) {
            const kke::EquipSlot slot = static_cast<kke::EquipSlot>(s);
            const kke::Equippable* item = m_equipment.item(slot);
            if (!item) continue;
            const bool hand = slot == kke::EquipSlot::LeftHand || slot == kke::EquipSlot::RightHand;
            if (hand && carrying) continue;
            const kke::InventoryItem* in = m_inv.equipped(slot);
            const auto look = in ? m_looks.find(in->id) : m_looks.end();
            if (look == m_looks.end()) continue;
            const glm::mat4 m = toWorld * m_equipment.itemTransform(slot, bones);
            const glm::mat3 nm = glm::mat3(m);
            const uint32_t base = static_cast<uint32_t>(v.size());
            for (const kke::Vertex& p : look->second.v) {
                kke::Vertex q = p;
                q.position = glm::vec3(m * glm::vec4(p.position, 1.0f));
                q.normal = glm::normalize(nm * p.normal);
                v.push_back(q);
            }
            for (uint32_t k : look->second.idx) idx.push_back(base + k);
            if (hand) m_equipment.closeHand(m_rigData, pose, slot == kke::EquipSlot::LeftHand ? 0 : 1);
        }
    }
    m_equipBatchIndices = idx.size();
    if (!idx.empty()) m_equipBatch->upload(v, idx);
}

void ShowcaseModule::batchItems() {
    static std::vector<kke::Vertex> v;
    static std::vector<uint32_t> idx;
    v.clear();
    idx.clear();
    const kke::RigidWorld& w = m_rigid->world();
    for (const WorldItem& it : m_worldItems) {
        const auto look = m_looks.find(it.id);
        if (look == m_looks.end()) continue;
        // The body's box sits round the mesh's bounds.
        const glm::mat4 m = w.transform(it.body) * glm::translate(glm::mat4(1.0f), -(look->second.lo + look->second.hi) * 0.5f);
        const glm::mat3 nm = glm::mat3(m);
        const uint32_t base = static_cast<uint32_t>(v.size());
        for (const kke::Vertex& p : look->second.v) {
            kke::Vertex q = p;
            q.position = glm::vec3(m * glm::vec4(p.position, 1.0f));
            q.normal = glm::normalize(nm * p.normal);
            v.push_back(q);
        }
        for (uint32_t k : look->second.idx) idx.push_back(base + k);
    }
    m_itemBatchIndices = idx.size();
    if (!idx.empty()) m_itemBatch->upload(v, idx);
}

// KKE_DEMO_ITEMS=1: at the supply table, pick up everything in reach,
// open the bag, equip the axe, the rifle, the helmet and the canteen,
// close it and turn the camera to see them worn (screenshots, checks).
// KKE_DEMO_ITEMS=2 leaves the bag open (to drive it with keys).
void ShowcaseModule::updateItemsDemo(float dt, glm::vec3& move) {
    const float before = m_demoItems;
    m_demoItems += dt;
    const float t = m_demoItems;
    auto at = [&](float mark) { return before < mark && t >= mark; };
    auto log = kke::log::get(name());
    if (at(0.3f)) {
        m_loco->teleport(kSupply + glm::vec3(-1.4f, 0.05f, 1.0f));
        m_loco->setFacing(glm::vec3(0, 0, -1));
        m_rig.yaw = 0.0f;
    }
    // Step by step along the table, picking up what's in reach.
    if (t > 1.0f && t < 5.0f) {
        if (std::fmod(t, 0.4f) < dt) {
            m_loco->setFacing(glm::vec3(0, 0, -1)); // to the table (walking along turns you)
            togglePickUp();
        }
        move = glm::vec3(0.3f, 0.0f, 0.0f);
    }
    if (at(5.2f)) {
        std::string list;
        for (const kke::InventoryItem& it : m_inv.items()) list += fmt::format("{}{} x{} at {},{}{}", list.empty() ? "" : ", ", it.id, it.count, it.x, it.y, it.rotated ? " turned" : "");
        log->info("items demo: bag {:.1f} kg: {}", m_inv.weight(m_items), list);
        openInventory(true);
    }
    if (at(6.0f)) {
        for (const auto& [id, slot] : { std::pair<const char*, kke::EquipSlot>{ "axe", kke::EquipSlot::RightHand }, { "rifle", kke::EquipSlot::Back },
                                        { "helmet", kke::EquipSlot::Head }, { "canteen", kke::EquipSlot::HipLeft }, { "lantern", kke::EquipSlot::LeftHand } }) {
            uint32_t uid = 0;
            for (const kke::InventoryItem& it : m_inv.items())
                if (it.id == id) uid = it.uid;
            const bool ok = uid && m_inv.equip(m_items, uid, slot);
            log->info("items demo: equip {} to {}: {}", id, kke::equipSlotName(slot), ok ? "yes" : "no");
        }
        syncEquipment();
        // The cursor on the first thing left in the grid: its details show.
        for (const kke::InventoryItem& it : m_inv.items())
            if (it.slot < 0) {
                m_invX = it.x;
                m_invY = it.y;
                break;
            }
        m_invDirty = true;
    }
    if (at(10.0f) && !m_demoItemsKeepOpen) {
        openInventory(false);
        m_rig.yaw = 200.0f; // in front and beside, to see what's worn
    }
    if (at(11.0f)) {
        // Where the right-hand item is against the hand bone.
        const std::vector<glm::mat4> world = m_models->boneWorld(m_charInstance);
        const kke::HandRig& hand = m_equipment.hand(1);
        if (hand.valid() && static_cast<size_t>(hand.hand) < world.size()) {
            const glm::vec3 h = glm::vec3(m_models->transform(m_charInstance) * world[static_cast<size_t>(hand.hand)][3]);
            const glm::vec3 me = m_rigid->world().characterPosition(m_player);
            log->info("items demo: right hand at {:.2f} {:.2f} {:.2f} ({:.2f} m up), {} things worn, {} indices drawn", h.x, h.y, h.z, h.y - me.y,
                      [&] {
                          int n = 0;
                          for (size_t s = 0; s < kke::kEquipSlots; ++s) n += m_equipment.item(static_cast<kke::EquipSlot>(s)) != nullptr;
                          return n;
                      }(),
                      m_equipBatchIndices);
        }
        log->info("items demo: speed factor {:.2f} with {:.1f} kg", loadFactor(), m_inv.weight(m_items));
    }
    if (t > 13.0f && t < 15.0f) move = glm::vec3(0, 0, 1) * 0.6f; // walk off with it all
}

void ShowcaseModule::toast(std::string text) {
    m_hud.toast = std::move(text);
    m_toastTime = 2.5f;
    if (m_hudModel) m_hudModel.DirtyVariable("toast");
}

} // namespace kke_showcase
