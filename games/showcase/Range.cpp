// The firing range (Kees, 2026-10-03: "a gun to shoot things and see
// destruction"), in its zone north east of the yard (layout::kZones[2]).
// You stand behind the bench at x 140 and shoot toward +X down three
// lanes, each 10 m wide:
//
//   z -167 to -157  steel plates on plinths at 15, 28 and 45 m: they ring
//                   and fall over when hit (the HUD counts them)
//   z -156 to -146  things that break (FEMFX): a glass pane, a plank
//                   across two blocks, two stone walls
//   z -145 to -134  red barrels that explode (one sets off the next), a
//                   pile of crates beside them, two dummies
//
// An earth bank at 72 m stops what flies. The bench has the rifle, the
// pistol, rounds and grenades on it (Items.cpp places them). Reset the
// world (spawn menu, pause menu) puts the whole range back.
// KKE_DEMO_GUNS=1 shoots its way down the range by itself.

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/FracturePattern.h"
#include "kke/modules/PhysicsBridgeModule.h"
#include "kke/modules/PhysicsModule.h"
#endif

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace kke_showcase {

using namespace layout;

namespace {

constexpr uint32_t kWood = 2, kMetal = 3;
constexpr float kLaneA = -162.0f, kLaneC = -139.5f; // the lanes' middles (z)
#if KKE_ENABLE_FEMFX
constexpr float kLaneB = -151.0f; // only the breakables stand in the middle lane
#endif
constexpr float kPlinthTop = 0.6f;
// Steel plates: x, z and half size (thin along X, facing the bench).
struct PlateSpot { float x, z; glm::vec3 half; };
constexpr PlateSpot kPlates[] = {
    { 155.0f, -164.4f, { 0.02f, 0.25f, 0.18f } }, { 155.0f, -162.8f, { 0.02f, 0.25f, 0.18f } },
    { 155.0f, -161.2f, { 0.02f, 0.25f, 0.18f } }, { 155.0f, -159.6f, { 0.02f, 0.25f, 0.18f } },
    { 168.0f, -164.0f, { 0.02f, 0.3f, 0.22f } },  { 168.0f, -162.0f, { 0.02f, 0.3f, 0.22f } },
    { 168.0f, -160.0f, { 0.02f, 0.3f, 0.22f } },  { 185.0f, -164.0f, { 0.025f, 0.42f, 0.32f } },
    { 185.0f, -161.5f, { 0.025f, 0.42f, 0.32f } }, { 185.0f, -159.0f, { 0.025f, 0.42f, 0.32f } },
};
const glm::vec3 kSteel(0.62f, 0.64f, 0.66f), kRed(0.75f, 0.12f, 0.08f), kCrate(0.62f, 0.45f, 0.28f);

glm::vec3 aimAngles(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 d = to - from;
    const float yaw = glm::degrees(std::atan2(d.x, -d.z));
    const float pitch = glm::degrees(std::atan2(d.y, glm::length(glm::vec2(d.x, d.z))));
    return glm::vec3(yaw, pitch, 0.0f);
}

} // namespace

// What never moves: the bench, the plinths, the lane lines, the earth bank.
void ShowcaseModule::buildRange(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 wood(0.5f, 0.36f, 0.22f), concrete(0.6f, 0.6f, 0.58f), earth(0.36f, 0.29f, 0.2f), paint(0.92f, 0.92f, 0.88f);
    addStaticBox({ kRangeBench + glm::vec3(0, kRangeBenchHalf.y, 0), kRangeBenchHalf, wood }, v, idx);
    // Posts at the bench's ends (no roof: grenades go up).
    for (float dz : { -6.2f, 6.2f }) addStaticBox({ kRangeBench + glm::vec3(0.0f, 1.0f, dz), { 0.08f, 1.0f, 0.08f }, wood * 0.8f }, v, idx);
    // Plinths for the plates.
    for (float x : { 155.0f, 168.0f, 185.0f }) addStaticBox({ { x, kPlinthTop * 0.5f, kLaneA }, { 0.3f, kPlinthTop * 0.5f, 3.2f }, concrete }, v, idx);
    // White lines between the lanes (paint: no collision), and distance marks.
    for (float z : { -156.5f, -145.5f }) appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(176.0f, 0.02f, z)), glm::vec3(35.0f, 0.02f, 0.06f), paint, v, idx);
    for (float x : { 150.0f, 160.0f, 170.0f, 180.0f, 190.0f, 200.0f })
        appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.02f, -151.0f)), glm::vec3(0.06f, 0.02f, 16.0f), paint * 0.8f, v, idx);
    // The earth bank behind it all.
    addStaticBox({ { 212.0f, 2.5f, -151.0f }, { 2.5f, 2.5f, 22.0f }, earth }, v, idx);
    addStaticBox({ { 209.0f, 1.0f, -151.0f }, { 1.0f, 1.0f, 22.0f }, earth * 1.1f }, v, idx);
}

int ShowcaseModule::plateCount() const {
    int n = 0;
    for (const RangeThing& r : m_range) n += r.kind == RangeKind::Plate ? 1 : 0;
    return n;
}

int ShowcaseModule::platesDown() const {
    const kke::RigidWorld& w = m_rigid->world();
    int n = 0;
    for (const RangeThing& r : m_range)
        if (r.kind == RangeKind::Plate && ((w.rotation(r.body) * glm::vec3(0, 1, 0)).y < 0.6f || w.position(r.body).y < kPlinthTop)) ++n; // over, or off its plinth
    return n;
}

void ShowcaseModule::clearRange() {
    kke::RigidWorld& w = m_rigid->world();
    for (const RangeThing& r : m_range)
        if (r.body != kke::RigidWorld::kNoBody) w.remove(r.body);
    m_range.clear();
    for (const Grenade& g : m_grenades) w.remove(g.body);
    m_grenades.clear();
#if KKE_ENABLE_FEMFX
    if (m_femfx) {
        for (uint32_t h : m_rangeBreakables) m_femfx->removeObject(h);
        for (const Slug& s : m_slugs) m_femfx->removeObject(s.handle);
    }
#endif
    m_rangeBreakables.clear();
    m_slugs.clear();
}

// Everything that moves, new: plates up, barrels and crates in place,
// the breakables whole, the dummies standing.
void ShowcaseModule::spawnRange() {
    clearRange();
    kke::RigidWorld& w = m_rigid->world();
    auto add = [&](RangeKind kind, kke::RigidWorld::BodyDesc d, const glm::vec3& half, const glm::vec3& color) {
        const kke::RigidWorld::BodyId id = w.add(d);
        if (id != kke::RigidWorld::kNoBody) m_range.push_back({ id, kind, half, color, -1.0f });
    };
    for (const PlateSpot& p : kPlates) {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Box;
        d.halfExtents = p.half;
        d.position = glm::vec3(p.x, kPlinthTop + p.half.y + 0.005f, p.z);
        d.mass = 3.0f + 20.0f * p.half.y * p.half.z;
        d.friction = 0.9f;
        d.material = kMetal;
        add(RangeKind::Plate, d, p.half, kSteel);
    }
    // Red barrels in a loose group, and a pile of crates next to them.
    const glm::vec2 barrels[] = { { 160.0f, -141.0f }, { 160.8f, -140.2f }, { 159.9f, -139.2f }, { 161.0f, -138.6f }, { 160.2f, -137.8f } };
    for (const glm::vec2& b : barrels) {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::ConvexHull;
        d.points = cylinderPoints(0.3f, 0.45f);
        d.position = glm::vec3(b.x, 0.46f, b.y);
        d.mass = 60.0f;
        d.material = kMetal;
        add(RangeKind::Barrel, d, glm::vec3(0.3f, 0.45f, 0.3f), kRed);
    }
    for (int level = 0; level < 3; ++level)
        for (int k = 0; k < 3 - level; ++k) {
            kke::RigidWorld::BodyDesc d;
            d.shape = kke::RigidWorld::Shape::Box;
            d.halfExtents = glm::vec3(0.3f);
            d.position = glm::vec3(163.5f, 0.31f + 0.61f * static_cast<float>(level), -136.6f + 0.62f * static_cast<float>(k) + 0.31f * static_cast<float>(level));
            d.density = 250.0f;
            d.material = kWood;
            add(RangeKind::Crate, d, glm::vec3(0.3f), kCrate * (1.0f - 0.06f * static_cast<float>((level + k) % 3)));
        }
    for (float dz : { -4.0f, 4.5f }) spawnDummy(glm::vec3(167.0f, 0.0f, kLaneC + dz));

#if KKE_ENABLE_FEMFX
    if (m_femfx) {
        // The same materials as the breaking yard (spawnBreakables).
        kke::Material stone;
        stone.density = 2500.0f; stone.stiffness = 3.0e7f; stone.poissonsRatio = 0.25f;
        stone.fractureStressThreshold = 1.0e5f; stone.roughness = 0.9f; stone.textureId = 1;
        kke::Material glass;
        glass.density = 2500.0f; glass.stiffness = 7.0e7f; glass.poissonsRatio = 0.22f;
        glass.fractureStressThreshold = 1.0e5f; glass.roughness = 0.05f; glass.textureId = 4;
        kke::Material wood;
        wood.density = 600.0f; wood.stiffness = 1.0e7f; wood.poissonsRatio = 0.3f;
        wood.fractureStressThreshold = 1.5e5f; wood.roughness = 0.75f; wood.textureId = 0;
        kke::Material support = stone;
        support.fractureStressThreshold = 1.0e12f;
        // Unbreakable blocks (FEMFX, so the breakables rest on them; a Jolt
        // twin so you collide with them; the bridge ignores the twin).
        auto block = [&](glm::vec3 size, glm::vec3 at) {
            const uint32_t h = m_femfx->spawnTetMesh(kke::PhysicsModule::buildGridBox(2, 2, 2, size.x, size.y, size.z), at, support);
            if (h != kke::PhysicsModule::kInvalidHandle) m_rangeBreakables.push_back(h);
            kke::RigidWorld::BodyDesc d;
            d.motion = kke::RigidWorld::Motion::Static;
            d.halfExtents = size * 0.5f;
            d.position = at;
            const kke::RigidWorld::BodyId id = w.add(d);
            if (auto* bridge = m_app->getModule<kke::PhysicsBridgeModule>()) bridge->ignore(id);
            // Kept with the range's things so a reset takes it away too (static: never batched as moving).
            m_range.push_back({ id, RangeKind::Crate, glm::vec3(0.0f), glm::vec3(0.0f), -1.0f });
        };
        auto breakable = [&](const glm::ivec3& cells, const glm::vec3& size, const glm::vec3& at, const kke::Material& m, kke::FracturePattern pattern,
                             float chunk, int perCluster) {
            const uint32_t h = m_femfx->spawnPatternedBox(cells, size, at, m, static_cast<int>(pattern), chunk, perCluster, glm::vec3(0.0f), 3.0f);
            if (h != kke::PhysicsModule::kInvalidHandle) m_rangeBreakables.push_back(h);
        };
        // A glass pane standing up on its own (clamping its foot between
        // blocks makes FEMFX push them apart: they start touching).
        breakable({ 2, 8, 12 }, { 0.12f, 1.0f, 1.4f }, { 152.0f, 0.501f, kLaneB - 3.0f }, glass, kke::FracturePattern::Radial, 0.4f, 0);
        // A plank across two blocks at waist height.
        block({ 0.3f, 1.0f, 0.4f }, { 152.0f, 0.5f, kLaneB - 0.2f });
        block({ 0.3f, 1.0f, 0.4f }, { 152.0f, 0.5f, kLaneB + 2.2f });
        breakable({ 2, 1, 16 }, { 0.3f, 0.12f, 2.8f }, { 152.0f, 1.061f, kLaneB + 1.0f }, wood, kke::FracturePattern::Splinters, 0.35f, 0);
        // Two stone walls, the second bigger and further.
        breakable({ 2, 6, 8 }, { 0.25f, 1.0f, 1.4f }, { 154.0f, 0.501f, kLaneB + 4.0f }, stone, kke::FracturePattern::Voronoi, 0.3f, 3);
        breakable({ 2, 8, 10 }, { 0.3f, 1.6f, 2.2f }, { 163.0f, 0.801f, kLaneB }, stone, kke::FracturePattern::Voronoi, 0.35f, 3);
    }
#endif
    kke::log::get(name())->info("range: {} plates, {} things, {} FEMFX objects", plateCount(), m_range.size(), m_rangeBreakables.size());
}

// The moving things in two meshes (plain and metal), like the props.
void ShowcaseModule::batchRange() {
    static std::vector<kke::Vertex> v[2];
    static std::vector<uint32_t> i[2];
    for (int k = 0; k < 2; ++k) {
        v[k].clear();
        i[k].clear();
    }
    const kke::RigidWorld& w = m_rigid->world();
    for (const RangeThing& r : m_range) {
        if (r.body == kke::RigidWorld::kNoBody || r.half.x <= 0.0f) continue;
        const glm::mat4 t = w.transform(r.body);
        switch (r.kind) {
        case RangeKind::Plate:
            appendBox(t, r.half, r.color, v[1], i[1]);
            appendBox(t * glm::translate(glm::mat4(1.0f), glm::vec3(-r.half.x - 0.002f, 0.0f, 0.0f)), glm::vec3(0.002f, r.half.y * 0.35f, r.half.z * 0.35f),
                      glm::vec3(0.9f, 0.85f, 0.2f), v[1], i[1]); // a yellow bull's-eye on the front
            break;
        case RangeKind::Barrel:
            appendCylinder(t, r.half.x, r.half.y, r.color, v[1], i[1], true);
            break;
        case RangeKind::Crate: appendBox(t, r.half, r.color, v[0], i[0]); break;
        }
    }
    for (const Grenade& g : m_grenades) appendSphere(w.transform(g.body), 0.05f, glm::vec3(0.25f, 0.3f, 0.18f), v[1], i[1]);
    m_rangeBatchIndices = i[0].size();
    m_rangeMetalIndices = i[1].size();
    if (!i[0].empty()) m_rangeBatch->upload(v[0], i[0]);
    if (!i[1].empty()) m_rangeMetal->upload(v[1], i[1]);
}

// KKE_DEMO_GUNS=1: at the bench with the rifle, shoot the near plates,
// the glass, the plank and a stone wall, then a red barrel (the rest go
// up after it); the pistol at the middle plates; a grenade at the crates.
// Logs what happened (checks, screenshots).
void ShowcaseModule::updateGunsDemo(float dt, bool& fire, bool& aim) {
    const float before = m_demoGuns;
    m_demoGuns += dt;
    const float t = m_demoGuns;
    auto at = [&](float mark) { return before < mark && t >= mark; };
    auto log = kke::log::get(name());
    kke::RigidWorld& w = m_rigid->world();
    if (at(0.3f)) {
        travelTo(2);
        m_inv.clear();
        for (const auto& [id, n] : { std::pair<const char*, int>{ "rifle", 1 }, { "ammo_rifle", 60 }, { "pistol", 1 }, { "ammo_pistol", 24 }, { "grenade", 3 } })
            m_inv.add(m_items, id, n);
        for (const kke::InventoryItem& it : m_inv.items())
            if (it.id == "rifle") m_inv.equip(m_items, it.uid, kke::EquipSlot::RightHand);
        syncEquipment();
        m_invDirty = true;
    }
    struct Shot { float time; glm::vec3 target; const char* what; };
    const float barrelY = 0.6f;
    static const Shot kShots[] = {
        { 2.6f, { 155.0f, 0.95f, -164.4f }, "plate 1" }, { 3.1f, { 155.0f, 0.95f, -162.8f }, "plate 2" },
        { 3.6f, { 155.0f, 0.95f, -161.2f }, "plate 3" }, { 4.1f, { 155.0f, 0.95f, -159.6f }, "plate 4" },
        { 5.0f, { 152.0f, 0.6f, -154.0f }, "glass" },   { 5.6f, { 151.85f, 1.05f, -150.0f }, "plank" },
        { 6.2f, { 154.0f, 0.6f, -147.0f }, "stone wall" }, { 6.5f, { 154.0f, 0.4f, -147.2f }, "stone wall" },
        { 8.5f, { 160.0f, barrelY, -141.0f }, "red barrel" },
        { 13.3f, { 168.0f, 1.0f, -164.0f }, "plate 5" }, { 13.8f, { 168.0f, 1.0f, -162.0f }, "plate 6" },
        { 14.3f, { 168.0f, 1.0f, -160.0f }, "plate 7" },
    };
    static std::vector<glm::vec3> cratesBefore;
    // Aim at the next target a moment before its shot; fire every shot
    // whose time this frame passed (a slow frame can pass two).
    const Shot* next = nullptr;
    for (const Shot& s : kShots)
        if (t < s.time + 0.3f && t > s.time - 0.7f) {
            next = &s;
            break;
        }
    const glm::vec3 eye = w.characterPosition(m_player) + glm::vec3(0, 1.5f, 0);
    if (next && t < 15.0f) {
        m_demoAim = true;
        m_aimPoint = next->target;
        aim = true;
        const glm::vec3 a = aimAngles(eye, next->target);
        m_rig.yaw = a.x;
        m_rig.pitch = a.y;
    }
    for (const Shot& s : kShots)
        if (at(s.time)) {
            m_aimPoint = s.target;
            fire = true;
            const GunDef* gun = gunInHand();
            log->info("guns demo: {} at {:.1f} s, {} rounds in the gun", s.what, t, gun ? m_loaded[gun->id] : 0);
        }
    if (at(7.6f)) {
#if KKE_ENABLE_FEMFX
        std::string pieces;
        if (m_femfx)
            for (uint32_t h : m_rangeBreakables) pieces += fmt::format("{} ", m_femfx->pieceCount(h));
        log->info("guns demo: plates down {} of {}; FEMFX pieces per object: {}", platesDown(), plateCount(), pieces);
#else
        log->info("guns demo: plates down {} of {}", platesDown(), plateCount());
#endif
        cratesBefore.clear();
        for (const RangeThing& r : m_range)
            if (r.kind == RangeKind::Crate && r.half.x > 0.0f) cratesBefore.push_back(w.position(r.body));
    }
    if (at(11.5f)) {
        int gone = 0;
        for (const RangeThing& r : m_range) gone += r.kind == RangeKind::Barrel && r.body == kke::RigidWorld::kNoBody ? 1 : 0;
        float moved = 0.0f;
        size_t k = 0;
        for (const RangeThing& r : m_range)
            if (r.kind == RangeKind::Crate && r.half.x > 0.0f && k < cratesBefore.size()) moved = std::max(moved, glm::length(w.position(r.body) - cratesBefore[k++]));
        log->info("guns demo: {} of 5 barrels went up; the crates flew up to {:.1f} m", gone, moved);
    }
    if (at(11.6f))
        for (const kke::InventoryItem& it : m_inv.items())
            if (it.id == "pistol") {
                m_inv.equip(m_items, it.uid, kke::EquipSlot::RightHand);
                syncEquipment();
            }
    if (at(15.0f)) {
        log->info("guns demo: plates down {} of {} after the pistol", platesDown(), plateCount());
        for (const kke::InventoryItem& it : m_inv.items())
            if (it.id == "grenade") {
                m_inv.equip(m_items, it.uid, kke::EquipSlot::RightHand);
                syncEquipment();
                break;
            }
    }
    if (t > 15.0f && t < 16.4f) {
        const glm::vec3 target(164.0f, 17.0f, -155.0f); // high: it arcs down between the stone wall and the far plates
        m_demoAim = true;
        m_aimPoint = target;
        aim = true;
        const glm::vec3 a = aimAngles(eye, target);
        m_rig.yaw = a.x;
        m_rig.pitch = a.y * 0.4f;
        if (at(15.8f)) fire = true;
    }
    if (at(16.4f)) m_demoAim = false;
    if (at(32.0f)) {
        log->info("guns demo: done; {} rifle and {} pistol rounds left in the bag, {} grenades", m_inv.count("ammo_rifle"), m_inv.count("ammo_pistol"),
                  m_inv.count("grenade"));
        m_demoGuns = -1.0f;
    }
}

} // namespace kke_showcase
