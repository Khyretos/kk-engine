// kke_demo's spawn menu (Kees, 2026-09-28: "rb spawns boxes but i cannot
// reset the world or unspawn them so a submenu for that would be nice to
// select what i want to spawn"). RB on a controller or G on the keyboard
// opens a list over the right of the screen while the game keeps running;
// the d-pad, the left stick, W/S or the arrows move through it, A, Space,
// Enter or a click spawns the row in front of you (the list stays open to
// spawn more), B, RB, G or Backspace closes it. Its last rows clear what
// you spawned and put the whole world back.

#include "ShowcaseModule.h"
#include "Geometry.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/UiModule.h"
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#endif

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/ElementDocument.h>
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke_showcase {

namespace {

// The rows, in order. The two after the last thing are the clean-up rows.
enum SpawnRowId : int {
    kRowCrate,
    kRowSmallCrate,
    kRowBigBox,
    kRowHeavyCrate,
    kRowBarrel,
    kRowBall,
    kRowTower,
    kRowDummy,
    kRowClear,
    kRowReset,
    kRowCount
};

// Audio materials (AudioModule's defaults): what a hit sounds like.
constexpr uint32_t kStone = 1, kWood = 2, kMetal = 3;
// The most anything may be: past this the oldest spawned thing goes.
constexpr size_t kMaxProps = 300;

} // namespace

void ShowcaseModule::buildSpawnMenu() {
    m_propBatch = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_propMetal = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_spawnRows = {
        { "Wooden crate", "Pick it up, throw it, stack it" },
        { "Small crate", "Light: carry it anywhere" },
        { "Big light box", "Big but light: push it around" },
        { "Iron crate", "Too heavy to lift: push or shoot it" },
        { "Barrel", "Rolls when it falls over" },
        { "Rubber ball", "Bounces" },
        { "Crate tower", "Twelve metal crates to knock down" },
        { "Ragdoll dummy", "Push it, shoot it, watch it fall" },
        { "Clear what I spawned", "Everything from this menu goes" },
        { "Reset the world", "Spawned things go, crates and you back to the start" },
    };
    static_assert(kRowCount == 10, "one SpawnRow per row id");
    kke::InputMap& in = m_input->map(0);
    using IM = kke::InputModule;
    // Open and close: in its own context, so it still closes the menu
    // while the game's actions are off.
    in.defineAction({ "spawn.menu", "Spawn menu", "Showcase", "spawn" });
    in.addBinding(IM::bind("spawn.menu", IM::key(SDL_SCANCODE_G)));
    in.addBinding(IM::bind("spawn.menu", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
    // In the menu: the game's actions are off, these move and pick.
    in.defineAction({ "spawn.up", "Spawn menu: up", "Showcase", "spawnlist" });
    in.defineAction({ "spawn.down", "Spawn menu: down", "Showcase", "spawnlist" });
    in.defineAction({ "spawn.pick", "Spawn menu: spawn", "Showcase", "spawnlist" });
    in.defineAction({ "spawn.close", "Spawn menu: close", "Showcase", "spawnlist" });
    for (SDL_Scancode k : { SDL_SCANCODE_W, SDL_SCANCODE_UP }) in.addBinding(IM::bind("spawn.up", IM::key(k)));
    for (SDL_Scancode k : { SDL_SCANCODE_S, SDL_SCANCODE_DOWN }) in.addBinding(IM::bind("spawn.down", IM::key(k)));
    in.addBinding(IM::bind("spawn.up", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_UP)));
    in.addBinding(IM::bind("spawn.down", IM::pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN)));
    in.addBinding(IM::bind("spawn.up", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY, -1)));
    in.addBinding(IM::bind("spawn.down", IM::padAxis(SDL_GAMEPAD_AXIS_LEFTY, 1)));
    for (SDL_Scancode k : { SDL_SCANCODE_SPACE, SDL_SCANCODE_RETURN, SDL_SCANCODE_KP_ENTER }) in.addBinding(IM::bind("spawn.pick", IM::key(k)));
    in.addBinding(IM::bind("spawn.pick", IM::pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    in.addBinding(IM::bind("spawn.close", IM::key(SDL_SCANCODE_BACKSPACE)));
    in.addBinding(IM::bind("spawn.close", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
    in.setContextEnabled("spawnlist", false);

    if (!m_ui || !m_ui->context()) return;
    Rml::Context* ctx = m_ui->context();
    Rml::DataModelConstructor c = ctx->CreateDataModel("spawn");
    if (!c) return;
    if (auto row = c.RegisterStruct<SpawnRow>()) {
        row.RegisterMember("label", &SpawnRow::label);
        row.RegisterMember("hint", &SpawnRow::hint);
    }
    c.RegisterArray<std::vector<SpawnRow>>();
    c.Bind("rows", &m_spawnRows);
    c.Bind("sel", &m_spawnSel);
    c.Bind("open", &m_spawnOpen);
    c.Bind("note", &m_spawnNote);
    c.BindEventCallback("pick", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList& args) {
        if (!args.empty()) spawnRow(args[0].Get<int>());
    });
    c.BindEventCallback("hover", [this](Rml::DataModelHandle h, Rml::Event&, const Rml::VariantList& args) {
        if (args.empty() || args[0].Get<int>() == m_spawnSel) return;
        m_spawnSel = args[0].Get<int>();
        h.DirtyVariable("sel");
    });
    m_spawnModel = c.GetModelHandle();
    const char* base = SDL_GetBasePath();
    const std::string root = std::string(base ? base : "") + "ui/";
    m_spawnDoc = ctx->LoadDocument(root + "showcase_spawn.rml");
    if (!m_spawnDoc) {
        kke::log::get(name())->warn("spawn menu: could not load {}showcase_spawn.rml", root);
        return;
    }
    m_spawnDoc->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
}

void ShowcaseModule::openSpawnMenu(bool open) {
    if (open == m_spawnOpen) return;
    if (open) {
        openInventory(false);
        openMap(false);
    }
    m_spawnOpen = open;
    kke::InputMap& in = m_input->map(0);
    in.setContextEnabled("spawnlist", open);
    if (open) {
        // The mouse is free to click a row while the list is up.
        m_spawnRecapture = m_captured;
        if (m_captured) setCaptured(false);
        m_spawnNote.clear();
    } else if (m_spawnRecapture) {
        setCaptured(true);
        m_swallowFire = true; // the click that picked the last row isn't a shot
    }
    m_spawnHeldDir = 0;
    if (m_spawnModel) {
        m_spawnModel.DirtyVariable("open");
        m_spawnModel.DirtyVariable("note");
    }
}

void ShowcaseModule::updateSpawnMenu(float dt) {
    kke::InputMap& in = m_input->map(0);
    // The pause menu has the screen: no spawn menu under it.
    in.setContextEnabled("spawn", !m_menuOpen && !m_invOpen && !m_mapOpen);
    if (m_menuOpen) {
        openSpawnMenu(false);
        return;
    }
    if (in.pressed("spawn.menu")) {
        openSpawnMenu(!m_spawnOpen);
        return;
    }
    if (!m_spawnOpen) return;
    if (in.pressed("spawn.close")) {
        openSpawnMenu(false);
        return;
    }
    // Up and down: one step on press, then repeating while held.
    const int dir = in.held("spawn.down") ? 1 : in.held("spawn.up") ? -1 : 0;
    int step = 0;
    if (dir != m_spawnHeldDir) {
        m_spawnHeldDir = dir;
        m_spawnRepeat = 0.4f;
        step = dir;
    } else if (dir != 0 && (m_spawnRepeat -= dt) <= 0.0f) {
        m_spawnRepeat = 0.12f;
        step = dir;
    }
    if (step != 0) {
        m_spawnSel = (m_spawnSel + step + kRowCount) % kRowCount;
        if (m_spawnModel) m_spawnModel.DirtyVariable("sel");
    }
    if (in.pressed("spawn.pick")) spawnRow(m_spawnSel);
}

glm::vec3 ShowcaseModule::spawnSpot(float distance) const {
    const kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 feet = w.characterPosition(m_player);
    glm::vec3 f = m_loco ? m_loco->facing() : glm::vec3(0, 0, -1);
    f.y = 0.0f;
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    // Not through a wall: as far as there's room in front of the chest.
    const glm::vec3 chest = feet + glm::vec3(0, 1.0f, 0);
    const kke::RigidWorld::RayHit wall = w.raycast(chest, f, distance);
    const float d = wall.hit ? std::max(0.6f, wall.distance - 0.5f) : distance;
    glm::vec3 p = chest + f * d;
    // Onto whatever is under that spot.
    const kke::RigidWorld::RayHit down = w.raycast(p + glm::vec3(0, 1.5f, 0), glm::vec3(0, -1, 0), 6.0f);
    if (down.hit) p.y = down.point.y;
    else p.y = feet.y;
    return p;
}

void ShowcaseModule::spawnProp(PropShape shape, const glm::vec3& half, float density, const glm::vec3& color, uint32_t material,
                               const glm::vec3& at, float metallic, float restitution) {
    if (m_props.size() >= kMaxProps) { // the oldest goes
        if (m_held.body == m_props.front().body) dropHeld();
        m_rigid->world().remove(m_props.front().body);
        m_props.erase(m_props.begin());
    }
    kke::RigidWorld::BodyDesc d;
    switch (shape) {
    case PropShape::Box:
        d.shape = kke::RigidWorld::Shape::Box;
        d.halfExtents = half;
        break;
    case PropShape::Sphere:
        d.shape = kke::RigidWorld::Shape::Sphere;
        d.radius = half.x;
        break;
    case PropShape::Barrel:
        d.shape = kke::RigidWorld::Shape::ConvexHull;
        d.points = cylinderPoints(half.x, half.y);
        break;
    }
    d.position = at;
    d.density = density;
    d.material = material;
    d.restitution = restitution;
    // A little spin so a pile doesn't stack like bricks.
    d.rotation = glm::angleAxis(glm::radians(static_cast<float>(m_props.size() % 7) * 13.0f), glm::vec3(0, 1, 0));
    const kke::RigidWorld::BodyId id = m_rigid->world().add(d);
    if (id == kke::RigidWorld::kNoBody) {
        m_spawnNote = "The physics world is full";
        return;
    }
    m_props.push_back({ id, shape, half, color, metallic });
}

// The mannequin, standing where it lands, then limp: a ragdoll on Jolt
// (joint limits, limbs that collide), its skeleton following the bodies.
void ShowcaseModule::spawnDummy(const glm::vec3& at) {
    const kke::ModelData* d = m_ualModel ? m_models->model(m_ualModel) : nullptr;
    if (!d) {
        m_spawnNote = "No dummy without the animation library (assets/animations/UAL1_Standard.fbx)";
        return;
    }
    if (m_dummies.size() >= 8) { // a few at a time: each is 11 bodies and a skinned mesh
        m_rigid->world().removeRagdoll(m_dummies.front().ragdoll);
        m_models->remove(m_dummies.front().instance);
        m_dummies.erase(m_dummies.begin());
    }
    // Facing you, standing in the pose you're in (the mannequin's own
    // skeleton: when you play a Synty character, its rest pose).
    const glm::vec3 toYou = m_rigid->world().characterPosition(m_player) - at;
    const float yaw = std::atan2(toYou.x, toYou.z);
    const glm::mat4 place = glm::rotate(glm::translate(glm::mat4(1.0f), at + glm::vec3(0, 0.02f, 0)), yaw, glm::vec3(0, 1, 0));
    SpawnDummy dummy;
    dummy.instance = m_models->spawn(m_ualModel, place);
    std::vector<glm::mat4> world;
    if (m_anim && m_character.empty() && m_rigData.bones.size() == d->bones.size()) world = kke::poseToModel(m_rigData, m_anim->pose());
    else world = m_models->boneWorld(dummy.instance);
    for (glm::mat4& w : world) w = place * w;
    std::string missing;
    const kke::RagdollDesc desc = kke::buildHumanoidRagdoll(*d, world, 70.0f, &missing);
    if (desc.bodies.empty()) {
        m_models->remove(dummy.instance);
        m_spawnNote = "The mannequin has no '" + missing + "' bone: no ragdoll";
        return;
    }
    dummy.binding = kke::bindSkeletonToRagdoll(*d, world, desc);
    dummy.ragdoll = m_rigid->world().addRagdoll(desc, glm::vec3(0.0f));
    if (!dummy.ragdoll) {
        m_models->remove(dummy.instance);
        m_spawnNote = "The physics world is full";
        return;
    }
    m_models->setTint(dummy.instance, glm::vec3(1.0f, 0.78f, 0.5f)); // not you: a warmer colour
    m_dummies.push_back(std::move(dummy));
}

void ShowcaseModule::updateDummies() {
    std::vector<glm::mat4> bodies;
    const kke::ModelData* d = m_ualModel ? m_models->model(m_ualModel) : nullptr;
    if (!d) return;
    for (SpawnDummy& dm : m_dummies) {
        if (!m_rigid->world().ragdollTransforms(dm.ragdoll, bodies) || bodies.empty()) continue;
        // The instance follows the pelvis, so its bounds (culling) and
        // shadow stay with the body.
        const glm::vec3 pelvis = glm::vec3(bodies[0][3]);
        const glm::mat4 place = glm::translate(glm::mat4(1.0f), pelvis);
        m_models->setTransform(dm.instance, place);
        m_models->setBoneWorldOverride(dm.instance, kke::poseFromRagdoll(*d, dm.binding, bodies, glm::inverse(place)));
    }
}

void ShowcaseModule::clearSpawned() {
    if (m_held.body != kke::RigidWorld::kNoBody)
        for (const Prop& p : m_props)
            if (p.body == m_held.body) dropHeld();
    for (const Prop& p : m_props) m_rigid->world().remove(p.body);
    m_props.clear();
    for (const SpawnDummy& dm : m_dummies) {
        m_rigid->world().removeRagdoll(dm.ragdoll);
        m_models->remove(dm.instance);
    }
    m_dummies.clear();
}

// Everything back the way the demo started: what you spawned goes, the
// course's crates go back to their piles, the lava starts over, you're
// back at the start.
void ShowcaseModule::resetWorld() {
    dropHeld();
    clearSpawned();
    resetCourse();
    spawnRange(); // plates up, barrels back, breakables whole, dummies standing
    // The bag empties, its things back on the supply table.
    openInventory(false);
    m_inv.clear();
    syncEquipment();
    placeItems();
    m_invDirty = true;
    m_loco->teleport(m_spawn);
    m_ik.reset();
}

void ShowcaseModule::spawnRow(int row) {
#if KKE_ENABLE_NET
    // Online the host's world is the real one; spawning there for
    // everyone is still to come.
    if (m_net && m_net->connected() && row != kRowReset) {
        m_spawnNote = "Spawning is offline only for now";
        if (m_spawnModel) m_spawnModel.DirtyVariable("note");
        return;
    }
#endif
    m_spawnNote.clear();
    const glm::vec3 at = spawnSpot(2.2f);
    const glm::vec3 wood(0.62f, 0.45f, 0.28f), metal(0.55f, 0.6f, 0.68f);
    switch (row) {
    case kRowCrate: spawnProp(PropShape::Box, glm::vec3(0.3f), 250.0f, wood, kWood, at + glm::vec3(0, 0.32f, 0)); break;
    case kRowSmallCrate: spawnProp(PropShape::Box, glm::vec3(0.2f), 250.0f, glm::vec3(0.7f, 0.55f, 0.35f), kWood, at + glm::vec3(0, 0.22f, 0)); break;
    case kRowBigBox: spawnProp(PropShape::Box, glm::vec3(0.6f), 60.0f, glm::vec3(0.3f, 0.6f, 0.4f), kWood, at + glm::vec3(0, 0.62f, 0)); break;
    case kRowHeavyCrate: spawnProp(PropShape::Box, glm::vec3(0.4f), 2000.0f, metal * 0.8f, kMetal, at + glm::vec3(0, 0.42f, 0), 0.8f); break;
    case kRowBarrel:
        spawnProp(PropShape::Barrel, glm::vec3(0.28f, 0.42f, 0.28f), 300.0f, glm::vec3(0.25f, 0.42f, 0.62f), kMetal, at + glm::vec3(0, 0.44f, 0), 0.4f);
        break;
    case kRowBall:
        spawnProp(PropShape::Sphere, glm::vec3(0.25f), 300.0f, glm::vec3(0.9f, 0.25f, 0.2f), kStone, at + glm::vec3(0, 0.6f, 0), 0.0f, 0.75f);
        break;
    case kRowTower: {
        // Two crates a layer, crossed layer on layer, six high.
        const glm::vec3 base = spawnSpot(3.0f);
        for (int level = 0; level < 6; ++level)
            for (int k = 0; k < 2; ++k) {
                const glm::vec3 off = level % 2 == 0 ? glm::vec3(k * 0.52f - 0.26f, 0, 0) : glm::vec3(0, 0, k * 0.52f - 0.26f);
                spawnProp(PropShape::Box, glm::vec3(0.25f), 700.0f, metal, kMetal, base + off + glm::vec3(0, 0.26f + level * 0.52f, 0), 0.6f);
            }
        break;
    }
    case kRowDummy: spawnDummy(spawnSpot(3.0f)); break;
    case kRowClear: clearSpawned(); break;
    case kRowReset: resetWorld(); break;
    default: return;
    }
    // What happened, under the list (a failure has already said why).
    static const char* const kDone[kRowCount] = { "A crate",  "A small crate", "A big box", "An iron crate", "A barrel",
                                                  "A ball",   "A tower",       "A dummy",   "Cleared",       "The world is back as it was" };
    if (m_spawnNote.empty()) m_spawnNote = kDone[row];
    kke::log::get(name())->info("spawn menu: {} ({:.1f} {:.1f} {:.1f}), {} props, {} dummies", m_spawnNote, at.x, at.y, at.z, m_props.size(), m_dummies.size());
    if (m_spawnModel) m_spawnModel.DirtyVariable("note");
}

// Every prop in two meshes (plain and metal), rebuilt from the bodies each
// frame like the course's crates: two draws however many there are.
void ShowcaseModule::batchProps() {
    static std::vector<kke::Vertex> v[2];
    static std::vector<uint32_t> i[2];
    for (int k = 0; k < 2; ++k) {
        v[k].clear();
        i[k].clear();
    }
    const kke::RigidWorld& w = m_rigid->world();
    for (const Prop& p : m_props) {
        const int k = p.metallic > 0.3f ? 1 : 0;
        const glm::mat4 t = w.transform(p.body);
        switch (p.shape) {
        case PropShape::Box: appendBox(t, p.half, p.color, v[k], i[k]); break;
        case PropShape::Sphere: appendSphere(t, p.half.x, p.color, v[k], i[k]); break;
        case PropShape::Barrel: appendCylinder(t, p.half.x, p.half.y, p.color, v[k], i[k], true); break;
        }
    }
    m_propBatchIndices = i[0].size();
    m_propMetalIndices = i[1].size();
    if (!i[0].empty()) m_propBatch->upload(v[0], i[0]);
    if (!i[1].empty()) m_propMetal->upload(v[1], i[1]);
}

} // namespace kke_showcase
