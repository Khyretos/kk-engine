#include "PlatoonModule.h"

#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Picking.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/ModelModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace platoon {

namespace {

constexpr float kEye = 1.45f; // chest / eye height for shots and sight

glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }
float angleDelta(float from, float to) { return std::fmod(to - from + 540.0f, 360.0f) - 180.0f; }
float aiYaw(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, d.z)); }

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::string(v) != "0";
}

// The wheel, clockwise from the top.
enum Wheel { WMove, WAttack, WFocus, WHold, WCover, WRegroup, WFollow, WFormation };
const char* kWheelIcons[] = { "📍", "🎯", "💥", "✋", "🛡", "🔄", "👣", "🔷" };
const char* kWheelLabels[] = { "Go there", "Attack", "Focus fire", "Hold", "Take cover", "Regroup", "Follow", "Formation" };

// Buttons along the bottom (touch: one tap, one order; mouse and keys too).
enum Button { BAll, BHold, BCover, BAttack, BFocus, BRegroup, BFormation };

std::unique_ptr<kke::DynamicMeshRenderer> boxMesh(kke::Application& app, const glm::vec3& color) {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    command_kit::appendBox(glm::vec3(0.0f), glm::vec3(0.5f), color, v, idx);
    auto m = std::make_unique<kke::DynamicMeshRenderer>(app);
    m->upload(v, idx);
    return m;
}

// A unit box stretched from `a` to `b`, `thick` across.
glm::mat4 beam(const glm::vec3& a, const glm::vec3& b, float thick) {
    const glm::vec3 d = b - a;
    const float len = glm::length(d);
    if (len < 1e-4f) return glm::scale(glm::translate(glm::mat4(1.0f), a), glm::vec3(thick));
    const glm::vec3 z = d / len;
    const glm::vec3 up = std::abs(z.y) > 0.95f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    const glm::vec3 x = glm::normalize(glm::cross(up, z));
    const glm::vec3 y = glm::cross(z, x);
    glm::mat4 m(1.0f);
    m[0] = glm::vec4(x * thick, 0.0f);
    m[1] = glm::vec4(y * thick, 0.0f);
    m[2] = glm::vec4(z * len, 0.0f);
    m[3] = glm::vec4((a + b) * 0.5f, 1.0f);
    return m;
}

} // namespace

PlatoonModule::PlatoonModule() = default;
PlatoonModule::~PlatoonModule() = default;

std::vector<kke::ModuleDependency> PlatoonModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the field's cover and line of sight" },
             { std::type_index(typeid(kke::InputModule)), true, "selecting and ordering" },
             { std::type_index(typeid(kke::ModelModule)), true, "the soldiers and the field" },
             { std::type_index(typeid(kke::ScriptModule)), false, "order.* in Lua (scripts/platoon.lua)" } };
}

void PlatoonModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();

    kke::InputMap& in = m_input->map(0);
    kke::InputModule::defineCharacterActions(in); // "move" pans, "look.rate" turns
    command_kit::CommandInput::defineActions(in);
    // The character defaults the platoon doesn't use would fire on keys it
    // does (Space jump vs rts.all, E interact vs rts.right, Q and the d-pad
    // down audio.ping vs rts.left and rts.regroup, RT fire vs rts.groups:
    // BUG-065). A top-down squad has nobody to walk or ping for.
    for (const char* unused : { "jump", "sprint", "walk", "crouch", "fire", "aim", "interact", "camera.toggle", "audio.ping", "voice.talk" })
        in.clearBindings(unused);
    using IM = kke::InputModule;
    auto quick = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        in.defineAction({ id, label, "Orders", "game" });
        if (key != SDL_SCANCODE_UNKNOWN) in.addBinding(IM::bind(id, IM::key(key)));
        if (pad != SDL_GAMEPAD_BUTTON_INVALID) in.addBinding(IM::bind(id, IM::pad(pad)));
    };
    quick("rts.select", "Select what's under the pointer", SDL_SCANCODE_UNKNOWN, SDL_GAMEPAD_BUTTON_SOUTH);
    quick("rts.all", "Select everyone", SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_DPAD_UP);
    quick("rts.next", "Next soldier", SDL_SCANCODE_PERIOD, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    quick("rts.prev", "Previous soldier", SDL_SCANCODE_COMMA, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    quick("rts.hold", "Hold position", SDL_SCANCODE_H, SDL_GAMEPAD_BUTTON_WEST);
    quick("rts.cover", "Take cover", SDL_SCANCODE_C, SDL_GAMEPAD_BUTTON_NORTH);
    quick("rts.regroup", "Regroup", SDL_SCANCODE_R, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    quick("rts.formation", "Next formation", SDL_SCANCODE_G, SDL_GAMEPAD_BUTTON_BACK);
    quick("rts.left", "Turn the view left", SDL_SCANCODE_Q, SDL_GAMEPAD_BUTTON_INVALID);
    quick("rts.right", "Turn the view right", SDL_SCANCODE_E, SDL_GAMEPAD_BUTTON_INVALID);
    quick("panels", "Developer panels", SDL_SCANCODE_F1, SDL_GAMEPAD_BUTTON_INVALID);
    // Groups: 1-9 recall, with cmd.force held (Ctrl / LT) they store. On a
    // controller hold RT (rts.groups) and the d-pad is groups 1-4 (up,
    // right, down, left) instead of its usual orders.
    for (int g = 1; g <= 9; ++g) {
        const std::string id = "rts.group" + std::to_string(g);
        in.defineAction({ id, "Group " + std::to_string(g) + " (Ctrl: store)", "Orders", "game" });
        in.addBinding(IM::bind(id, IM::key(SDL_Scancode(SDL_SCANCODE_1 + g - 1))));
    }
    in.defineAction({ "rts.groups", "Hold: the d-pad picks groups 1-4", "Orders", "game" });
    kke::Binding rt = IM::bind("rts.groups", IM::padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 1), kke::Trigger::Continuous);
    rt.threshold = 0.4f;
    in.addBinding(rt);
    const SDL_GamepadButton groupPad[] = { SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
                                           SDL_GAMEPAD_BUTTON_DPAD_LEFT };
    for (int g = 1; g <= 4; ++g) in.addBinding(IM::bind("rts.group" + std::to_string(g), IM::pad(groupPad[g - 1])));
    // The settings panel (kke::DemoPanelModule) opens with Start or F3 here:
    // View is rts.formation. Esc opens it too, with a Quit row.
    in.defineAction({ "panel.toggle", "Settings panel", "Menus", "panel" });
    in.addBinding(IM::bind("panel.toggle", IM::pad(SDL_GAMEPAD_BUTTON_START)));
    in.addBinding(IM::bind("panel.toggle", IM::key(SDL_SCANCODE_F3)));
    m_input->commitDefaults();
    buildPanel();

    m_scenery = std::make_unique<command_kit::Scenery>(app, *m_models, *m_rigid);
    buildField();
    m_kit = std::make_unique<command_kit::HumanoidKit>(app);
    m_kit->load(*m_models);

    // Soldiers: one species for both sides; the team makes them enemies.
    kke::ai::Species s;
    s.id = "soldier";
    s.label = "Soldier";
    s.walkSpeed = 1.6f;
    s.runSpeed = 4.2f;
    s.acceleration = 10.0f;
    s.turnRate = 540.0f;
    s.radius = 0.35f;
    s.senses.sightRange = 30.0f;
    s.senses.fovDegrees = 200.0f;
    s.senses.smell = 0.0f;
    s.senses.eyeHeight = kEye;
    s.temperament = { 0.95f, 0.1f, 0.9f, 0.3f };
    s.eats.clear();
    s.drinks.clear();
    s.needs.clear();
    s.attackRange = 11.0f;
    s.attackCooldown = 1.6f;
    s.homeRadius = 40.0f;
    s.scent = 0.0f;
    m_ai.defineSpecies(s);
    m_ai.lineOfSight = [this](const glm::vec3& from, const glm::vec3& to) { return clearShot(from, to); };

    // Alpha squad (1-3) and Bravo (4-6), and five enemies behind the barriers.
    const char* names[] = { "Alpha 1", "Alpha 2", "Alpha 3", "Bravo 1", "Bravo 2", "Bravo 3" };
    for (uint32_t i = 0; i < 6; ++i) addSoldier(kFirstSoldier + i, names[i], { -5.0f + float(i) * 2.0f, 0.0f, 22.0f + float(i % 2) }, false);
    const glm::vec3 posts[] = { { -8.0f, 0, -13.5f }, { -2.5f, 0, -15.5f }, { 3.0f, 0, -13.5f }, { 8.5f, 0, -15.0f }, { 0.5f, 0, -20.0f } };
    for (uint32_t i = 0; i < 5; ++i) addSoldier(kFirstEnemy + i, "Enemy " + std::to_string(i + 1), posts[i], true);
    for (const Soldier& e : m_soldiers)
        if (e.enemy) {
            kke::ai::Order hold;
            hold.kind = kke::ai::Order::Kind::Hold; // their own orders: hold the line
            hold.position = e.pos;
            m_ai.order(e.id, hold);
        }
    m_selection.set(living(false));
    m_selection.storeGroup(1);
    m_selection.set({ 1, 2, 3 });
    m_selection.storeGroup(2);
    m_selection.set({ 4, 5, 6 });
    m_selection.storeGroup(3);
    m_selection.set(living(false));

    m_board.setPositionFn([this](uint32_t id) {
        const kke::ai::Agent* a = m_ai.agent(id);
        return a ? a->position : glm::vec3(NAN);
    });
    m_board.listen({ [this](uint32_t unit, const kke::UnitOrder& o) {
                        kke::log::get("Platoon")->info("{}: {}", soldier(unit) ? soldier(unit)->name : "?", kke::orderName(o.kind));
                        // A new order frees the cover spot unless it is the order to hold one.
                        if (Soldier* s = soldier(unit); s && s->coverSpot >= 0 && !(o.kind == kke::OrderKind::Stay && o.hasPoint &&
                                                                                      glm::length(o.point - m_cover[size_t(s->coverSpot)].pos) < 0.1f)) {
                            m_cover[size_t(s->coverSpot)].taken = 0;
                            s->coverSpot = -1;
                        }
                    },
                     {} });
    m_bridge = std::make_unique<kke::AiOrderBridge>(m_board, m_ai);
    m_bridge->runBeyond = 6.0f;
    if (auto* scripts = app.getModule<kke::ScriptModule>()) m_script = std::make_unique<kke::OrderScript>(scripts->vm(), m_board, *this);

    m_ring = boxMesh(app, { 1.0f, 0.85f, 0.25f });
    m_enemyRing = boxMesh(app, { 1.0f, 0.2f, 0.15f });
    m_hpBack = boxMesh(app, { 0.25f, 0.05f, 0.05f });
    m_hpFront = boxMesh(app, { 0.3f, 0.95f, 0.4f });
    m_tracer = boxMesh(app, { 3.0f, 2.6f, 1.2f });
    m_marker = boxMesh(app, { 0.35f, 0.7f, 1.0f });

    m_hud = std::make_unique<command_kit::CommandHud>(app);
    m_hud->build("PLATOON");
    m_hud->onButton = [this](int b) { pressButton(b); };
    std::vector<command_kit::CommandHud::WheelItem> wheel;
    for (size_t i = 0; i < std::size(kWheelLabels); ++i) wheel.push_back({ kWheelIcons[i], kWheelLabels[i] });
    m_hud->setWheel(std::move(wheel));
    m_cmd.overUi = [this](const glm::vec2& p) { return m_hud->overButtons(p); };

    m_demo = envOn("KKE_PLATOON_DEMO");
    if (const char* q = std::getenv("KKE_PLATOON_QUIT")) m_quitAfter = float(std::atof(q));
    if (m_demo && m_quitAfter < 0.0f) m_quitAfter = 150.0f;
    m_scenery->logUsed(name());
    kke::log::get(name())->info("the field is ready: {} soldiers, {} enemies, {} cover spots{}", living(false).size(), living(true).size(),
                                m_cover.size(), m_kit->loaded() ? "" : " (no mannequin: soldiers are blocks)");
}

void PlatoonModule::shutdown() {
    m_script.reset();
    m_bridge.reset();
    m_hud.reset();
    m_soldiers.clear();
    m_ring.reset();
    m_enemyRing.reset();
    m_hpBack.reset();
    m_hpFront.reset();
    m_tracer.reset();
    m_marker.reset();
    m_kit.reset();
    m_scenery.reset();
}

// ---------------------------------------------------------------- the field

void PlatoonModule::buildField() {
    command_kit::Scenery& s = *m_scenery;
    s.ground(45.0f, { 0.36f, 0.42f, 0.3f });
    // No-man's-land: crates and barriers to hide behind on the way over,
    // the enemy's walls at the far end.
    addCover("SM_Prop_Crate_01", { -7.0f, 0, 9.0f }, 10.0f, { 0.75f, 0.75f, 0.75f }, true);
    addCover("SM_Prop_Crate_02", { -1.5f, 0, 8.0f }, -15.0f, { 0.75f, 0.75f, 0.75f }, true);
    addCover("SM_Prop_Barrier_01", { 4.0f, 0, 9.5f }, 0.0f, { 1.6f, 0.6f, 0.35f }, true);
    addCover("SM_Prop_Crate_01", { 9.5f, 0, 7.5f }, 30.0f, { 0.75f, 0.75f, 0.75f }, true);
    addCover("SM_Prop_Barrier_01", { -4.0f, 0, 1.0f }, 0.0f, { 1.6f, 0.6f, 0.35f }, true);
    addCover("SM_Prop_Crate_03", { 2.0f, 0, 0.0f }, 45.0f, { 0.75f, 0.75f, 0.75f }, true);
    addCover("SM_Prop_Barrier_01", { 7.5f, 0, 1.5f }, 0.0f, { 1.6f, 0.6f, 0.35f }, true);
    addCover("SM_Prop_Barrier_01", { -8.0f, 0, -12.0f }, 0.0f, { 1.6f, 0.6f, 0.35f }, false);
    addCover("SM_Prop_Barrier_01", { -2.5f, 0, -14.0f }, 0.0f, { 1.6f, 0.6f, 0.35f }, false);
    addCover("SM_Prop_Barrier_01", { 3.0f, 0, -12.0f }, 0.0f, { 1.6f, 0.6f, 0.35f }, false);
    addCover("SM_Prop_Barrier_01", { 8.5f, 0, -13.5f }, 0.0f, { 1.6f, 0.6f, 0.35f }, false);
    // The enemy's back wall and the range's side walls (no cover spots).
    const glm::vec3 concrete(0.55f, 0.56f, 0.6f);
    s.block({ 0, 1.5f, -24 }, { 10, 1.5f, 0.25f }, concrete);
    for (float x : { -16.0f, 16.0f }) {
        s.block({ x, 1.5f, 2.5f }, { 0.25f, 1.5f, 27.5f }, concrete);
        for (float z = -24.0f; z <= 30.0f; z += 1.5f) m_ai.addObstacle({ { x, 0, z }, 0.8f });
    }
}

void PlatoonModule::addCover(const char* model, const glm::vec3& pos, float yaw, const glm::vec3& half, bool friendlySide) {
    if (!m_scenery->place(model, pos, yaw, 1.0f, true, { "POLYGON_Prototype" }))
        m_scenery->block(pos + glm::vec3(0.0f, half.y, 0.0f), half, { 0.5f, 0.42f, 0.3f });
    // Walked around, and hidden behind: spots on the side away from the
    // danger (the enemy is at -Z for us, we are at +Z for them).
    const float r = std::max(half.x, half.z);
    m_ai.addObstacle({ pos, r + 0.1f });
    const float side = friendlySide ? 1.0f : -1.0f;
    const int spots = half.x > 1.0f ? 2 : 1;
    for (int i = 0; i < spots; ++i) {
        CoverSpot c;
        const float x = spots == 1 ? 0.0f : (i == 0 ? -0.8f : 0.8f);
        c.pos = pos + glm::vec3(x, 0.0f, side * (half.z + 0.75f));
        c.facing = glm::vec3(0.0f, 0.0f, -side);
        if (friendlySide) m_cover.push_back(c);
    }
}

void PlatoonModule::addSoldier(uint32_t id, const std::string& name, const glm::vec3& pos, bool enemy) {
    Soldier s;
    s.id = id;
    s.name = name;
    s.enemy = enemy;
    s.maxHp = s.hp = enemy ? 100.0f : 120.0f;
    s.pos = pos;
    s.yaw = enemy ? 0.0f : 180.0f;
    s.body = std::make_unique<command_kit::Humanoid>(*m_kit, *m_models, enemy ? glm::vec3(1.0f, 0.45f, 0.4f) : glm::vec3(0.35f, 0.6f, 1.7f));
    m_ai.addAgent(id, "soldier", pos, s.yaw);
    m_ai.setTeam(id, enemy ? 2 : 1);
    m_soldiers.push_back(std::move(s));
}

PlatoonModule::Soldier* PlatoonModule::soldier(uint32_t id) {
    for (Soldier& s : m_soldiers)
        if (s.id == id) return &s;
    return nullptr;
}

const PlatoonModule::Soldier* PlatoonModule::soldier(uint32_t id) const {
    for (const Soldier& s : m_soldiers)
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<uint32_t> PlatoonModule::living(bool enemy) const {
    std::vector<uint32_t> out;
    for (const Soldier& s : m_soldiers)
        if (s.enemy == enemy && !s.dead) out.push_back(s.id);
    return out;
}

// ---------------------------------------------------------------- orders

void PlatoonModule::give(kke::OrderKind kind, uint32_t target, const glm::vec3* point, bool queue) {
    if (m_selection.empty()) {
        m_hud->toast("Select soldiers first");
        return;
    }
    kke::Order o;
    o.kind = kind;
    o.units = m_selection.ids();
    o.target = target;
    o.formation = m_formation;
    o.spacing = 2.2f;
    o.queue = queue;
    if (point) {
        o.point = *point;
        o.hasPoint = true;
        // Face the way they're going (formation slots turn with it).
        glm::vec3 centre(0.0f);
        for (uint32_t u : o.units) centre += m_ai.agent(u) ? m_ai.agent(u)->position : glm::vec3(0.0f);
        centre /= float(o.units.size());
        if (glm::length(flat(*point - centre)) > 1.0f) {
            o.yawDegrees = kke::yawOf(flat(*point - centre));
            o.hasYaw = true;
        }
    }
    if (kind == kke::OrderKind::Regroup && !point) {
        // Around the first selected.
        o.point = m_ai.agent(o.units.front())->position;
        o.hasPoint = true;
        o.formation = kke::Formation::Circle;
    }
    std::string why;
    if (!m_board.issue(o, &why)) {
        m_hud->toast(why.empty() ? "Can't do that" : why);
        return;
    }
    if (o.hasPoint) {
        m_marker3 = o.point;
        m_markerLeft = 1.2f;
    }
    std::string who = m_selection.size() == 1 ? soldier(o.units.front())->name : std::to_string(m_selection.size()) + " soldiers";
    std::string what = kke::orderLabel(kind);
    if (target)
        if (const Soldier* t = soldier(target)) what += ": " + t->name;
    m_hud->toast(who + ", " + what, 1.4f);
}

void PlatoonModule::takeCover(const glm::vec3* near) {
    // Each selected soldier to the nearest free cover spot (near the
    // pointer when given), one soldier a spot: this layer's call, the AI
    // just holds where it's put.
    std::vector<uint32_t> units = m_selection.ids();
    if (units.empty()) {
        m_hud->toast("Select soldiers first");
        return;
    }
    for (CoverSpot& c : m_cover)
        if (std::find(units.begin(), units.end(), c.taken) != units.end()) c.taken = 0;
    int placed = 0;
    for (uint32_t u : units) {
        Soldier* s = soldier(u);
        if (!s) continue;
        const glm::vec3 from = near ? *near : s->pos;
        int best = -1;
        float bestD = 1e9f;
        for (size_t i = 0; i < m_cover.size(); ++i) {
            if (m_cover[i].taken) continue;
            const float d = glm::length(flat(m_cover[i].pos - from)) + 0.3f * glm::length(flat(m_cover[i].pos - s->pos));
            if (d < bestD) {
                bestD = d;
                best = int(i);
            }
        }
        if (best < 0) break;
        kke::Order o;
        o.kind = kke::OrderKind::Stay;
        o.units = { u };
        o.point = m_cover[size_t(best)].pos;
        o.hasPoint = true;
        if (m_board.issue(o)) {
            m_cover[size_t(best)].taken = u;
            s->coverSpot = best;
            ++placed;
        }
    }
    m_hud->toast(placed ? "Take cover!" : "No cover left", 1.4f);
}

// The controller's select: a tap selects what's under the ring (a click);
// held, a box grows around the ring and selects everyone in it when let
// go (the mouse's drag-box). With cmd.queue held it adds, as a drag does.
void PlatoonModule::padSelect(kke::InputMap& in, command_kit::CommandInput::Frame& f, float dt) {
    if (f.wheelOpen) {
        m_padSelectHeld = -1.0f;
        return;
    }
    if (in.pressed("rts.select")) m_padSelectHeld = 0.0f;
    if (m_padSelectHeld < 0.0f) return;
    const glm::vec2 c = f.pointer;
    const float grow = std::max(0.0f, m_padSelectHeld - 0.3f);
    const glm::vec2 half(std::min(40.0f + 420.0f * grow, 600.0f), std::min(30.0f + 300.0f * grow, 420.0f));
    if (in.held("rts.select")) {
        m_padSelectHeld += dt;
        if (grow > 0.0f) {
            f.dragging = true;
            f.boxA = c - half;
            f.boxB = c + half;
        }
        return;
    }
    // Let go.
    if (grow > 0.0f) {
        f.boxDone = true;
        f.boxA = c - half;
        f.boxB = c + half;
    } else {
        f.click = true;
        f.touch = false;
    }
    m_padSelectHeld = -1.0f;
}

// The settings panel (RmlUi, kke::DemoPanelModule, right side, folded):
// Start or F3 opens it, Esc too (with Quit). The same things the HUD bar
// does, reachable by controller row by row.
void PlatoonModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    auto& s = panel->section("Platoon");
    m_formationIndex = int(m_formation);
    s.choice("Formation", kke::DemoPanelModule::Ref<int>([this] {
                 m_formationIndex = int(m_formation);
                 return &m_formationIndex;
             }),
             std::vector<std::string>{ kke::formationName(kke::Formation::Line), kke::formationName(kke::Formation::Wedge),
                                       kke::formationName(kke::Formation::Column), kke::formationName(kke::Formation::Circle) },
             [this] { m_formation = kke::Formation(m_formationIndex); });
    s.button("Select everyone", [this] { selectAll(); });
    s.button("Hold position", [this] { give(kke::OrderKind::Stay); });
    s.button("Take cover", [this] { takeCover(); });
    s.button("Regroup", [this] { give(kke::OrderKind::Regroup); });
    s.hint("Groups: Ctrl+1-9 stores the selection, 1-9 brings it back.",
           "Groups: hold {rts.groups} and press the d-pad (up, right, down, left) for groups 1-4; hold {cmd.force} too to store.");
    s.note("Attack and Focus fire are orders at the ring: {cmd.context} (with {cmd.force} for focus fire) or the order wheel ({cmd.wheel}).");
    panel->setState(kke::DemoPanelModule::State::Collapsed);
}

void PlatoonModule::cycleFormation() {
    m_formation = kke::Formation((int(m_formation) + 1) % 4);
    m_hud->toast(std::string("Formation: ") + kke::formationName(m_formation), 1.2f);
}

void PlatoonModule::selectAll() {
    m_selection.set(living(false));
    m_hud->toast("Everyone", 0.8f);
}

void PlatoonModule::cycleSelection(int step) {
    const std::vector<uint32_t> mine = living(false);
    if (mine.empty()) return;
    size_t at = 0;
    if (m_selection.size() == 1) {
        auto it = std::find(mine.begin(), mine.end(), m_selection.ids().front());
        if (it != mine.end()) at = size_t((int(it - mine.begin()) + step + int(mine.size())) % int(mine.size()));
    }
    m_selection.select(mine[at]);
    m_focus = soldier(mine[at])->pos;
}

std::vector<kke::ScreenUnit> PlatoonModule::screenUnits() const {
    const kke::Camera& cam = m_app->camera();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    const glm::mat4 vp = kke::engineProjection(cam.fovDegrees, float(w) / float(std::max(h, 1)), cam.nearPlane, cam.farPlane) *
                         glm::lookAt(cam.position, cam.target, cam.up);
    std::vector<kke::ScreenUnit> out;
    for (const Soldier& s : m_soldiers) {
        if (s.dead) continue;
        const glm::vec4 c = vp * glm::vec4(s.pos + glm::vec3(0.0f, 0.9f, 0.0f), 1.0f);
        kke::ScreenUnit u;
        u.id = s.id;
        u.onScreen = c.w > 0.0f;
        if (u.onScreen) u.screen = { (c.x / c.w * 0.5f + 0.5f) * float(w), (c.y / c.w * 0.5f + 0.5f) * float(h) };
        out.push_back(u);
    }
    return out;
}

kke::PointerTarget PlatoonModule::pick(const glm::vec2& pointer) const {
    kke::PointerTarget t;
    // A soldier within a finger's width of the pointer.
    if (const uint32_t id = kke::unitNear(screenUnits(), pointer, 34.0f)) {
        const Soldier* s = soldier(id);
        t.thing = id;
        t.relation = s->enemy ? kke::Relation::Hostile : kke::Relation::Own;
        t.point = s->pos;
        return t;
    }
    const kke::Camera& cam = m_app->camera();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    const kke::Ray ray = kke::screenToRay(pointer, { float(w), float(h) }, glm::lookAt(cam.position, cam.target, cam.up),
                                          kke::engineProjection(cam.fovDegrees, float(w) / float(std::max(h, 1)), cam.nearPlane, cam.farPlane));
    if (float d = kke::rayPlaneY(ray, 0.0f); d > 0.0f) t.point = ray.at(d);
    else t.point = m_focus;
    return t;
}

void PlatoonModule::click(const command_kit::CommandInput::Frame& f) {
    const kke::PointerTarget t = pick(f.pointer);
    // A button armed a targeted order: this click is its target.
    if (m_armed != kke::OrderKind::None) {
        if (t.relation == kke::Relation::Hostile) give(m_armed, t.thing);
        else m_hud->toast("That's not an enemy", 1.0f);
        m_armed = kke::OrderKind::None;
        return;
    }
    if (t.relation == kke::Relation::Own) {
        if (f.queue || f.touch) m_selection.toggle(t.thing); // Shift+click, or tapping soldiers one by one
        else m_selection.select(t.thing);
        return;
    }
    // A finger has no right button: tapping elsewhere with soldiers selected orders them.
    if (f.touch && !m_selection.empty()) {
        giveContext(f.pointer, f.force, false);
        return;
    }
    if (!f.queue) m_selection.clear();
}

void PlatoonModule::giveContext(const glm::vec2& pointer, bool force, bool queue) {
    const kke::PointerTarget t = pick(pointer);
    kke::UnitAbilities can;
    can.attack = true;
    can.fetch = false;
    kke::PointerModifiers mods;
    mods.force = force;
    mods.queue = queue;
    const kke::Order o = kke::contextOrder(m_selection.ids(), t, can, mods);
    if (o.kind == kke::OrderKind::None) return;
    give(o.kind, o.target, o.hasPoint ? &o.point : nullptr, queue);
}

void PlatoonModule::giveWheel(int item, const glm::vec2& pointer) {
    const kke::PointerTarget t = pick(pointer);
    switch (item) {
    case WMove: give(kke::OrderKind::Move, 0, &t.point); break;
    case WAttack:
    case WFocus:
        if (t.relation == kke::Relation::Hostile) give(item == WAttack ? kke::OrderKind::Attack : kke::OrderKind::FocusFire, t.thing);
        else m_hud->toast("Point at an enemy", 1.0f);
        break;
    case WHold: give(kke::OrderKind::Stay, 0, &t.point); break;
    case WCover: takeCover(&t.point); break;
    case WRegroup: give(kke::OrderKind::Regroup, 0, &t.point); break;
    case WFollow:
        if (t.relation == kke::Relation::Own) {
            // Everyone else selected follows that soldier.
            m_selection.remove(t.thing);
            give(kke::OrderKind::Follow, t.thing);
            m_selection.add(t.thing);
        } else {
            m_hud->toast("Point at one of yours", 1.0f);
        }
        break;
    case WFormation: cycleFormation(); break;
    default: break;
    }
}

void PlatoonModule::pressButton(int button) {
    switch (button) {
    case BAll: selectAll(); break;
    case BHold: give(kke::OrderKind::Stay); break;
    case BCover: takeCover(); break;
    case BAttack:
    case BFocus:
        m_armed = button == BAttack ? kke::OrderKind::Attack : kke::OrderKind::FocusFire;
        m_hud->toast("Now tap an enemy", 1.5f);
        break;
    case BRegroup: give(kke::OrderKind::Regroup); break;
    case BFormation: cycleFormation(); break;
    default: break;
    }
}

// ---------------------------------------------------------------- the fight

bool PlatoonModule::clearShot(const glm::vec3& from, const glm::vec3& to) const {
    const glm::vec3 d = to - from;
    const float len = glm::length(d);
    if (len < 0.1f) return true;
    const kke::RigidWorld::RayHit hit = m_rigid->world().raycast(from, d / len, len - 0.3f);
    return !hit.hit || hit.point.y < 0.02f; // the ground under a low ray doesn't count
}

void PlatoonModule::handleEvents(const std::vector<kke::ai::AiEvent>& events) {
    for (const kke::ai::AiEvent& e : events) {
        if (e.kind != kke::ai::AiEvent::Kind::Attack) continue;
        Soldier* from = soldier(e.who);
        Soldier* at = soldier(e.other);
        if (from && at && !from->dead && !at->dead) shoot(*from, *at);
    }
}

void PlatoonModule::shoot(Soldier& from, Soldier& at) {
    from.aimAt = at.id;
    from.sinceShot = 0.0f;
    from.body->act("Pistol_Shoot", false, 1.3f, 0.05f, true);
    const glm::vec3 muzzle = from.pos + glm::vec3(0.0f, kEye, 0.0f) + glm::normalize(flat(at.pos - from.pos) + glm::vec3(0.001f, 0, 0)) * 0.5f;
    glm::vec3 aim = at.pos + glm::vec3(0.0f, kEye - 0.2f, 0.0f);
    // Crouched behind cover, only the head shows over it.
    const bool covered = at.coverSpot >= 0 && glm::length(flat(at.pos - m_cover[size_t(at.coverSpot)].pos)) < 0.8f &&
                         glm::dot(flat(from.pos - at.pos), m_cover[size_t(at.coverSpot)].facing) > 0.0f;
    if (covered) aim.y = 1.05f;
    float chance = from.enemy ? 0.38f : 0.45f;
    if (covered) chance *= 0.35f;
    if (!clearShot(muzzle, aim)) chance = 0.0f; // it hits the cover
    // Deterministic rolls: the same fight every run (replays, the self-play).
    m_nextRoll = m_nextRoll * 1664525u + 1013904223u;
    const float roll = float(m_nextRoll >> 8) / float(1u << 24);
    const bool hit = roll < chance;
    Tracer t;
    t.from = muzzle;
    t.to = hit ? aim : aim + glm::vec3(std::sin(roll * 40.0f), std::cos(roll * 31.0f) * 0.4f, 0.0f) * 0.8f;
    t.left = 0.09f;
    t.hit = hit;
    m_tracers.push_back(t);
    if (!hit) return;
    at.hp -= 20.0f;
    at.body->act("Hit_Chest", false, 1.4f, 0.05f, true);
    if (at.hp <= 0.0f) kill(at);
}

void PlatoonModule::kill(Soldier& s) {
    s.dead = true;
    s.hp = 0.0f;
    s.body->act("Death01", false, 1.0f, 0.1f, true);
    if (s.coverSpot >= 0) m_cover[size_t(s.coverSpot)].taken = 0;
    s.coverSpot = -1;
    m_ai.remove(s.id);
    m_selection.remove(s.id);
    m_board.forget(s.id);
    m_board.targetGone(s.id); // attack / focus fire on it: done
    kke::log::get(name())->info("{} is down", s.name);
    m_hud->toast(s.name + " is down", 1.5f);
}

// ---------------------------------------------------------------- the frame

void PlatoonModule::onEvent(const SDL_Event& e) {
    m_cmd.onEvent(e);
    if (e.type == SDL_EVENT_MOUSE_WHEEL) m_camDistance = std::clamp(m_camDistance * std::pow(0.9f, e.wheel.y), 10.0f, 50.0f);
}

void PlatoonModule::updateCamera(float dt) {
    kke::InputMap& in = m_input->map(0);
    const glm::vec2 pan = in.axis2("move");
    if (!m_cmd.wheelOpen()) {
        const glm::vec2 turn = in.axis2("look.rate");
        m_camYaw += turn.x * 90.0f * dt;
        m_camDistance = std::clamp(m_camDistance * (1.0f + turn.y * 1.2f * dt), 10.0f, 50.0f);
    }
    if (in.held("rts.left")) m_camYaw -= 90.0f * dt;
    if (in.held("rts.right")) m_camYaw += 90.0f * dt;
    const glm::vec3 fwd = kke::yawForward(m_camYaw);
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    m_focus += (fwd * pan.y + right * pan.x) * (m_camDistance * 0.9f * dt);
    m_focus.x = std::clamp(m_focus.x, -20.0f, 20.0f);
    m_focus.z = std::clamp(m_focus.z, -28.0f, 30.0f);
    const float p = glm::radians(m_camPitch);
    kke::Camera& cam = m_app->camera();
    cam.target = m_focus;
    cam.position = m_focus - fwd * (std::cos(p) * m_camDistance) + glm::vec3(0.0f, std::sin(p) * m_camDistance, 0.0f);
    cam.up = glm::vec3(0.0f, 1.0f, 0.0f);
}

void PlatoonModule::update(const kke::UpdateContext& ctx) {
    const float dt = std::min(ctx.dt, 0.05f);
    m_clock += dt;
    if (m_quitAfter > 0.0f && m_clock > m_quitAfter) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
    kke::InputMap& in = m_input->map(0);
    if (in.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());

    command_kit::CommandInput::Frame f = m_cmd.update(in, *m_app, false, dt);
    padSelect(in, f, dt);
    if (f.click) click(f);
    if (f.boxDone) {
        std::vector<kke::ScreenUnit> mine;
        for (const kke::ScreenUnit& u : screenUnits())
            if (!soldier(u.id)->enemy) mine.push_back(u);
        const std::vector<uint32_t> inside = kke::unitsInRect(mine, f.boxA, f.boxB);
        if (f.queue)
            for (uint32_t id : inside) m_selection.add(id);
        else if (!inside.empty())
            m_selection.set(inside);
    }
    if (f.context) {
        m_armed = kke::OrderKind::None;
        giveContext(f.pointer, f.force, f.queue);
    }
    if (f.wheelGiven >= 0) giveWheel(f.wheelGiven, f.wheelTarget);
    if (!f.wheelOpen) {
        // With RT held the d-pad is groups (above), not these orders.
        const bool groups = in.held("rts.groups");
        if (in.pressed("rts.all") && !groups) selectAll();
        if (in.pressed("rts.next") && !groups) cycleSelection(1);
        if (in.pressed("rts.prev") && !groups) cycleSelection(-1);
        if (in.pressed("rts.hold")) give(kke::OrderKind::Stay);
        if (in.pressed("rts.cover")) takeCover();
        if (in.pressed("rts.regroup") && !groups) give(kke::OrderKind::Regroup);
        if (in.pressed("rts.formation")) cycleFormation();
        const bool store = in.held("cmd.force"); // Ctrl or LT
        for (int g = 1; g <= 9; ++g)
            // The d-pad is bound to groups 1-4 too: only with RT held.
            if (in.pressed("rts.group" + std::to_string(g)) && (g > 4 || groups || !m_cmd.padActive())) {
                if (store) {
                    m_selection.storeGroup(g);
                    m_hud->toast("Group " + std::to_string(g) + " stored", 1.0f);
                } else if (!m_selection.recallGroup(g)) {
                    m_hud->toast("Group " + std::to_string(g) + " is empty", 1.0f);
                }
            }
    }
    if (m_demo) runDemo(dt);

    // The AI moves everyone (no bodies to push: the field is flat) and says who shoots.
    for (Soldier& s : m_soldiers) {
        if (s.dead) continue;
        const kke::UnitOrder* o = m_board.current(s.id);
        if (o && o->status == kke::UnitOrder::Status::Given) m_board.markRunning(s.id);
    }
    m_ai.update(dt);
    const std::vector<kke::ai::AiEvent> events = m_ai.takeEvents();
    m_bridge->handle(events);
    handleEvents(events);
    if (!m_announcedClear && living(true).empty()) {
        m_announcedClear = true;
        m_hud->toast("Area clear!", 3.0f);
        kke::log::get(name())->info("area clear: {} of 6 soldiers standing", living(false).size());
    }

    updateCamera(dt);
    updateBodies(dt);
    if (m_script) m_script->fireEvents();
    updateHud(dt);
}

void PlatoonModule::updateBodies(float dt) {
    for (Soldier& s : m_soldiers) {
        s.sinceShot += dt;
        if (s.dead) {
            s.deadFor += dt;
            s.body->update(s.pos, 180.0f - s.yaw, 0.0f, dt);
            if (s.deadFor > 6.0f) s.body->setVisible(false);
            continue;
        }
        const kke::ai::Agent* a = m_ai.agent(s.id);
        s.pos = a->position;
        const float speed = glm::length(flat(a->velocity));
        // Face the one it's shooting at; else where it goes, or what it watches.
        float face = a->yaw;
        const Soldier* target = s.sinceShot < 2.5f ? soldier(s.aimAt) : nullptr;
        if (target && !target->dead && speed < 0.5f) face = aiYaw(flat(target->pos - s.pos));
        else if (s.coverSpot >= 0 && speed < 0.3f) face = aiYaw(m_cover[size_t(s.coverSpot)].facing);
        s.yaw += angleDelta(s.yaw, face) * (1.0f - std::exp(-10.0f * dt));
        // Stance: running, crouched in cover, gun up after a fight, at ease.
        const bool inCover = s.coverSpot >= 0 && speed < 0.3f && glm::length(flat(s.pos - m_cover[size_t(s.coverSpot)].pos)) < 0.8f;
        const std::string& now = s.body->acting();
        const bool oneShot = now == "Pistol_Shoot" || now == "Hit_Chest";
        if (!oneShot || s.body->actionFinished()) {
            if (speed > 0.3f) s.body->act("");
            else if (inCover && s.sinceShot > 0.6f) s.body->act("Crouch_Idle_Loop", true, 1.0f, 0.25f);
            else if (s.sinceShot < 6.0f || a->focus) s.body->act("Pistol_Idle_Loop", true, 1.0f, 0.2f);
            else s.body->act("");
        }
        s.body->update(s.pos, 180.0f - s.yaw, speed, dt);
    }
    for (Tracer& t : m_tracers) t.left -= dt;
    m_tracers.erase(std::remove_if(m_tracers.begin(), m_tracers.end(), [](const Tracer& t) { return t.left <= 0.0f; }), m_tracers.end());
    m_markerLeft -= dt;
}

void PlatoonModule::updateHud(float dt) {
    const command_kit::CommandInput::Frame& f = m_cmd.frame();
    std::string sel = "Selected: none (click, drag or tap your soldiers)";
    if (m_selection.size() == 1) sel = "Selected: " + soldier(m_selection.ids().front())->name;
    else if (!m_selection.empty()) sel = "Selected: " + std::to_string(m_selection.size()) + " soldiers";
    std::string orderLine = "Order: none";
    if (!m_selection.empty()) {
        const kke::UnitOrder* o = m_board.current(m_selection.ids().front());
        orderLine = std::string("Order: ") + (o ? kke::orderLabel(o->kind) : "free to fight");
        if (o && o->target)
            if (const Soldier* t = soldier(o->target)) orderLine += " (" + t->name + ")";
    }
    int hp = 0;
    for (const Soldier& s : m_soldiers)
        if (!s.enemy && !s.dead) hp += int(s.hp);
    m_hud->setLines({ { sel, "#ffcf5c" },
                      { orderLine },
                      { std::string("Formation: ") + kke::formationName(m_formation) },
                      { "Squad: " + std::to_string(living(false).size()) + " of 6 standing, " + std::to_string(hp) + " health",
                        living(false).size() >= 4 ? "#6fe39a" : "#ff8a7a" },
                      { "Enemies left: " + std::to_string(living(true).size()), "#aab3cc" } });
    // Prompt text: {action} shows that button on the device in use.
    // Attack and Focus fire are armed by a tap or click, then given with
    // the next one; a controller gives them at the ring directly.
    const bool pad = m_cmd.padActive();
    m_hud->setButtons({ { "👥", "Everyone", "{rts.all}", false },
                        { "✋", "Hold", "{rts.hold}", false },
                        { "🛡", "Cover", "{rts.cover}", false },
                        { "🎯", "Attack", pad ? "{cmd.context}" : "{touch:tap}", m_armed == kke::OrderKind::Attack },
                        { "💥", "Focus fire", pad ? "{cmd.force}+{cmd.context}" : "{touch:tap}", m_armed == kke::OrderKind::FocusFire },
                        { "🔄", "Regroup", "{rts.regroup}", false },
                        { "🔷", kke::formationName(m_formation), "{rts.formation}", false } });
    m_hud->setHint(m_cmd.padActive()
                       ? "{move} pan · {look.rate} turn/zoom · {rts.select} select, hold: select an area · {cmd.context} order at the ring "
                         "({cmd.force} focus fire) · hold {cmd.wheel} order wheel · {rts.all}{rts.next}{rts.regroup} all, next, regroup · "
                         "hold {rts.groups} + d-pad: groups 1-4 ({cmd.force} too: store) · {panel.toggle} menu"
                       : "{mouse:left} click or drag: select · {cmd.context} order ({cmd.force} focus fire / hold there, {cmd.queue} queue) · "
                         "hold {cmd.wheel} order wheel · {move} pan, {rts.left}{rts.right} turn, {camera.zoom} zoom · {rts.group1}{rts.group2}{rts.group3} groups "
                         "(Ctrl+1-9 store) · {touch:tap} select, tap again: order");
    m_hud->update(f, dt);
}

void PlatoonModule::render(const kke::RenderContext& ctx) {
    m_scenery->render(ctx);
    for (Soldier& s : m_soldiers) {
        s.body->render(ctx);
        if (s.dead) continue;
        const glm::mat4 at = glm::translate(glm::mat4(1.0f), s.pos);
        if (m_selection.contains(s.id)) m_ring->draw(ctx, glm::scale(at * glm::translate(glm::mat4(1.0f), { 0, 0.02f, 0 }), { 0.9f, 0.03f, 0.9f }));
        // The one your selection is told to shoot at.
        bool targeted = false;
        for (uint32_t u : m_selection.ids())
            if (const kke::UnitOrder* o = m_board.current(u); o && o->target == s.id) targeted = true;
        if (targeted) m_enemyRing->draw(ctx, glm::scale(at * glm::translate(glm::mat4(1.0f), { 0, 0.02f, 0 }), { 1.1f, 0.03f, 1.1f }));
        // Health: a bar over the head.
        const glm::mat4 bar = at * glm::translate(glm::mat4(1.0f), { 0, 2.15f, 0 });
        m_hpBack->draw(ctx, glm::scale(bar, { 0.8f, 0.07f, 0.07f }));
        const float frac = std::clamp(s.hp / s.maxHp, 0.0f, 1.0f);
        m_hpFront->draw(ctx, glm::scale(bar * glm::translate(glm::mat4(1.0f), { -0.4f * (1.0f - frac), 0.0f, 0.0f }), { 0.8f * frac + 0.01f, 0.09f, 0.09f }));
    }
    for (const Tracer& t : m_tracers) m_tracer->draw(ctx, beam(t.from, t.to, 0.035f));
    if (m_markerLeft > 0.0f)
        m_marker->draw(ctx, glm::scale(glm::translate(glm::mat4(1.0f), m_marker3 + glm::vec3(0, 0.03f, 0)), { 0.5f, 0.04f, 0.5f }));
}

void PlatoonModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_scenery->renderShadow(ctx);
    for (Soldier& s : m_soldiers) s.body->renderShadow(ctx);
}

// ---------------------------------------------------------------- KKE_PLATOON_DEMO

void PlatoonModule::runDemo(float dt) {
    m_demoTimer += dt;
    auto next = [&](const std::string& what) {
        kke::log::get(name())->info("demo: {} ({:.1f} s)", what, m_demoTimer);
        ++m_demoStep;
        m_demoTimer = 0.0f;
    };
    auto allDone = [&](kke::OrderKind k) {
        for (uint32_t u : living(false))
            if (m_board.currentKind(u) == k) return false;
        return true;
    };
    auto nearestEnemy = [&](const glm::vec3& from) {
        uint32_t best = 0;
        float d = 1e9f;
        for (uint32_t e : living(true))
            if (glm::length(soldier(e)->pos - from) < d) {
                d = glm::length(soldier(e)->pos - from);
                best = e;
            }
        return best;
    };
    // The camera follows the squad.
    glm::vec3 centre(0.0f);
    const std::vector<uint32_t> mine = living(false);
    for (uint32_t u : mine) centre += soldier(u)->pos;
    if (!mine.empty()) m_focus += (centre / float(mine.size()) - m_focus) * (1.0f - std::exp(-1.5f * dt));

    switch (m_demoStep) {
    case 0:
        if (m_demoTimer > 1.5f) {
            selectAll();
            const glm::vec3 there(0.0f, 0.0f, 13.0f);
            give(kke::OrderKind::Move, 0, &there);
            next("everyone: go there, in a wedge");
        }
        break;
    case 1:
        if (allDone(kke::OrderKind::Move) || m_demoTimer > 20.0f) {
            float spread = 1e9f;
            for (uint32_t a : mine)
                for (uint32_t b : mine)
                    if (a < b) spread = std::min(spread, glm::length(soldier(a)->pos - soldier(b)->pos));
            next("arrived in formation, closest two " + std::to_string(spread).substr(0, 4) + " m apart");
            selectAll();
            takeCover();
            next("everyone: take cover");
        }
        break;
    case 2: break;
    case 3:
        if (m_demoTimer > 9.0f) {
            int inCover = 0;
            for (uint32_t u : living(false))
                if (soldier(u)->coverSpot >= 0 && glm::length(flat(soldier(u)->pos - m_cover[size_t(soldier(u)->coverSpot)].pos)) < 0.8f) ++inCover;
            next(std::to_string(inCover) + " of " + std::to_string(living(false).size()) + " behind cover");
            // Individual order: Alpha 1 alone attacks the nearest enemy.
            m_selection.select(1);
            if (const uint32_t e = nearestEnemy(soldier(1) ? soldier(1)->pos : glm::vec3(0.0f))) give(kke::OrderKind::Attack, e);
        }
        break;
    case 4:
        if (m_demoTimer > 6.0f) {
            // Everyone: focus fire on one enemy.
            selectAll();
            const uint32_t e = nearestEnemy({ 0, 0, 5 });
            m_stepTarget = e;
            if (e) give(kke::OrderKind::FocusFire, e);
            next("Alpha 1 went alone; now everyone: focus fire on " + (e ? soldier(e)->name : std::string("nobody")));
        }
        break;
    case 5:
        if (!m_stepTarget || soldier(m_stepTarget)->dead || m_demoTimer > 40.0f) {
            next(m_stepTarget && soldier(m_stepTarget)->dead ? soldier(m_stepTarget)->name + " down under focus fire" : "FOCUS FIRE TIMED OUT");
        }
        break;
    case 6: {
        // Then one enemy at a time until the field is clear.
        if (living(true).empty() || living(false).empty() || m_demoTimer > 90.0f) {
            next(living(true).empty() ? "area clear" : living(false).empty() ? "the squad was lost" : "ASSAULT TIMED OUT");
            break;
        }
        bool busy = false;
        for (uint32_t u : living(false))
            if (m_board.currentKind(u) == kke::OrderKind::Attack || m_board.currentKind(u) == kke::OrderKind::FocusFire) busy = true;
        if (!busy) {
            selectAll();
            if (const uint32_t e = nearestEnemy(centre / float(std::max<size_t>(1, mine.size())))) give(kke::OrderKind::FocusFire, e);
        }
        break;
    }
    case 7:
        selectAll();
        give(kke::OrderKind::Regroup);
        next("regroup");
        break;
    case 8:
        if (allDone(kke::OrderKind::Regroup) || m_demoTimer > 15.0f) {
            kke::log::get(name())->info("demo: result: {} of 6 soldiers standing, {} of 5 enemies left", living(false).size(), living(true).size());
            next("done");
        }
        break;
    case 9:
        if (m_demoTimer > 2.0f) {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            ++m_demoStep;
        }
        break;
    default: break;
    }
}

} // namespace platoon
