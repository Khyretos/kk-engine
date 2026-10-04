#include "PetModule.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Picking.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/GameShellModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <typeindex>

namespace pet_companion {

namespace {

constexpr float kGarden = 10.0f;     // the fence: x and z from -10 to 10
constexpr float kGround = 45.0f;     // the field around it
constexpr float kGateZ0 = 0.0f;      // the gate in the east fence, z 0 .. 2.5
constexpr const char* kSaveFile = "pet_companion_save.json";
constexpr int kRight = 1;            // the hand that holds and pats

glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }

float angleDelta(float from, float to) { return std::fmod(to - from + 540.0f, 360.0f) - 180.0f; }

// AiWorld's yaw (0 = +Z, 90 = +X) of a direction.
float aiYaw(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, d.z)); }
glm::vec3 aiDir(float yaw) { return glm::vec3(std::sin(glm::radians(yaw)), 0.0f, std::cos(glm::radians(yaw))); }

// The wheel, clockwise from the top.
const kke::OrderKind kWheel[] = { kke::OrderKind::Follow, kke::OrderKind::Fetch, kke::OrderKind::Move, kke::OrderKind::Drop,
                                  kke::OrderKind::Free,   kke::OrderKind::Pet,   kke::OrderKind::Stay, kke::OrderKind::Sit };
const char* kWheelIcons[] = { "👋", "🎾", "📍", "👇", "🌼", "❤", "✋", "🐕" };
const char* kWheelLabels[] = { "Come", "Fetch", "Go there", "Drop it", "At ease", "Pet", "Stay", "Sit" };

// Quiet corners for its business, away from the bowls.
const glm::vec3 kToiletSpots[] = { { -8.4f, 0.0f, 8.4f }, { 8.4f, 0.0f, 8.4f }, { 8.4f, 0.0f, -8.4f }, { -8.4f, 0.0f, 3.0f } };

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::string(v) != "0";
}

// A box (centre, half extents, yaw as Scenery placed it) as triangles for the navmesh.
void addBox(std::vector<glm::vec3>& v, std::vector<uint32_t>& idx, const command_kit::Scenery::Collider& c) {
    const glm::quat q = glm::angleAxis(glm::radians(-c.yaw), glm::vec3(0, 1, 0));
    const uint32_t b = uint32_t(v.size());
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 s((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
        v.push_back(c.center + q * (s * c.half));
    }
    const uint32_t f[] = { 0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5 };
    for (uint32_t i : f) idx.push_back(b + i);
}

} // namespace

PetModule::PetModule() = default;
PetModule::~PetModule() = default;

std::vector<kke::ModuleDependency> PetModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the characters, the toys and the garden" },
             { std::type_index(typeid(kke::InputModule)), true, "moving, looking and giving orders" },
             { std::type_index(typeid(kke::ModelModule)), true, "the dog, the person and the garden" },
             { std::type_index(typeid(kke::ScriptModule)), false, "order.* in Lua (scripts/pet.lua)" } };
}

void PetModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_input = app.getModule<kke::InputModule>();
    m_models = app.getModule<kke::ModelModule>();

    kke::InputMap& in = m_input->map(0);
    kke::InputModule::defineCharacterActions(in);
    command_kit::CommandInput::defineActions(in);
    // BUG-065: the d-pad down is Sit here and Select is the pause menu, so
    // audio.ping is Q only; orders don't queue in the garden, so Left Shift
    // is only sprint.
    in.clearBindings("audio.ping");
    in.clearBindings("cmd.queue");
    using IM = kke::InputModule;
    auto quick = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        in.defineAction({ id, label, "Orders", "game" });
        in.addBinding(IM::bind(id, IM::key(key)));
        if (pad != SDL_GAMEPAD_BUTTON_INVALID) in.addBinding(IM::bind(id, IM::pad(pad)));
    };
    quick("pet.come", "Come", SDL_SCANCODE_1, SDL_GAMEPAD_BUTTON_DPAD_UP);
    quick("pet.sit", "Sit", SDL_SCANCODE_2, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    quick("pet.stay", "Stay", SDL_SCANCODE_3, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    quick("pet.fetch", "Fetch", SDL_SCANCODE_4, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    quick("pet.drop", "Drop it", SDL_SCANCODE_5, SDL_GAMEPAD_BUTTON_WEST);
    in.addBinding(IM::bind("audio.ping", IM::key(SDL_SCANCODE_Q)));
    // Esc is the pause menu (kke::GameShellModule), which frees the mouse too.
    quick("pet.mouse", "Free the mouse / look with it", SDL_SCANCODE_TAB, SDL_GAMEPAD_BUTTON_INVALID);
    quick("panels", "Developer panels", SDL_SCANCODE_F1, SDL_GAMEPAD_BUTTON_INVALID);
    // Touch: a tap on the world points (orders), so looking is a right stick, not a drag.
    kke::TouchLayoutOptions touch;
    touch.look = kke::TouchLayoutOptions::Look::Stick;
    m_input->setTouchLayout(touch);
    m_input->commitDefaults();
    app.window().setQuitOnEscape(false);

    // Only the packs this game uses: a big library isn't read folder by folder at start.
    m_scenery = std::make_unique<command_kit::Scenery>(app, *m_models, *m_rigid,
                                                       std::vector<std::string>{ "POLYGON_Town", "POLYGON_Dogs", "Farm Animals Animated  by Quaternius" });
    buildGarden();

    kke::RigidWorld& w = m_rigid->world();
    kke::RigidWorld::CharacterDesc pd;
    pd.position = glm::vec3(0.0f, 0.05f, 5.0f);
    m_playerChar = w.addCharacter(pd);
    m_loco = std::make_unique<kke::Locomotion>(w, m_playerChar);
    m_kit = std::make_unique<command_kit::HumanoidKit>(app);
    m_kit->load(*m_models);
    m_body = std::make_unique<command_kit::Humanoid>(*m_kit, *m_models, glm::vec3(0.55f, 0.75f, 1.0f));
    // Feet on the ground and hands on what they hold (kke::CharacterIk).
    m_body->enableIk([this](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) { return groundAt(from, hit, normal); });
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.pitch = -16.0f;
    m_rig.settings.armLength = 4.2f;

    kke::RigidWorld::CharacterDesc dd;
    dd.radius = 0.22f;
    dd.height = 0.6f;
    dd.mass = 20.0f;
    dd.stepUp = 0.2f;
    dd.position = glm::vec3(1.6f, 0.05f, 3.5f);
    m_dogChar = w.addCharacter(dd);
    m_dog = std::make_unique<DogBody>(app, *m_models, *m_scenery);
    // The picker lists the pets this machine has the art for.
    for (size_t i = 0; i < breeds().size(); ++i)
        if (m_dog->available(int(i))) {
            m_breedIds.push_back(int(i));
            m_breedNames.push_back(breeds()[i].label);
        }

    addToy(Toy::Kind::Ball, { -2.5f, 0.3f, 1.0f });
    addToy(Toy::Kind::Football, { 3.5f, 0.3f, -2.0f });
    addToy(Toy::Kind::Frisbee, { -1.0f, 0.3f, -4.0f });
    addToy(Toy::Kind::Bone, { 4.0f, 0.3f, 4.0f });
    addToy(Toy::Kind::Duck, { -5.0f, 0.3f, 3.0f });
    addToy(Toy::Kind::Stick, { 1.5f, 0.3f, 6.5f });

    m_board.setPositionFn([this](uint32_t t) { return positionOf(t); });
    m_board.listen({ [](uint32_t, const kke::UnitOrder& o) { kke::log::get("Pet")->info("order: {}", kke::orderName(o.kind)); },
                     [this](uint32_t unit, const kke::UnitOrder& o, bool ok) {
                         kke::log::get("Pet")->info("{} {}", kke::orderName(o.kind), ok ? "done" : "failed");
                         // There: it waits on that spot for the next order.
                         if (o.kind == kke::OrderKind::Move && ok) {
                             kke::Order stay;
                             stay.kind = kke::OrderKind::Stay;
                             stay.units = { unit };
                             stay.point = o.point;
                             stay.hasPoint = true;
                             stay.issuer = o.issuer;
                             m_board.issue(stay);
                         }
                     } });

    // Which pet: KKE_PET_BREED, else the one you had, else the first there is.
    loadSave();
    if (const char* b = std::getenv("KKE_PET_BREED"); b && breedIndex(b) >= 0) m_breed = breedIndex(b);
    if (const char* c = std::getenv("KKE_PET_COAT")) m_coat = std::clamp(std::atoi(c), 0, 2);
    if (std::find(m_breedIds.begin(), m_breedIds.end(), m_breed) == m_breedIds.end())
        m_breed = m_breedIds.empty() ? breedIndex("pug") : m_breedIds.front();
    m_dog->load(m_breed, m_coat);
    setUpAi();
    choosePet(m_breed, m_coat);
    buildNavMesh();
    if (auto* scripts = app.getModule<kke::ScriptModule>()) m_script = std::make_unique<kke::OrderScript>(scripts->vm(), m_board, *this);

    m_hud = std::make_unique<command_kit::CommandHud>(app);
    m_hud->build("YOUR DOG");
    m_hud->onButton = [this](int b) { pressButton(b); };
    std::vector<command_kit::CommandHud::WheelItem> wheel;
    for (size_t i = 0; i < std::size(kWheel); ++i) wheel.push_back({ kWheelIcons[i], kWheelLabels[i] });
    m_hud->setWheel(std::move(wheel));
    m_cmd.overUi = [this](const glm::vec2& p) { return m_hud->overButtons(p); };

    // The shared menus: where to start, the pet picker, the course.
    if (auto* shell = app.getModule<kke::GameShellModule>()) {
        auto toCourse = [this] {
            m_loco->teleport(m_course.start() + glm::vec3(0.0f, 0.05f, 0.0f));
            m_loco->setFacing(m_course.startFacing());
            m_rig.yaw = kke::yawOf(m_course.startFacing());
            m_rigid->world().teleportCharacter(m_dogChar, m_course.start() + glm::vec3(-0.6f, 0.05f, 1.2f));
            m_course.reset();
            m_over = {};
            m_hud->toast("Agility: run past jump 1 or point at it", 3.0f);
        };
        auto toGarden = [this] {
            m_loco->teleport({ 0.0f, 0.05f, 5.0f });
            m_rigid->world().teleportCharacter(m_dogChar, { 1.6f, 0.05f, 3.5f });
            m_course.reset();
            m_over = {};
        };
        shell->addMode({ "garden", "The garden", "Look after your dog: food, water, toys and a pat" });
        shell->addMode({ "agility", "Agility", "Run the course next to the garden together" });
        shell->onPlay = [toCourse](const std::string& mode) {
            if (mode == "agility") toCourse();
        };
        shell->pauseRows()
            .choice("Pet", &m_breedChoice, m_breedNames.empty() ? std::vector<std::string>{ "Dog" } : m_breedNames,
                    [this] {
                        if (m_breedChoice >= 0 && size_t(m_breedChoice) < m_breedIds.size()) choosePet(m_breedIds[size_t(m_breedChoice)], m_coat);
                        if (m_care.dead()) adopt(); // a new pet of the breed you picked
                    })
            .choice("Coat", &m_coat, { "1", "2", "3" }, [this] { choosePet(m_breed, m_coat); })
            .showIf([this] { return !breeds()[size_t(m_breed)].material.empty(); });
        shell->addPauseItem("Adopt a new pet", [this] { adopt(); }, [this] { return m_care.dead(); });
        shell->addPauseItem("Run the agility course", toCourse);
        shell->addPauseItem("Back to the garden", toGarden);
    }

    m_demo = envOn("KKE_PET_DEMO");
    // Self-play from a later step (a quicker check of one part).
    if (const char* st = std::getenv("KKE_PET_DEMO_STEP")) m_demoStep = std::max(0, std::atoi(st));
    if (const char* q = std::getenv("KKE_PET_QUIT")) m_quitAfter = float(std::atof(q));
    if (m_demo && m_quitAfter < 0.0f) m_quitAfter = 150.0f;
    m_scenery->logUsed(name());
    kke::log::get(name())->info("the garden is ready: {} ({} pets to pick from), its brain the AI core's \"dog\"",
                                m_dog->isBlock() ? "no dog model found, the dog is a block" : m_dog->label(), m_breedIds.size());
}

void PetModule::shutdown() {
    writeSave();
    // Everything holding GPU or UI resources goes before the modules that own them.
    m_script.reset();
    m_bridge.reset();
    m_hud.reset();
    m_toyBlock.reset();
    m_poopBlock.reset();
    m_dog.reset();
    m_body.reset();
    m_kit.reset();
    m_scenery.reset();
}

// ---------------------------------------------------------------- the garden

void PetModule::buildGarden() {
    command_kit::Scenery& s = *m_scenery;
    s.ground(kGround, { 0.32f, 0.5f, 0.26f });
    const std::vector<std::string> town{ "POLYGON_Town" }, dogs{ "POLYGON_Dogs" };
    // A fenced garden 20 m across. A fence piece is 2.5 m long from its
    // end; the gate is the piece at z 0 .. 2.5 in the east side, standing open.
    bool fence = false;
    for (int i = 0; i < 8; ++i) {
        const float a = -kGarden + 2.5f * float(i);
        fence |= s.place("SM_Env_Fence_Wood_Straight_01", { a, 0.0f, -kGarden }, 0.0f, 1.0f, true, town) != 0;
        s.place("SM_Env_Fence_Wood_Straight_01", { a, 0.0f, kGarden }, 0.0f, 1.0f, true, town);
        s.place("SM_Env_Fence_Wood_Straight_01", { -kGarden, 0.0f, a }, 90.0f, 1.0f, true, town);
        if (std::abs(a - kGateZ0) > 0.1f) s.place("SM_Env_Fence_Wood_Straight_01", { kGarden, 0.0f, a }, 90.0f, 1.0f, true, town);
    }
    for (float x : { -kGarden, kGarden })
        for (float z : { -kGarden, kGarden }) s.place("SM_Env_Fence_Wood_Post_01", { x, 0.0f, z }, 0.0f, 1.0f, false, town);
    // The gate swung open into the field.
    s.place("SM_Env_Fence_Wood_Gate_01", { kGarden, 0.0f, kGateZ0 }, 0.0f, 1.0f, true, town);
    if (!fence) {
        const glm::vec3 wood(0.45f, 0.32f, 0.2f);
        s.block({ 0, 0.5f, -kGarden }, { kGarden, 0.5f, 0.05f }, wood);
        s.block({ 0, 0.5f, kGarden }, { kGarden, 0.5f, 0.05f }, wood);
        s.block({ -kGarden, 0.5f, 0 }, { 0.05f, 0.5f, kGarden }, wood);
        s.block({ kGarden, 0.5f, -5.0f }, { 0.05f, 0.5f, 5.0f }, wood);
        s.block({ kGarden, 0.5f, 6.25f }, { 0.05f, 0.5f, 3.75f }, wood);
    }
    // Its corner: house, bed, bowls and the food bag.
    if (!s.place("SM_Prop_House_01", { -7.0f, 0.0f, -8.4f }, 0.0f, 1.0f, true, dogs) &&
        !s.place("SM_Prop_Doghouse_01", { -7.0f, 0.0f, -8.4f }, 180.0f, 1.0f, true, town))
        s.block({ -7.0f, 0.6f, -8.4f }, { 0.7f, 0.6f, 0.6f }, { 0.7f, 0.25f, 0.2f });
    m_bed = { -4.8f, 0.0f, -8.6f };
    if (!s.place("SM_Prop_Bed_01", m_bed, 0.0f, 1.0f, false, dogs)) s.block(m_bed + glm::vec3(0, 0.08f, 0), { 0.55f, 0.08f, 0.4f }, { 0.55f, 0.3f, 0.3f }, false);
    addBowl(false, { -3.0f, 0.0f, -9.1f });
    addBowl(true, { -2.3f, 0.0f, -9.1f });
    if (!s.place("SM_Prop_FoodBag_01", { -1.5f, 0.0f, -9.5f }, 10.0f, 1.0f, true, dogs))
        s.block({ -1.5f, 0.28f, -9.5f }, { 0.18f, 0.28f, 0.09f }, { 0.8f, 0.6f, 0.3f });
    // Trees, a hedge along the west fence, a barrel and grass.
    if (!s.place("SM_Env_Tree_01", { 6.5f, 0.0f, -6.5f }, 0.0f, 1.0f, true, town)) s.block({ 6.5f, 2, -6.5f }, { 0.3f, 2, 0.3f }, { 0.4f, 0.3f, 0.2f });
    s.place("SM_Env_Tree_02", { -6.5f, 0.0f, 6.5f }, 40.0f, 1.0f, true, town);
    s.place("SM_Env_Hedge_01", { -9.1f, 0.0f, 1.5f }, 90.0f, 1.0f, true, town);
    s.place("SM_Prop_Barrel_01", { 8.6f, 0.0f, -4.5f }, 0.0f, 1.0f, true, town);
    for (int i = 0; i < 18; ++i) {
        const float a = float(i) * 2.39996f, r = 3.0f + float(i % 6) * 1.1f;
        s.place("SM_Env_Grass_01", { std::cos(a) * r, 0.0f, std::sin(a) * r }, float(i) * 37.0f, 1.0f, false, town);
    }
    // The agility field east of the garden.
    m_course.build(s, *m_models);
}

void PetModule::buildNavMesh() {
    // The field and every collider in it: the dog paths round the fence through the gate.
    std::vector<glm::vec3> v;
    std::vector<uint32_t> idx;
    const float h = m_scenery->groundHalf();
    v.insert(v.end(), { { -h, 0, -h }, { -h, 0, h }, { h, 0, h }, { h, 0, -h } });
    idx.insert(idx.end(), { 0, 1, 2, 0, 2, 3 });
    for (const command_kit::Scenery::Collider& c : m_scenery->colliders()) {
        if (c.center.y + c.half.y < 0.05f) continue; // the ground itself
        addBox(v, idx, c);
    }
    kke::ai::NavMeshSettings s;
    s.cellSize = 0.2f;
    s.cellHeight = 0.1f;
    s.agentRadius = 0.3f;
    s.agentHeight = 0.8f;
    s.agentMaxClimb = 0.2f;
    std::string error;
    const uint64_t t0 = SDL_GetTicksNS();
    if (m_nav.build(v, idx, s, &error)) {
        m_ai.setNavMesh(&m_nav);
        kke::log::get(name())->info("navmesh: {} polygons in {} ms", m_nav.polygonCount(), (SDL_GetTicksNS() - t0) / 1000000);
    } else {
        kke::log::get(name())->warn("navmesh failed ({}): the dog walks straight at things", error);
    }
}

void PetModule::addBowl(bool water, const glm::vec3& pos) {
    Bowl b;
    b.water = water;
    b.pos = pos;
    const std::vector<std::string> dogs{ "POLYGON_Dogs" };
    b.bowl = m_scenery->place(water ? "SM_Prop_Bowl_Blue_01" : "SM_Prop_Bowl_Red_01", pos, 0.0f, 1.0f, false, dogs);
    if (b.bowl) {
        b.content = m_scenery->place(water ? "SM_Prop_Bowl_Water_01" : "SM_Prop_Bowl_Food_01", pos, 0.0f, 1.0f, false, dogs);
    } else {
        m_scenery->block(pos + glm::vec3(0, 0.04f, 0), { 0.11f, 0.04f, 0.11f }, water ? glm::vec3(0.2f, 0.4f, 0.9f) : glm::vec3(0.8f, 0.2f, 0.2f), false);
    }
    m_bowls.push_back(b);
}

void PetModule::addToy(Toy::Kind kind, const glm::vec3& pos) {
    Toy t;
    t.id = kFirstToy + uint32_t(m_toys.size());
    t.kind = kind;
    t.pos = pos;
    struct Spec { const char* label; const char* asset; const char* pack; float size, grip, mass; glm::vec3 color; };
    static const Spec specs[] = {
        { "tennis ball", "SM_Prop_Ball_01", "POLYGON_Dogs", 0.075f, 0.0375f, 0.06f, { 0.85f, 0.95f, 0.2f } },
        { "football", "SM_Item_Ball_Soccer_01", "POLYGON_Town", 0.22f, 0.11f, 0.4f, { 0.95f, 0.95f, 0.95f } },
        { "frisbee", "SM_Prop_Disc_Red_01", "POLYGON_Dogs", 0.27f, 0.03f, 0.17f, { 0.9f, 0.15f, 0.15f } },
        { "rubber bone", "SM_Prop_Toy_RubberBone_01", "POLYGON_Dogs", 0.22f, 0.03f, 0.1f, { 0.3f, 0.6f, 0.95f } },
        { "rubber duck", "SM_Prop_Toy_Duck_01", "POLYGON_Dogs", 0.2f, 0.04f, 0.1f, { 1.0f, 0.85f, 0.1f } },
        { "stick", "SM_Prop_Stick_01", "POLYGON_Dogs", 0.5f, 0.025f, 0.2f, { 0.5f, 0.35f, 0.2f } },
    };
    const Spec& s = specs[size_t(kind)];
    t.label = s.label;
    t.radius = s.grip;
    t.color = s.color;
    const bool round = kind == Toy::Kind::Ball || kind == Toy::Kind::Football;
    glm::vec3 size = round ? glm::vec3(s.size) : glm::vec3(s.size, s.size * 0.25f, s.size * 0.4f);
    if (const kke::ModelModule::ModelId m = m_scenery->model(s.asset, { s.pack })) {
        const kke::ModelData* d = m_models->model(m);
        const glm::vec3 ext = d->boundsMax - d->boundsMin;
        // Sized to the toy it is, whatever units the file is in.
        t.modelScale = s.size / std::max({ ext.x, ext.y, ext.z, 1e-4f });
        t.modelCentre = (d->boundsMin + d->boundsMax) * 0.5f;
        size = glm::max(ext * t.modelScale, glm::vec3(0.02f));
        t.model = m_models->spawn(m);
        m_models->setOverlayEnabled(t.model, false);
    } else if (!m_toyBlock) {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        command_kit::appendBox(glm::vec3(0.0f), glm::vec3(0.5f), glm::vec3(1.0f), v, idx);
        m_toyBlock = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_toyBlock->upload(v, idx);
    }
    t.half = size * 0.5f;
    if (round) t.radius = std::max(t.half.x, std::max(t.half.y, t.half.z));
    kke::RigidWorld::BodyDesc d;
    d.shape = round ? kke::RigidWorld::Shape::Sphere : kke::RigidWorld::Shape::Box;
    d.radius = t.radius;
    d.halfExtents = t.half;
    d.position = pos;
    d.mass = s.mass;
    d.restitution = round ? 0.6f : 0.2f;
    d.friction = 0.6f;
    t.body = m_rigid->world().add(d);
    m_toys.push_back(t);
}

PetModule::Toy* PetModule::toy(uint32_t id) {
    for (Toy& t : m_toys)
        if (t.id == id) return &t;
    return nullptr;
}

glm::vec3 PetModule::positionOf(uint32_t thing) const {
    const kke::RigidWorld& w = m_rigid->world();
    if (thing == kPlayer) return w.characterPosition(m_playerChar);
    if (thing == kDog) return w.characterPosition(m_dogChar);
    for (const Toy& t : m_toys)
        if (t.id == thing) return t.pos;
    return glm::vec3(NAN);
}

bool PetModule::exists(uint32_t thing) const {
    return thing == kPlayer || thing == kDog || std::any_of(m_toys.begin(), m_toys.end(), [&](const Toy& t) { return t.id == thing; });
}

bool PetModule::groundAt(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) const {
    // Down from a little above the point; small loose things (toys) don't count.
    const kke::RigidWorld::RayHit h = m_rigid->world().raycast(from + glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 3.0f);
    const bool toyHit = h.hit && std::any_of(m_toys.begin(), m_toys.end(), [&](const Toy& t) { return t.body == h.body; });
    if (!h.hit || toyHit || h.point.y > from.y + 0.3f) {
        hit = glm::vec3(from.x, 0.0f, from.z);
        normal = glm::vec3(0.0f, 1.0f, 0.0f);
        return true;
    }
    hit = h.point;
    normal = h.normal;
    return true;
}

// ---------------------------------------------------------------- the pet

void PetModule::choosePet(int breed, int coat) {
    m_breed = breed;
    m_coat = std::clamp(coat, 0, 2);
    if (m_dog->breed() != breed || m_dog->coat() != m_coat) m_dog->load(breed, m_coat);
    const auto it = std::find(m_breedIds.begin(), m_breedIds.end(), breed);
    m_breedChoice = it == m_breedIds.end() ? 0 : int(it - m_breedIds.begin());
    // Its body: a capsule its size (the head is above the shoulders).
    m_rigid->world().setCharacterHeight(m_dogChar, std::clamp(m_dog->height() * 0.8f, 0.4f, 1.0f));
    updateSpecies();
    kke::log::get(name())->info("your pet: {} (coat {}), {:.2f} m tall", m_dog->label(), m_coat + 1, m_dog->height());
}

void PetModule::loadSave() {
    std::ifstream f(kSaveFile);
    if (!f) return;
    nlohmann::json j = nlohmann::json::parse(f, nullptr, false);
    if (!j.is_object()) {
        kke::log::get(name())->warn("{} isn't JSON: starting afresh", kSaveFile);
        return;
    }
    if (const int b = breedIndex(j.value("pet", std::string())); b >= 0) m_breed = b;
    m_coat = std::clamp(j.value("coat", 0), 0, 2);
    m_care.load(j.value("care", nlohmann::json::object()));
    m_course.setBest(j.value("agilityBest", -1.0f));
    m_lastHunger = j.value("hunger", 0.3f);
    m_lastThirst = j.value("thirst", 0.2f);
    if (const auto bowls = j.value("bowls", nlohmann::json::array()); bowls.is_array())
        for (size_t i = 0; i < bowls.size() && i < m_bowls.size(); ++i) m_bowls[i].level = std::clamp(bowls[i].get<float>(), 0.0f, 1.0f);
}

void PetModule::writeSave() const {
    if (m_demo || !m_dog) return; // self-play doesn't touch your pet
    nlohmann::json j;
    j["pet"] = breeds()[size_t(m_breed)].id;
    j["coat"] = m_coat;
    j["care"] = m_care.save();
    j["hunger"] = m_ai.need(kDog, "hunger");
    j["thirst"] = m_ai.need(kDog, "thirst");
    j["agilityBest"] = m_course.best();
    nlohmann::json bowls = nlohmann::json::array();
    for (const Bowl& b : m_bowls) bowls.push_back(b.level);
    j["bowls"] = bowls;
    std::ofstream f(kSaveFile);
    if (f) f << j.dump(2) << "\n";
}

// ---------------------------------------------------------------- orders

void PetModule::give(kke::OrderKind kind, uint32_t target, const glm::vec3* point) {
    if (m_care.dead()) {
        m_hud->toast("Your dog has passed away: adopt a new pet in the pause menu", 2.5f);
        return;
    }
    kke::Order o;
    o.kind = kind;
    o.units = { kDog };
    o.issuer = kPlayer;
    o.target = target;
    if (kind == kke::OrderKind::Follow && !target) o.target = kPlayer;
    if (kind == kke::OrderKind::Pet) o.target = kPlayer;
    if (kind == kke::OrderKind::Fetch && !o.target) {
        // The nearest toy nobody is holding.
        const glm::vec3 dog = positionOf(kDog);
        float best = 1e9f;
        for (const Toy& t : m_toys)
            if (t.held != Toy::Held::Player && glm::length(t.pos - dog) < best) {
                best = glm::length(t.pos - dog);
                o.target = t.id;
            }
        if (!o.target) {
            m_hud->toast("You have the toy: throw it first");
            return;
        }
    }
    if (point) {
        o.point = *point;
        o.hasPoint = true;
    }
    // A real order wins over what it was up to (on its way to an obstacle, its business).
    if (!m_over.crossing) m_over = {};
    if (m_toilet.phase == Toilet::Phase::Going) m_toilet.phase = Toilet::Phase::None;
    m_toBed = false;
    std::string why;
    if (!m_board.issue(o, &why)) {
        kke::log::get(name())->info("order not given: {}", why);
        return;
    }
    playerSay(std::string(kke::orderLabel(kind)) + "!");
}

kke::PointerTarget PetModule::pick(const glm::vec2& pointer, int* obstacle) const {
    const kke::Camera& cam = m_app->camera();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    const glm::mat4 view = glm::lookAt(cam.position, cam.target, cam.up);
    const glm::mat4 proj = kke::engineProjection(cam.fovDegrees, float(w) / float(std::max(h, 1)), cam.nearPlane, cam.farPlane);
    const kke::Ray ray = kke::screenToRay(pointer, { float(w), float(h) }, view, proj);
    kke::PointerTarget t;
    float best = 1e30f;
    int hitObstacle = -1;
    auto test = [&](uint32_t id, kke::Relation rel, const glm::vec3& mn, const glm::vec3& mx, int obs = -1) {
        const float d = kke::rayAabb(ray, mn, mx);
        if (d >= 0.0f && d < best) {
            best = d;
            t.thing = id;
            t.relation = rel;
            t.point = ray.at(d);
            hitObstacle = obs;
        }
    };
    // Generous boxes: a toy is small, and a finger is big.
    for (const Toy& toy : m_toys)
        if (toy.held == Toy::Held::No) test(toy.id, kke::Relation::Item, toy.pos - glm::vec3(0.35f), toy.pos + glm::vec3(0.35f));
    const glm::vec3 dog = positionOf(kDog), me = positionOf(kPlayer);
    const float r = std::max(0.45f, m_dog->length() * 0.6f);
    test(kDog, kke::Relation::Own, dog + glm::vec3(-r, 0.0f, -r), dog + glm::vec3(r, m_dog->height() + 0.1f, r));
    test(kPlayer, kke::Relation::Self, me + glm::vec3(-0.3f, 0.0f, -0.3f), me + glm::vec3(0.3f, 1.8f, 0.3f));
    // The agility obstacles.
    const std::vector<Obstacle>& obs = m_course.obstacles();
    for (size_t i = 0; i < obs.size(); ++i) {
        const glm::vec3 ext = glm::abs(obs[i].dir) * (obs[i].length * 0.5f) + glm::abs(glm::vec3(obs[i].dir.z, 0.0f, obs[i].dir.x)) * 0.8f;
        test(0, kke::Relation::Ground, obs[i].centre - glm::vec3(ext.x, 0.0f, ext.z), obs[i].centre + glm::vec3(ext.x, 1.4f, ext.z), int(i));
    }
    if (obstacle) *obstacle = hitObstacle;
    if (t.thing || hitObstacle >= 0) return t;
    const kke::RigidWorld::RayHit hit = m_rigid->world().raycast(ray.origin, ray.direction, 200.0f);
    if (hit.hit) t.point = hit.point;
    else if (float d = kke::rayPlaneY(ray, 0.0f); d > 0.0f) t.point = ray.at(d);
    else t.point = positionOf(kDog);
    return t;
}

void PetModule::giveContext(const glm::vec2& pointer, bool force) {
    int obstacle = -1;
    const kke::PointerTarget t = pick(pointer, &obstacle);
    // An obstacle: over it.
    if (obstacle >= 0) {
        sendOver(obstacle);
        return;
    }
    // Pointing at the dog itself: a pat.
    if (t.thing == kDog) {
        pet();
        return;
    }
    kke::UnitAbilities dog;
    dog.attack = false;
    dog.fetch = true;
    kke::PointerModifiers mods;
    mods.force = force;
    const kke::Order o = kke::contextOrder({ kDog }, t, dog, mods, kPlayer);
    give(o.kind, o.target, o.hasPoint ? &o.point : nullptr);
}

void PetModule::giveWheel(int item, const glm::vec2& pointer) {
    if (item < 0 || item >= int(std::size(kWheel))) return;
    const kke::OrderKind k = kWheel[item];
    const kke::PointerTarget t = pick(pointer);
    switch (k) {
    case kke::OrderKind::Fetch: give(k, t.relation == kke::Relation::Item ? t.thing : 0); break;
    case kke::OrderKind::Move: give(k, 0, &t.point); break;
    case kke::OrderKind::Pet: pet(); break;
    default: give(k); break;
    }
}

void PetModule::pressButton(int button) {
    switch (button) {
    case 0: give(kke::OrderKind::Follow); break;
    case 1: give(kke::OrderKind::Sit); break;
    case 2: give(kke::OrderKind::Stay); break;
    case 3: give(kke::OrderKind::Fetch); break;
    case 4: give(kke::OrderKind::Drop); break;
    case 5: interact(); break;
    case 6: throwToy(); break;
    default: break;
    }
}

void PetModule::pet() { give(kke::OrderKind::Pet); }

void PetModule::adopt() {
    m_care.adopt();
    m_mourned = false;
    m_ai.setEnabled(kDog, true);
    m_ai.setNeed(kDog, "hunger", 0.3f);
    m_ai.setNeed(kDog, "thirst", 0.2f);
    m_ai.setNeed(kDog, "tiredness", 0.1f);
    for (Bowl& b : m_bowls) {
        b.level = 1.0f;
        if (!b.place) b.place = m_ai.addPlace(b.water ? "water" : "dogfood", b.pos, 0.85f);
    }
    m_rigid->world().teleportCharacter(m_dogChar, positionOf(kPlayer) + aiDir(aiYaw(m_loco->facing())) * 1.5f + glm::vec3(0.0f, 0.05f, 0.0f));
    updateSpecies();
    m_hud->toast("Welcome home, " + m_dog->label() + "!", 3.0f);
    kke::log::get(name())->info("a new pet: {}", m_dog->label());
}

void PetModule::throwToy() {
    if (m_busy != Busy::None) return;
    for (Toy& t : m_toys) {
        if (t.held != Toy::Held::Player) continue;
        // The arm throws; the toy leaves the palm part way through (updateHands).
        m_busy = Busy::Throw;
        m_busyTime = 0.0f;
        m_busyToy = t.id;
        m_body->act("Spell_Simple_Shoot", false, 1.4f, 0.08f, true);
        return;
    }
    // Not holding one: crouch for the nearest within reach.
    const glm::vec3 me = positionOf(kPlayer);
    Toy* nearest = nullptr;
    float best = 1.8f;
    for (Toy& t : m_toys)
        if (t.held == Toy::Held::No && glm::length(flat(t.pos - me)) < best) {
            best = glm::length(flat(t.pos - me));
            nearest = &t;
        }
    if (!nearest) {
        m_hud->toast("No toy near you");
        return;
    }
    m_busy = Busy::PickUp;
    m_busyTime = 0.0f;
    m_busyToy = nearest->id;
}

// What E / Y does where you stand: clean up, fill a bowl, or pat the dog.
std::string PetModule::interactLabel() const {
    const glm::vec3 me = positionOf(kPlayer);
    for (const Poop& p : m_poops)
        if (glm::length(flat(p.pos - me)) < 1.6f) return "Clean up";
    for (const Bowl& b : m_bowls)
        if (glm::length(flat(b.pos - me)) < 1.4f && b.level < 0.9f) return b.water ? "Water" : "Feed";
    return "Pet";
}

void PetModule::interact() {
    if (m_busy != Busy::None) return;
    const glm::vec3 me = positionOf(kPlayer);
    for (size_t i = 0; i < m_poops.size(); ++i)
        if (glm::length(flat(m_poops[i].pos - me)) < 1.6f) {
            m_busy = Busy::Scoop;
            m_busyTime = 0.0f;
            m_busyIndex = int(i);
            m_busyAt = m_poops[i].pos;
            return;
        }
    for (size_t i = 0; i < m_bowls.size(); ++i)
        if (glm::length(flat(m_bowls[i].pos - me)) < 1.4f && m_bowls[i].level < 0.9f) {
            m_busy = Busy::Fill;
            m_busyTime = 0.0f;
            m_busyIndex = int(i);
            m_busyAt = m_bowls[i].pos;
            return;
        }
    pet();
}

void PetModule::grab(Toy& t) {
    if (t.body != kke::RigidWorld::kNoBody) m_rigid->world().remove(t.body);
    t.body = kke::RigidWorld::kNoBody;
    t.held = Toy::Held::Dog;
    // A toy in a mouth or a hand isn't a thing on the lawn: the AI stops seeing it.
    m_ai.setEnabled(t.id, false);
}

void PetModule::release(Toy& t, const glm::vec3& velocity) {
    if (t.body == kke::RigidWorld::kNoBody) {
        const bool round = t.kind == Toy::Kind::Ball || t.kind == Toy::Kind::Football;
        kke::RigidWorld::BodyDesc d;
        d.shape = round ? kke::RigidWorld::Shape::Sphere : kke::RigidWorld::Shape::Box;
        d.radius = t.radius;
        d.halfExtents = t.half;
        d.position = t.pos;
        d.rotation = t.rot;
        d.velocity = velocity;
        d.mass = t.kind == Toy::Kind::Football ? 0.4f : 0.1f;
        d.restitution = round ? 0.6f : 0.2f;
        d.friction = 0.6f;
        t.body = m_rigid->world().add(d);
    }
    t.held = Toy::Held::No;
    m_ai.setTransform(t.id, t.pos, glm::vec3(0.0f), 0.0f);
    m_ai.setEnabled(t.id, true);
}

void PetModule::playerSay(const std::string& text) { m_hud->toast(text, 1.2f); }

// ---------------------------------------------------------------- the dog: the AI core decides
//
// The dog is an agent of the AI core's built-in "dog" species; you, the
// toys, the bowls and the garden are what it perceives. Orders go board
// -> bridge -> AiWorld::order; with none, what you're doing becomes a
// soft AI order (never a command: the board stays empty). This part only
// moves the Jolt body the way the AI wants and shows how the dog feels.

void PetModule::setUpAi() {
    // A toy: something to see and fetch, with no life of its own.
    kke::ai::Species toys;
    toys.id = "toy";
    toys.label = "Toy";
    toys.radius = 0.1f;
    toys.scent = 0.0f;
    toys.needs.clear();
    toys.eats.clear();
    toys.drinks.clear();
    m_ai.defineSpecies(toys);
    updateSpecies();

    m_ai.addActor(kPlayer, "farmer", positionOf(kPlayer));
    m_ai.addAgent(kDog, "dog", positionOf(kDog));
    m_ai.setNeed(kDog, "hunger", m_lastHunger);
    m_ai.setNeed(kDog, "thirst", m_lastThirst);
    if (kke::ai::Agent* a = m_ai.agent(kDog)) a->home = glm::vec3(0.0f); // the middle of the garden
    for (const Toy& t : m_toys) m_ai.addActor(t.id, "toy", t.pos);
    // Its bowls (while there is something in them) and its bed.
    for (Bowl& b : m_bowls)
        if (b.level > 0.02f) b.place = m_ai.addPlace(b.water ? "water" : "dogfood", b.pos, 0.85f);
    m_bedPlace = m_ai.addPlace("bed", m_bed, 0.8f);

    m_bridge = std::make_unique<kke::AiOrderBridge>(m_board, m_ai);
    m_bridge->followDistance = 1.6f;
    m_bridge->pickUp = [this](uint32_t, uint32_t thing) {
        Toy* t = toy(thing);
        if (!t || t->held != Toy::Held::No) return false; // you picked it up first
        grab(*t);
        kke::log::get(name())->info("got the {}", t->label);
        return true;
    };
    m_bridge->deliver = [this](uint32_t, uint32_t thing, uint32_t) {
        if (Toy* t = toy(thing)) release(*t, glm::vec3(0.0f));
        m_care.praised(0.15f);
        m_joyTime = 1.2f;
        m_hud->toast("Good dog!");
    };
    m_bridge->drop = [this](uint32_t unit) {
        if (Toy* t = toy(m_bridge->carrying(unit))) release(*t, glm::vec3(0.0f));
    };
    // It came for the pat: you kneel, it sits, and the order ends when the pat does.
    m_bridge->petStart = [this](uint32_t, uint32_t) {
        m_petTime = 3.2f;
        m_busy = Busy::Pet;
        m_busyTime = 0.0f;
    };
}

void PetModule::updateSpecies() {
    // The built-in dog with this breed's pace, slower when it is ill.
    kke::ai::Species dog = *m_ai.species("dog");
    dog.label = m_dog->label();
    const float pace = m_care.sick() ? 0.6f : 1.0f;
    dog.walkSpeed = m_dog->walkSpeed() * pace;
    dog.runSpeed = m_dog->runSpeed() * pace;
    dog.radius = 0.3f;
    dog.homeRadius = 7.5f;
    dog.eats = { "dogfood" };
    dog.drinks = { "water" };
    // A day in a few minutes: hungry in about four, thirsty in three.
    dog.needs = { { "hunger", 1.0f / 220.0f, 0.3f }, { "thirst", 1.0f / 170.0f, 0.2f }, { "tiredness", 1.0f / 420.0f, 0.1f } };
    dog.actions.clear(); // rebuilt from the changes
    m_ai.defineSpecies(dog);
}

DogAct PetModule::chooseAct() const {
    if (m_care.dead()) return DogAct::Sleep;
    if (m_over.crossing) return m_overPose.act;
    if (m_joyTime > 0.0f) return DogAct::Joy;
    if (m_petTime > 0.0f) return m_petTime > 2.6f ? DogAct::Sit : DogAct::Wag;
    if (m_toilet.phase == Toilet::Phase::Doing) return m_toilet.poop ? DogAct::Poop : DogAct::Pee;
    if (m_board.currentKind(kDog) == kke::OrderKind::Sit) return DogAct::Sit;
    const kke::ai::Agent* a = m_ai.agent(kDog);
    const float speed = glm::length(flat(m_rigid->world().characterVelocity(m_dogChar)));
    if (!a || speed > 0.3f) return DogAct::Move;
    if (a->anim == "eat") return DogAct::Eat;
    if (a->anim == "drink") return DogAct::Drink;
    if (a->anim == "rest") return DogAct::Sleep;
    if (a->anim == "sniff") return DogAct::Sniff;
    if (a->anim == "attack") return DogAct::Bark;
    if (a->anim == "alert") return DogAct::Wag;
    const kke::OrderKind now = m_board.currentKind(kDog);
    if (now == kke::OrderKind::None || now == kke::OrderKind::Free) {
        // How it feels shows when it has nothing to do.
        if (m_care.sick()) return DogAct::Cower;
        const bool emptyBowl = std::any_of(m_bowls.begin(), m_bowls.end(), [](const Bowl& b) { return !b.water && b.level < 0.05f; });
        if (m_care.needs().hunger > 0.7f && emptyBowl && glm::length(flat(positionOf(kPlayer) - positionOf(kDog))) < 3.0f) return DogAct::Beg;
        if (m_ai.actionName(kDog) == "wander" && std::fmod(m_clock, 23.0f) < 2.5f) return DogAct::Sniff;
    }
    return DogAct::Move;
}

void PetModule::updateDog(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    if (m_care.dead()) {
        // It lies where it was; nothing more for its brain to do.
        if (!m_mourned) {
            m_mourned = true;
            if (Toy* t = toy(m_bridge->carrying(kDog))) release(*t, glm::vec3(0.0f));
            if (m_board.current(kDog)) m_board.complete(kDog, false);
            if (m_over.crossing) w.setCharacterKinematic(m_dogChar, false);
            m_over = {};
            m_toilet.phase = Toilet::Phase::None;
            m_petTime = m_joyTime = 0.0f;
            m_ai.clearOrder(kDog);
            m_ai.setEnabled(kDog, false);
            m_hud->toast(m_dog->label() + " has passed away. Adopt a new pet in the pause menu.", 8.0f);
            kke::log::get(name())->info("the pet died of neglect");
        }
        w.setCharacterInput(m_dogChar, {});
        m_turnRate = 0.0f;
        m_hasLook = false;
        return;
    }
    const glm::vec3 dog = positionOf(kDog), me = positionOf(kPlayer);
    const glm::vec3 myVel = flat(w.characterVelocity(m_playerChar));
    const glm::vec3 dogVel = flat(w.characterVelocity(m_dogChar));
    m_ai.setTransform(kPlayer, me, myVel, aiYaw(m_loco->facing()));
    m_ai.setTransform(kDog, dog, dogVel, m_dogYaw);
    for (const Toy& t : m_toys)
        if (t.held == Toy::Held::No) m_ai.setTransform(t.id, t.pos, glm::vec3(0.0f), 0.0f);

    // A good companion fetches a toy you throw without being told.
    kke::OrderKind now = m_board.currentKind(kDog);
    const bool busy = m_over.obstacle >= 0 || m_toilet.phase != Toilet::Phase::None;
    if (!busy && (now == kke::OrderKind::None || now == kke::OrderKind::Free || now == kke::OrderKind::Follow)) {
        for (const Toy& t : m_toys)
            if (t.held == Toy::Held::No && t.thrownAgo < 1.0f && t.thrownAgo > 0.2f) {
                give(kke::OrderKind::Fetch, t.id);
                kke::log::get(name())->info("the dog goes after the {} on its own", t.label);
                break;
            }
    }
    const kke::UnitOrder* o = m_board.current(kDog);
    if (o && o->status == kke::UnitOrder::Status::Given) m_board.markRunning(kDog);
    now = m_board.currentKind(kDog);

    // Tired with nothing to do: to its bed first.
    const bool free = now == kke::OrderKind::None || now == kke::OrderKind::Free;
    if (free && !busy && m_ai.actionName(kDog) == "rest" && glm::length(flat(dog - m_bed)) > 0.9f && !m_toBed) {
        kke::ai::Order bed;
        bed.kind = kke::ai::Order::Kind::MoveTo;
        bed.position = m_bed;
        bed.distance = 0.4f;
        m_ai.order(kDog, bed);
        m_toBed = true;
    }
    if (m_toBed && (glm::length(flat(dog - m_bed)) < 0.7f || !free)) {
        if (free) m_ai.clearOrder(kDog);
        m_toBed = false;
    }

    // No command: it reads what you're doing (kke::IntentReader) and plays along.
    if (free && !busy && !m_toBed) {
        const kke::Intent& in = m_intent.intent();
        Reflex want{ kke::Intent::Kind::Idle, 0, true };
        kke::ai::Order soft;
        if (in.kind == kke::Intent::Kind::Approaching && in.thing == kDog) {
            soft.kind = kke::ai::Order::Kind::Hold; // you're coming over: it waits for a pat
            soft.position = dog;
        } else if (in.kind == kke::Intent::Kind::LookingAt && in.thing != kDog && exists(in.thing)) {
            soft.kind = kke::ai::Order::Kind::Follow; // what are you looking at?
            soft.target = in.thing;
            soft.distance = 1.0f;
        } else if (in.kind == kke::Intent::Kind::Walking || in.kind == kke::Intent::Kind::Running) {
            soft.kind = kke::ai::Order::Kind::Follow; // off somewhere: it trots along
            soft.target = kPlayer;
            soft.distance = 1.8f;
            soft.run = in.kind == kke::Intent::Kind::Running;
        }
        // Hungry, thirsty or tired, it sees to that first (the AI's own wants).
        if (m_care.needs().hunger > 0.6f || m_care.needs().thirst > 0.6f || m_care.needs().tiredness > 0.8f)
            soft.kind = kke::ai::Order::Kind::None;
        if (soft.kind != kke::ai::Order::Kind::None) {
            want.kind = in.kind;
            want.thing = in.thing;
        }
        if (!m_reflex.on || want.kind != m_reflex.kind || want.thing != m_reflex.thing) {
            if (soft.kind == kke::ai::Order::Kind::None) m_ai.clearOrder(kDog); // its own life: sniff, eat, rest, watch you
            else m_ai.order(kDog, soft);
            m_reflex = want;
        }
    } else {
        m_reflex.on = false;
    }

    m_ai.update(dt);
    m_bridge->handle(m_ai.takeEvents());
    now = m_board.currentKind(kDog);

    // The pat.
    if (now == kke::OrderKind::Pet && m_petTime > 0.0f) {
        m_petTime -= dt;
        m_care.petted(dt);
        if (m_petTime <= 0.0f) {
            m_board.complete(kDog, true);
            m_joyTime = 1.2f;
            m_hud->toast("❤");
        }
    } else if (now != kke::OrderKind::Pet) {
        m_petTime = 0.0f;
    }

    // The body goes where the AI wants, except while it sits, eats, naps, is petted or jumps for joy.
    const kke::ai::Agent* a = m_ai.agent(kDog);
    if (m_joyTime > 0.0f) m_joyTime -= dt;
    const DogAct act = chooseAct();
    glm::vec3 want = flat(a->desiredVelocity);
    const bool still = act == DogAct::Sit || act == DogAct::Wag || act == DogAct::Joy || act == DogAct::Sleep || act == DogAct::Poop ||
                       act == DogAct::Pee || (act == DogAct::Eat && glm::length(want) < 0.5f) || (act == DogAct::Drink && glm::length(want) < 0.5f);
    if (still || m_over.crossing) want = glm::vec3(0.0f);
    if (!m_over.crossing) {
        kke::RigidWorld::CharacterInput ci;
        ci.move = want;
        w.setCharacterInput(m_dogChar, ci);
    }

    // Face where it runs; standing, what it attends to, else you.
    float face = m_dogYaw;
    glm::vec3 look = me + glm::vec3(0.0f, 1.2f, 0.0f);
    if (a->hasLookAt) look = a->lookAt;
    else if (a->focus && m_ai.agent(a->focus)) look = m_ai.agent(a->focus)->position;
    else if (m_reflex.on && m_reflex.thing && m_reflex.thing != kDog) look = positionOf(m_reflex.thing);
    else if (now == kke::OrderKind::Fetch && o && !m_bridge->carrying(kDog)) look = positionOf(o->target);
    if (m_over.crossing) face = aiYaw(m_overPose.forward);
    else if (glm::length(dogVel) > 0.4f) face = aiYaw(dogVel);
    else if (glm::length(flat(look - dog)) > 0.1f && act != DogAct::Sleep && act != DogAct::Poop && act != DogAct::Pee) face = aiYaw(flat(look - dog));
    const float before = m_dogYaw;
    m_dogYaw += angleDelta(m_dogYaw, face) * (1.0f - std::exp(-(m_over.crossing ? 14.0f : 8.0f) * dt));
    m_dogYaw = std::fmod(m_dogYaw + 540.0f, 360.0f) - 180.0f;
    m_turnRate = dt > 0.0f ? angleDelta(before, m_dogYaw) / dt : 0.0f;
    m_lookTarget = look;
    m_hasLook = true;
}

void PetModule::updateCare(float dt) {
    const glm::vec3 dog = positionOf(kDog);
    const kke::ai::Agent* a = m_ai.agent(kDog);
    // Eating and drinking empty the bowls.
    for (Bowl& b : m_bowls) {
        const bool at = glm::length(flat(dog - b.pos)) < 1.1f;
        if (at && a && a->anim == (b.water ? "drink" : "eat") && b.level > 0.0f) {
            const float took = std::min(b.level, dt / 15.0f * 1.25f); // a full bowl is most of a meal
            b.level -= took;
            if (b.water) m_care.drank(took);
            else m_care.ate(took);
            if (b.level <= 0.01f && b.place) {
                b.level = 0.0f;
                m_ai.removePlace(b.place);
                b.place = 0;
                kke::log::get(name())->info("the {} bowl is empty", b.water ? "water" : "food");
            }
        }
        // The food or water in it sinks as it goes.
        if (b.content) {
            m_models->setVisible(b.content, b.level > 0.03f);
            m_models->setTransform(b.content, glm::translate(glm::mat4(1.0f), b.pos + glm::vec3(0.0f, (b.level - 1.0f) * 0.045f, 0.0f)));
        }
    }
    float oldest = 0.0f;
    for (Poop& p : m_poops) {
        p.age += dt;
        oldest = std::max(oldest, p.age);
    }
    Care::Needs n;
    n.hunger = std::max(0.0f, m_ai.need(kDog, "hunger"));
    n.thirst = std::max(0.0f, m_ai.need(kDog, "thirst"));
    n.tiredness = std::max(0.0f, m_ai.need(kDog, "tiredness"));
    Care::World world;
    world.poops = int(m_poops.size());
    world.oldestPoop = oldest;
    world.playerDistance = glm::length(flat(positionOf(kPlayer) - dog));
    world.playing = m_petTime > 0.0f || m_over.obstacle >= 0 || m_board.currentKind(kDog) == kke::OrderKind::Fetch;
    const bool wasSick = m_care.sick(), wasDying = m_care.dying() > 0.0f;
    m_care.update(n, world, dt);
    if (!wasDying && m_care.dying() > 0.0f) m_hud->toast("Your dog is dying: fill its food and water bowls now!", 4.0f);
    if (m_care.sick() != wasSick) {
        updateSpecies();
        m_hud->toast(m_care.sick() ? "Your dog isn't well: food, water, a clean garden" : "Your dog is feeling better", 3.0f);
    }
    m_saveTimer += dt;
    if (m_saveTimer > 30.0f) {
        m_saveTimer = 0.0f;
        writeSave();
    }
}

void PetModule::updateToilet(float dt) {
    if (m_care.dead()) return;
    const kke::OrderKind now = m_board.currentKind(kDog);
    const bool free = now == kke::OrderKind::None || now == kke::OrderKind::Free;
    const glm::vec3 dog = positionOf(kDog);
    switch (m_toilet.phase) {
    case Toilet::Phase::None:
        if (!free || m_over.obstacle >= 0 || m_petTime > 0.0f || m_joyTime > 0.0f) break;
        if (!m_care.needsToPoop() && !m_care.needsToPee()) break;
        {
            // The quiet corner furthest from you and its bowls.
            m_toilet.poop = m_care.needsToPoop();
            float best = -1.0f;
            for (const glm::vec3& s : kToiletSpots) {
                const float score = glm::length(flat(s - positionOf(kPlayer))) + glm::length(flat(s - m_bowls.front().pos));
                if (score > best) {
                    best = score;
                    m_toilet.spot = s;
                }
            }
            kke::ai::Order go;
            go.kind = kke::ai::Order::Kind::MoveTo;
            go.position = m_toilet.spot;
            go.distance = 0.5f;
            m_ai.order(kDog, go);
            m_toilet.phase = Toilet::Phase::Going;
            m_toilet.time = 0.0f;
            m_toBed = false;
        }
        break;
    case Toilet::Phase::Going:
        m_toilet.time += dt;
        if (!free) {
            m_toilet.phase = Toilet::Phase::None;
            break;
        }
        if (glm::length(flat(dog - m_toilet.spot)) < 0.8f || m_toilet.time > 25.0f) {
            m_ai.clearOrder(kDog);
            m_toilet.phase = Toilet::Phase::Doing;
            m_toilet.time = 0.0f;
        }
        break;
    case Toilet::Phase::Doing:
        m_toilet.time += dt;
        if ((m_toilet.time > 0.5f && m_dog->actDone()) || m_toilet.time > 6.0f) {
            if (m_toilet.poop) {
                Poop p;
                p.pos = flat(dog) - aiDir(m_dogYaw) * (m_dog->length() * 0.5f);
                if (const kke::ModelModule::ModelId m = m_scenery->model("SM_Prop_Poop_01", { "POLYGON_Dogs" })) {
                    p.model = m_models->spawn(m, glm::rotate(glm::translate(glm::mat4(1.0f), p.pos), m_clock, glm::vec3(0, 1, 0)));
                    m_models->setOverlayEnabled(p.model, false);
                } else if (!m_poopBlock) {
                    std::vector<kke::Vertex> v;
                    std::vector<uint32_t> idx;
                    command_kit::appendBox({ 0.0f, 0.04f, 0.0f }, { 0.07f, 0.04f, 0.07f }, { 0.35f, 0.22f, 0.1f }, v, idx);
                    m_poopBlock = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
                    m_poopBlock->upload(v, idx);
                }
                m_poops.push_back(p);
                m_care.pooped();
                m_hud->toast("Your dog did a poo: clean it up ({interact} next to it)", 3.0f);
            } else {
                m_care.peed();
            }
            m_toilet.phase = Toilet::Phase::None;
        }
        break;
    }
}

void PetModule::sendOver(int obstacle) {
    if (m_care.dead()) return;
    const std::vector<Obstacle>& obs = m_course.obstacles();
    if (obstacle < 0 || obstacle >= int(obs.size()) || m_over.crossing) return;
    if (m_course.running() && obstacle != m_course.next()) {
        m_course.refused();
        m_hud->toast("Not that one: a fault (+5 s)", 2.0f);
        return;
    }
    if (m_course.finished()) m_course.reset();
    // Drop what it carries, forget the board's order: over it goes.
    if (Toy* t = toy(m_bridge->carrying(kDog))) release(*t, glm::vec3(0.0f));
    if (m_board.current(kDog)) m_board.complete(kDog, true);
    m_toilet.phase = Toilet::Phase::None;
    m_toBed = false;
    m_over = { obstacle, false, 0.0f, 12.0f };
    kke::ai::Order go;
    go.kind = kke::ai::Order::Kind::MoveTo;
    go.position = obs[size_t(obstacle)].entry();
    go.distance = 0.3f;
    go.run = true;
    m_ai.order(kDog, go);
    playerSay("Over!");
}

void PetModule::updateAgility(float dt) {
    m_course.update(dt);
    const std::vector<Obstacle>& obs = m_course.obstacles();
    const glm::vec3 me = positionOf(kPlayer), dog = positionOf(kDog);
    kke::RigidWorld& w = m_rigid->world();
    // Back in the garden in the middle of a run: it's off.
    if (m_course.running() && !m_course.finished() && me.x < kGarden - 0.5f) {
        m_course.reset();
        m_hud->toast("Agility run stopped", 2.0f);
    }
    // You run past the next obstacle: the dog takes it.
    const kke::OrderKind now = m_board.currentKind(kDog);
    const bool follows = now == kke::OrderKind::None || now == kke::OrderKind::Free || now == kke::OrderKind::Follow;
    if (m_over.obstacle < 0 && follows && m_course.next() < int(obs.size()) && me.x > kGarden + 0.5f) {
        const Obstacle& o = obs[size_t(m_course.next())];
        if (glm::length(flat(me - o.centre)) < o.length * 0.5f + 3.0f) sendOver(m_course.next());
    }
    if (m_over.obstacle < 0) return;
    const Obstacle& o = obs[size_t(m_over.obstacle)];
    if (!m_over.crossing) {
        m_over.giveUp -= dt;
        // At the start of it, facing the right way: over it goes (kinematic along the path).
        if (glm::length(flat(dog - o.entry())) < 0.55f) {
            m_ai.clearOrder(kDog);
            w.setCharacterKinematic(m_dogChar, true);
            m_over.crossing = true;
            m_over.t = 0.0f;
        } else if (m_over.giveUp <= 0.0f) {
            m_ai.clearOrder(kDog);
            m_over = {};
            m_hud->toast("It couldn't get there", 1.5f);
        }
        return;
    }
    m_over.t += o.speed(m_dog->runSpeed()) * dt;
    m_overPose = traverse(o, m_over.t, m_dog->height());
    w.moveCharacter(m_dogChar, m_overPose.feet);
    if (m_over.t >= o.span()) {
        w.setCharacterKinematic(m_dogChar, false);
        w.setCharacterVelocity(m_dogChar, o.dir * o.speed(m_dog->runSpeed()) * 0.6f);
        const int i = m_over.obstacle;
        m_over = {};
        if (m_course.took(i)) {
            kke::log::get(name())->info("agility: over the {} ({} of {}), {:.1f} s", obstacleName(o.kind), i + 1, obs.size(), m_course.time());
            if (m_course.finished()) {
                char line[96];
                std::snprintf(line, sizeof(line), "Clear round! %.1f s%s", static_cast<double>(m_course.result()),
                              m_course.faults() ? " (with faults)" : "");
                m_hud->toast(line, 4.0f);
                m_care.praised(0.3f);
                m_joyTime = 1.2f;
                kke::log::get(name())->info("agility: finished in {:.1f} s with {} faults, best {:.1f} s", m_course.time(), m_course.faults(),
                                            m_course.best());
            }
        }
        // And back to you.
        kke::ai::Order back;
        back.kind = kke::ai::Order::Kind::Follow;
        back.target = kPlayer;
        back.distance = 1.6f;
        back.run = true;
        m_ai.order(kDog, back);
        m_reflex.on = false;
    }
}

std::string PetModule::doing() const {
    if (m_care.dead()) return "has passed away (pause menu: adopt a new pet)";
    if (m_over.crossing) return std::string("over the ") + obstacleName(m_course.obstacles()[size_t(m_over.obstacle)].kind);
    if (m_over.obstacle >= 0) return "running to the " + std::string(obstacleName(m_course.obstacles()[size_t(m_over.obstacle)].kind));
    if (m_joyTime > 0.0f) return "jumping for joy";
    if (m_petTime > 0.0f) return "enjoying a pat";
    if (m_toilet.phase != Toilet::Phase::None) return "doing its business";
    switch (m_board.currentKind(kDog)) {
    case kke::OrderKind::Follow: return "at your side";
    case kke::OrderKind::Stay: return "staying put";
    case kke::OrderKind::Sit: return "sitting";
    case kke::OrderKind::Move: return "on its way";
    case kke::OrderKind::Pet: return "coming for a pat";
    case kke::OrderKind::Fetch: return m_bridge->carrying(kDog) ? "bringing it back" : "chasing it";
    default: break;
    }
    if (m_toBed) return "off to its bed";
    switch (chooseAct()) {
    case DogAct::Eat: return "eating";
    case DogAct::Drink: return "drinking";
    case DogAct::Sleep: return "having a nap";
    case DogAct::Beg: return "begging: its bowl is empty";
    case DogAct::Cower: return "moping";
    default: break;
    }
    if (m_reflex.on) switch (m_reflex.kind) {
        case kke::Intent::Kind::Approaching: return "waiting for a pat";
        case kke::Intent::Kind::LookingAt: return "curious";
        case kke::Intent::Kind::Walking: return "trotting along";
        case kke::Intent::Kind::Running: return "running with you";
        default: break;
        }
    const std::string act = m_ai.actionName(kDog);
    if (act == "wander") return "sniffing about";
    if (act == "graze") return "off to its food";
    if (act == "drink") return "off to its water";
    if (act == "rest") return "having a rest";
    if (act == "watch") return "watching";
    if (act == "investigate") return "having a look";
    if (act == "idle" || act.empty()) return "looking around";
    return act;
}

void PetModule::animateDog(float dt) {
    // Drawn between the last two physics steps: at its raw position the
    // body moves in 60 Hz jumps while the planted feet stay put.
    kke::RigidWorld& w = m_rigid->world();
    DogBody::Frame f;
    f.feet = m_over.crossing ? m_overPose.feet : w.characterDrawPosition(m_dogChar, m_app->fixedAlpha());
    f.yaw = m_dogYaw;
    f.velocity = m_over.crossing ? m_overPose.forward * m_course.obstacles()[size_t(m_over.obstacle)].speed(m_dog->runSpeed())
                                 : flat(w.characterVelocity(m_dogChar));
    f.turnRate = m_turnRate;
    f.act = chooseAct();
    f.phase = m_overPose.phase;
    f.pitch = m_over.crossing ? m_overPose.pitch : 0.0f;
    f.hasLook = m_hasLook;
    f.look = m_lookTarget;
    // On a ramp or a seesaw the ground is the obstacle's own surface.
    const Obstacle* on = m_over.crossing ? &m_course.obstacles()[size_t(m_over.obstacle)] : nullptr;
    const DogBody::Ground ground = [this, on](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        if (on && (on->kind == Obstacle::Kind::Ramp || on->kind == Obstacle::Kind::SeeSaw)) {
            const float along = glm::dot(flat(from - on->centre), on->dir);
            hit = glm::vec3(from.x, on->height(along), from.z);
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
            return true;
        }
        return groundAt(from, hit, normal);
    };
    m_dog->update(f, ground, dt);
}

// ---------------------------------------------------------------- the player, the toys

void PetModule::updateHands(float dt) {
    const glm::vec3 me = positionOf(kPlayer);
    const glm::vec3 fwd = m_loco->facing();
    const glm::vec3 right(-fwd.z, 0.0f, fwd.x);
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    // Walk the last bit to where the hands can reach (crouched, kneeling).
    auto stepTo = [&](const glm::vec3& target, float reach) {
        glm::vec3 d = flat(target - me);
        const float dist = glm::length(d);
        if (dist < 1e-3f) return;
        d /= dist;
        m_loco->setFacing(d);
        const float off = dist - reach;
        if (std::abs(off) > 0.02f) m_loco->teleport(me + d * std::clamp(off, -1.2f * dt, 1.2f * dt) + glm::vec3(0.0f, 0.02f, 0.0f));
    };
    m_busyTime += dt;
    switch (m_busy) {
    case Busy::None: break;
    case Busy::PickUp:
    case Busy::Fill:
    case Busy::Scoop: {
        // Crouch, reach down, take it (or fill the bowl, or bag the poop), stand up.
        glm::vec3 at = m_busyAt;
        if (m_busy == Busy::PickUp) {
            const Toy* t = toy(m_busyToy);
            if (!t || (t->held != Toy::Held::No && t->held != Toy::Held::Player)) {
                m_busy = Busy::None;
                m_body->act("");
                break;
            }
            at = t->pos;
        }
        if (m_busyTime < 0.3f) stepTo(at, 0.42f);
        m_body->act("Crouch_Idle_Loop", true, 1.0f, 0.15f);
        if (m_busyTime > 0.12f && m_busyTime < 0.75f) m_body->reach(kRight, at + up * 0.03f, me + right * 0.5f + up * 0.6f);
        if (m_busyTime >= 0.55f && m_busyIndex != -2) {
            if (m_busy == Busy::PickUp) {
                if (Toy* t = toy(m_busyToy); t && t->held == Toy::Held::No) {
                    grab(*t);
                    t->held = Toy::Held::Player;
                    m_hud->toast("Got the " + t->label + ": throw it!");
                }
            } else if (m_busy == Busy::Fill && m_busyIndex >= 0 && size_t(m_busyIndex) < m_bowls.size()) {
                Bowl& b = m_bowls[size_t(m_busyIndex)];
                b.level = 1.0f;
                if (!b.place) b.place = m_ai.addPlace(b.water ? "water" : "dogfood", b.pos, 0.85f);
                m_hud->toast(b.water ? "Fresh water" : "Dinner's served", 1.5f);
            } else if (m_busy == Busy::Scoop && m_busyIndex >= 0 && size_t(m_busyIndex) < m_poops.size()) {
                if (m_poops[size_t(m_busyIndex)].model) m_models->remove(m_poops[size_t(m_busyIndex)].model);
                m_poops.erase(m_poops.begin() + m_busyIndex);
                m_hud->toast("All clean", 1.2f);
            }
            m_busyIndex = -2; // done its part
        }
        if (m_busyTime > 0.95f) {
            m_busy = Busy::None;
            m_busyIndex = -1;
            m_body->act("");
        }
        break;
    }
    case Busy::Throw:
        if (m_busyTime >= 0.3f) {
            if (Toy* t = toy(m_busyToy); t && t->held == Toy::Held::Player) {
                // Out of the hand, the way the camera looks.
                const glm::vec3 aim = glm::normalize(flat(m_rig.forward()) + glm::vec3(0.0f, 0.001f, 0.0f));
                glm::vec3 v = aim * 11.0f + up * 4.5f;
                if (t->kind == Toy::Kind::Frisbee) v = aim * 12.0f + up * 2.2f;
                if (t->kind == Toy::Kind::Football) v = aim * 9.0f + up * 5.0f;
                release(*t, v);
                t->thrownAgo = 0.0f;
                if (t->kind != Toy::Kind::Ball && t->kind != Toy::Kind::Football)
                    m_rigid->world().setAngularVelocity(t->body, t->kind == Toy::Kind::Frisbee ? up * 18.0f : right * 9.0f);
                m_care.excite(0.4f);
                kke::log::get(name())->info("{} thrown", t->label);
            }
            m_busyToy = 0;
        }
        if (m_busyTime > 0.8f) {
            m_busy = Busy::None;
            m_body->act("");
        }
        break;
    case Busy::Pet:
        if (m_petTime <= 0.0f) {
            m_busy = Busy::None;
            m_body->act("");
            break;
        }
        {
            // Kneel by its head and pat: the palm comes down on top of it, again and again.
            const glm::vec3 head = m_dog->headTop();
            const glm::vec3 away = glm::length(flat(me - head)) > 0.05f ? glm::normalize(flat(me - head)) : -fwd;
            stepTo(head + away * 0.0f, 0.5f);
            m_body->act("Fixing_Kneeling", true, 1.0f, 0.2f);
            const float pat = 0.5f - 0.5f * std::cos(m_busyTime * 6.2831853f * 1.4f);
            const glm::vec3 dogFwd = aiDir(m_dogYaw);
            m_body->reach(kRight, head + up * (0.015f + 0.07f * pat) - dogFwd * (0.03f * pat), me + right * 0.45f + up * 0.7f);
        }
        break;
    }
    // A toy in the hand: held in front at the waist, fingers round it.
    if (m_busy == Busy::None || m_busy == Busy::Throw)
        for (const Toy& t : m_toys)
            if (t.held == Toy::Held::Player) {
                if (m_busy == Busy::None) m_body->reach(kRight, me + up * 1.0f + fwd * 0.3f + right * 0.18f, me + right * 0.5f + up * 1.0f - fwd * 0.2f);
                m_body->grip(kRight, t.radius);
            }
}

void PetModule::updatePlayer(float dt) {
    kke::InputMap& in = m_input->map(0);
    kke::RigidWorld& world = m_rigid->world();
    const bool wheel = m_cmd.wheelOpen();
    if (m_captured && !wheel) {
        const glm::vec2 look = in.axis2("look");
        m_rig.addLook(look.x * 0.12f, look.y * 0.12f);
    }
    if (!wheel) {
        const glm::vec2 rate = in.axis2("look.rate");
        m_rig.addLook(rate.x * 200.0f * dt, rate.y * 140.0f * dt);
    }
    if (in.pressed("jump")) m_jumpQueued = true;

    glm::vec2 move = in.axis2("move");
    if (m_demo) {
        // Self-play: nobody at the controls, so the camera keeps the dog in view.
        move = glm::vec2(0.0f);
        const glm::vec3 toDog = flat(positionOf(kDog) - world.characterPosition(m_playerChar));
        if (glm::length(toDog) > 1.5f) m_rig.yaw += angleDelta(m_rig.yaw, kke::yawOf(toDog)) * (1.0f - std::exp(-2.0f * dt));
    }
    kke::Locomotion::Input li;
    li.move = m_rig.forward() * move.y + m_rig.right() * move.x;
    li.move.y = 0.0f;
    if (glm::length(li.move) > 1e-3f) li.move = glm::normalize(li.move) * std::min(1.0f, glm::length(move));
    li.fast = in.held("sprint");
    li.slow = in.held("walk");
    li.goUp = m_jumpQueued;
    m_jumpQueued = false;
    // Hands busy on the ground or on the dog: you stay where you are.
    if (m_busy != Busy::None && m_busy != Busy::Throw) li = {};
    m_loco->update(li, dt);
    const glm::vec3 feet = world.characterPosition(m_playerChar);
    if (feet.y < -20.0f) m_loco->teleport({ 0.0f, 0.1f, 5.0f });
    updateHands(dt);
    if (m_busy == Busy::None && !m_body->acting().empty() && m_body->actionFinished()) m_body->act("");
    // Drawn and followed between the last two physics steps: the raw
    // feet move in 60 Hz jumps, which shakes the body on a faster screen.
    const glm::vec3 drawn = world.characterDrawPosition(m_playerChar, m_app->fixedAlpha());
    m_body->update(drawn, m_loco->facingYaw(), m_loco->groundSpeed(), dt);
    m_rig.update(dt, drawn, [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = world.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    }, m_app->camera());

    // What the player seems to be doing, for the dog.
    kke::IntentSample s;
    s.position = feet;
    s.velocity = world.characterVelocity(m_playerChar);
    s.view = glm::normalize(m_app->camera().target - m_app->camera().position);
    std::vector<kke::IntentCandidate> things;
    for (const Toy& t : m_toys)
        if (t.held == Toy::Held::No) things.push_back({ t.id, t.pos, kke::Relation::Item });
    things.push_back({ kDog, positionOf(kDog), kke::Relation::Own });
    m_intent.update(s, things, dt);
}

void PetModule::updateToys(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 me = positionOf(kPlayer);
    const glm::mat4 mouth = m_dog->mouthFrame();
    for (Toy& t : m_toys) {
        t.thrownAgo += dt;
        switch (t.held) {
        case Toy::Held::No:
            if (t.body != kke::RigidWorld::kNoBody) {
                t.pos = w.position(t.body);
                t.rot = w.rotation(t.body);
                // A frisbee glides: lift from its speed while it flies flat, a little drag.
                if (t.kind == Toy::Kind::Frisbee && t.pos.y > 0.2f && t.thrownAgo < 6.0f) {
                    glm::vec3 v = w.velocity(t.body);
                    const float h = glm::length(flat(v));
                    const float flatness = std::abs((t.rot * glm::vec3(0, 1, 0)).y);
                    v.y += std::min(9.0f, 0.075f * h * h) * flatness * dt;
                    v.x *= std::exp(-0.12f * dt);
                    v.z *= std::exp(-0.12f * dt);
                    w.setVelocity(t.body, v);
                }
            }
            // Out over the field: back into the garden.
            if (std::abs(t.pos.x) > kGround - 1.0f || std::abs(t.pos.z) > kGround - 1.0f || t.pos.y < -5.0f) {
                w.remove(t.body);
                t.body = kke::RigidWorld::kNoBody;
                t.pos = glm::vec3(0.0f, 0.5f, 0.0f);
                release(t, glm::vec3(0.0f));
            }
            break;
        case Toy::Held::Player: {
            // In the palm, turned with the hand.
            t.pos = m_body->inPalm(kRight, t.radius);
            glm::mat4 palm(1.0f);
            if (m_body->palm(kRight, palm)) t.rot = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(palm[0])), glm::normalize(glm::vec3(palm[1])),
                                                                              glm::normalize(glm::vec3(palm[2]))));
            break;
        }
        case Toy::Held::Dog: {
            // Across its jaws (a stick, a bone), or by the rim (a frisbee).
            const glm::vec3 z = glm::vec3(mouth[2]);
            t.pos = glm::vec3(mouth[3]);
            glm::quat q = glm::quat_cast(glm::mat3(mouth));
            if (t.kind == Toy::Kind::Duck) q = q * glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));
            if (t.kind == Toy::Kind::Frisbee) {
                q = q * glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));
                t.pos += z * (t.half.x * 0.8f);
            }
            if (t.kind == Toy::Kind::Football) t.pos += z * (t.radius * 0.6f);
            t.rot = q;
            break;
        }
        }
        // The toy it just brought: yours again when it's at your feet (you crouch for it).
        const bool fetching = m_board.currentKind(kDog) == kke::OrderKind::Fetch && m_board.current(kDog)->target == t.id;
        if (t.held == Toy::Held::No && !fetching && m_busy == Busy::None && t.thrownAgo > 2.0f && glm::length(flat(t.pos - me)) < 1.2f &&
            m_board.currentKind(kDog) != kke::OrderKind::Fetch &&
            std::none_of(m_toys.begin(), m_toys.end(), [](const Toy& o) { return o.held == Toy::Held::Player; }) &&
            glm::length(w.characterVelocity(m_playerChar)) < 0.5f && t.thrownAgo < 60.0f) {
            m_busy = Busy::PickUp;
            m_busyTime = 0.0f;
            m_busyToy = t.id;
            t.thrownAgo = 1e9f;
        }
        if (t.model) {
            const glm::mat4 xf = glm::translate(glm::mat4(1.0f), t.pos) * glm::mat4_cast(t.rot) * glm::scale(glm::mat4(1.0f), glm::vec3(t.modelScale)) *
                                 glm::translate(glm::mat4(1.0f), -t.modelCentre);
            m_models->setTransform(t.model, xf);
        }
    }
}

// ---------------------------------------------------------------- the frame

void PetModule::onEvent(const SDL_Event& e) {
    m_cmd.onEvent(e);
    // A finger on the screen: the pointer is the finger, not the reticle.
    if (e.type == SDL_EVENT_FINGER_DOWN && m_captured) {
        m_captured = false;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), false);
    }
}

void PetModule::update(const kke::UpdateContext& ctx) {
    const float dt = std::min(ctx.dt, 0.05f);
    m_clock += dt;
    if (m_quitAfter > 0.0f && m_clock > m_quitAfter) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
    kke::InputMap& in = m_input->map(0);
    if (in.pressed("panels")) m_app->debugUi().setVisible(!m_app->debugUi().visible());
    m_captured = SDL_GetWindowRelativeMouseMode(m_app->window().handle()); // the pause menu frees it
    if (in.pressed("pet.mouse")) {
        m_captured = !m_captured;
        SDL_SetWindowRelativeMouseMode(m_app->window().handle(), m_captured);
    }

    const command_kit::CommandInput::Frame& f = m_cmd.update(in, *m_app, m_captured, dt);
    if (f.click && !m_captured) giveContext(f.pointer, f.force);
    if (f.context) giveContext(f.pointer, f.force);
    if (f.wheelGiven >= 0) giveWheel(f.wheelGiven, f.wheelTarget);
    if (!f.wheelOpen) {
        if (in.pressed("pet.come")) give(kke::OrderKind::Follow);
        if (in.pressed("pet.sit")) give(kke::OrderKind::Sit);
        if (in.pressed("pet.stay")) give(kke::OrderKind::Stay);
        if (in.pressed("pet.fetch")) give(kke::OrderKind::Fetch);
        if (in.pressed("pet.drop")) give(kke::OrderKind::Drop);
        if (in.pressed("interact")) interact();
        // Left mouse throws while the mouse turns the camera; RT always.
        if (in.pressed("fire") && (m_captured || m_cmd.padActive())) throwToy();
    }
    if (m_demo) runDemo(dt);

    updatePlayer(dt);
    updateDog(dt);
    updateAgility(dt);
    updateToilet(dt);
    updateCare(dt);
    animateDog(dt);
    updateToys(dt);
    if (m_script) m_script->fireEvents();
    updateHud(dt);
}

void PetModule::updateHud(float dt) {
    const command_kit::CommandInput::Frame& f = m_cmd.frame();
    const kke::UnitOrder* o = m_board.current(kDog);
    std::string toyText = "Toys: on the grass";
    for (const Toy& t : m_toys) {
        if (t.held == Toy::Held::Player) toyText = "Toy: the " + t.label + " in your hand (throw it)";
        if (t.held == Toy::Held::Dog) toyText = "Toy: the " + t.label + " in its mouth";
    }
    std::vector<command_kit::CommandHud::Line> lines{ { std::string("Order: ") + (o ? kke::orderLabel(o->kind) : "none"), "#ffcf5c" },
                                                      { m_dog->label() + ": " + doing() },
                                                      { "Mood: " + m_care.mood(), m_care.moodColor() },
                                                      { toyText, "#aab3cc" } };
    // On the course: what's next and the clock.
    if (positionOf(kPlayer).x > kGarden + 0.5f || m_course.running()) {
        char line[160];
        const std::vector<Obstacle>& obs = m_course.obstacles();
        if (m_course.finished())
            std::snprintf(line, sizeof(line), "Agility: done in %.1f s, %d fault%s (best %.1f s)", static_cast<double>(m_course.result()),
                          m_course.faults(), m_course.faults() == 1 ? "" : "s", static_cast<double>(m_course.best()));
        else if (m_course.next() < int(obs.size()))
            std::snprintf(line, sizeof(line), "Agility: next %d, the %s · %.1f s · %d fault%s", m_course.next() + 1,
                          obstacleName(obs[size_t(m_course.next())].kind), static_cast<double>(m_course.time()), m_course.faults(),
                          m_course.faults() == 1 ? "" : "s");
        else
            std::snprintf(line, sizeof(line), "Agility");
        lines.push_back({ line, "#56a8ff" });
    }
    m_hud->setLines(std::move(lines));
    const Care::Needs n = m_care.needs();
    auto color = [](float v) { return v > 0.5f ? std::string("#6fe39a") : v > 0.25f ? std::string("#ffcf5c") : std::string("#ff8a7a"); };
    m_hud->setMeters({ { "🍖", "Food", 1.0f - n.hunger, color(1.0f - n.hunger) },
                       { "💧", "Water", 1.0f - n.thirst, color(1.0f - n.thirst) },
                       { "⚡", "Energy", 1.0f - n.tiredness, color(1.0f - n.tiredness) },
                       { "❤", "Happy", m_care.happiness(), color(m_care.happiness()) },
                       { "✚", "Health", m_care.health(), color(m_care.health()) } });
    const kke::OrderKind k = o ? o->kind : kke::OrderKind::None;
    const std::string what = interactLabel();
    const char* whatIcon = what == "Pet" ? "❤" : what == "Feed" ? "🍖" : what == "Water" ? "💧" : "🧹";
    // Prompt text: {action} shows that button on the device in use.
    m_hud->setButtons({ { "👋", "Come", "{pet.come}", k == kke::OrderKind::Follow },
                        { "🐕", "Sit", "{pet.sit}", k == kke::OrderKind::Sit },
                        { "✋", "Stay", "{pet.stay}", k == kke::OrderKind::Stay },
                        { "🎾", "Fetch", "{pet.fetch}", k == kke::OrderKind::Fetch },
                        { "👇", "Drop it", "{pet.drop}", k == kke::OrderKind::Drop },
                        { whatIcon, what, "{interact}", k == kke::OrderKind::Pet },
                        { "🥏", "Throw", "{fire}", false } });
    const kke::PromptStyle style = m_input->promptStyle();
    m_hud->setHint(style == kke::PromptStyle::Touch ? "{touch:tap} order there (tap the dog to pet it, an obstacle to send it over) · {touch:hold} order wheel"
                   : m_cmd.padActive() || m_captured
                       ? "{move} move · {cmd.context} order at the ring (an obstacle: over it) · hold {cmd.wheel} order wheel · "
                         "{pet.come}{pet.sit}{pet.stay}{pet.fetch} quick orders · {interact} " + what + " · {fire} pick up / throw · {pet.mouse} free the mouse"
                       : "{mouse:left} order there (the dog: pet it; an obstacle: over it) · hold {cmd.wheel} order wheel · {pet.mouse} look with the mouse");
    m_hud->update(f, dt);
}

void PetModule::render(const kke::RenderContext& ctx) {
    m_scenery->render(ctx);
    m_body->render(ctx);
    m_dog->render(ctx);
    if (m_toyBlock)
        for (const Toy& t : m_toys)
            m_toyBlock->draw(ctx, glm::translate(glm::mat4(1.0f), t.pos) * glm::mat4_cast(t.rot) * glm::scale(glm::mat4(1.0f), t.half * 2.0f), 0.0f, 0.5f);
    if (m_poopBlock)
        for (const Poop& p : m_poops) m_poopBlock->draw(ctx, glm::translate(glm::mat4(1.0f), p.pos), 0.0f, 0.6f);
}

void PetModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_scenery->renderShadow(ctx);
    m_body->renderShadow(ctx);
    m_dog->renderShadow(ctx);
}

// ---------------------------------------------------------------- KKE_PET_DEMO

void PetModule::runDemo(float dt) {
    m_demoTimer += dt;
    auto next = [&](const char* what) {
        kke::log::get(name())->info("demo: {}", what);
        ++m_demoStep;
        m_demoTimer = 0.0f;
    };
    const glm::vec3 dog = positionOf(kDog), me = positionOf(kPlayer);
    switch (m_demoStep) {
    case 0:
        if (m_demoTimer > 1.0f) {
            // Walk to the ball and crouch for it.
            const glm::vec3 ballPos = m_toys[0].pos;
            m_loco->teleport(ballPos + glm::vec3(0.6f, -ballPos.y + 0.05f, 0.6f));
            throwToy();
            next("picking up the ball");
        }
        break;
    case 1:
        if (m_demoTimer > 1.5f) {
            kke::log::get(name())->info("demo: the ball is {:.2f} m from the right palm", glm::length(m_toys[0].pos - m_body->inPalm(kRight, m_toys[0].radius)));
            m_rig.yaw = 0.0f; // throw toward -Z
            throwToy();
            next("threw the ball");
        }
        break;
    case 2:
        if (m_board.currentKind(kDog) != kke::OrderKind::Fetch && m_demoTimer > 1.0f) {
            kke::log::get(name())->info("demo: fetch finished after {:.1f} s, the ball is {:.1f} m from the player", m_demoTimer,
                                        glm::length(flat(m_toys[0].pos - me)));
            next("fetched");
        } else if (m_demoTimer > 20.0f) {
            next("FETCH TIMED OUT");
        }
        break;
    case 3:
        if (m_demoTimer > 2.0f) {
            // A ball over the fence: it goes round through the gate.
            Toy& t = m_toys[1];
            if (t.held != Toy::Held::No) release(t, glm::vec3(0.0f));
            m_rigid->world().remove(t.body);
            t.body = kke::RigidWorld::kNoBody;
            t.pos = { 6.0f, 0.3f, -14.0f };
            release(t, glm::vec3(0.0f));
            give(kke::OrderKind::Fetch, t.id);
            next("fetch the football outside the fence");
        }
        break;
    case 4:
        if (m_board.currentKind(kDog) != kke::OrderKind::Fetch && m_demoTimer > 1.0f) {
            kke::log::get(name())->info("demo: fetched from outside the fence in {:.1f} s", m_demoTimer);
            next("fetched from outside");
        } else if (m_demoTimer > 40.0f) {
            kke::log::get(name())->info("demo: the dog got to {:.1f} {:.1f}", dog.x, dog.z);
            next("FETCH OUTSIDE TIMED OUT");
        }
        break;
    case 5:
        give(kke::OrderKind::Sit);
        next("sit");
        break;
    case 6:
        if (m_demoTimer > 2.5f) {
            give(kke::OrderKind::Stay);
            m_demoWant = dog;
            next("stay, and the player walks away");
        }
        break;
    case 7:
        m_loco->teleport(me + glm::vec3(2.0f * dt, 0.0f, 0.0f)); // walks off
        if (m_demoTimer > 3.0f) {
            kke::log::get(name())->info("demo: the dog stayed within {:.2f} m of its spot", glm::length(flat(dog - m_demoWant)));
            give(kke::OrderKind::Follow);
            next("come");
        }
        break;
    case 8:
        if (m_demoTimer > 3.0f) {
            pet();
            next("pet");
        }
        break;
    case 9:
        if (m_busy == Busy::Pet && m_petTime < 1.5f && m_demoBlocked == 0) {
            glm::mat4 palm(1.0f);
            m_body->palm(kRight, palm);
            kke::log::get(name())->info("demo: patting, the palm is {:.2f} m from the top of its head", glm::length(glm::vec3(palm[3]) - m_dog->headTop()));
            m_demoBlocked = 1;
        }
        if (m_board.currentKind(kDog) != kke::OrderKind::Pet && m_demoTimer > 1.0f) {
            kke::log::get(name())->info("demo: petted, happiness {:.2f}", m_care.happiness());
            next("petted");
        } else if (m_demoTimer > 15.0f) {
            next("PET TIMED OUT");
        }
        break;
    case 10:
        // Hungry with an empty bowl: you fill it, it eats.
        for (Bowl& b : m_bowls)
            if (!b.water) {
                b.level = 0.0f;
                if (b.place) m_ai.removePlace(b.place);
                b.place = 0;
                m_loco->teleport(b.pos + glm::vec3(0.0f, 0.05f, 1.0f));
            }
        m_ai.setNeed(kDog, "hunger", 0.9f);
        give(kke::OrderKind::Free);
        next("hungry, the bowl empty");
        break;
    case 11:
        if (m_demoTimer > 1.0f && m_busy == Busy::None && m_bowls[0].level < 0.5f) {
            interact();
            next("filling the food bowl");
        }
        break;
    case 12:
        // Out of its way once the bowl is full.
        if (m_busy == Busy::None && glm::length(flat(me - m_bowls[0].pos)) < 2.0f) m_loco->teleport(m_bowls[0].pos + glm::vec3(2.5f, 0.05f, 2.0f));
        if (m_ai.need(kDog, "hunger") < 0.5f) {
            kke::log::get(name())->info("demo: it ate, hunger {:.2f}, the bowl at {:.2f}", m_ai.need(kDog, "hunger"), m_bowls[0].level);
            next("ate");
        } else if (m_demoTimer > 60.0f) {
            kke::log::get(name())->info("demo: hunger still {:.2f}, doing: {}", m_ai.need(kDog, "hunger"), doing());
            next("EATING TIMED OUT");
        }
        break;
    case 13:
        m_care.ate(2.0f); // time to go
        next("needs the toilet");
        break;
    case 14:
        if (!m_poops.empty()) {
            m_loco->teleport(m_poops.back().pos + glm::vec3(0.8f, 0.05f, 0.0f));
            interact();
            next("cleaning up");
        } else if (m_demoTimer > 30.0f) {
            next("POOP TIMED OUT");
        }
        break;
    case 15:
        if (m_demoTimer > 1.5f) {
            kke::log::get(name())->info("demo: {} poops left", m_poops.size());
            m_loco->teleport(m_course.start() + glm::vec3(0.0f, 0.05f, 0.0f));
            m_rigid->world().teleportCharacter(m_dogChar, m_course.start() + glm::vec3(-0.5f, 0.05f, 1.0f));
            m_course.reset();
            next("agility");
        }
        break;
    case 16:
        // The handler runs the course with the dog: to each obstacle in turn.
        if (!m_course.finished()) {
            const std::vector<Obstacle>& obs = m_course.obstacles();
            if (m_course.next() < int(obs.size())) {
                const Obstacle& o = obs[size_t(m_course.next())];
                const glm::vec3 to = flat(o.centre + glm::vec3(o.dir.z, 0.0f, -o.dir.x) * 2.2f - me);
                if (glm::length(to) > 0.3f) m_loco->teleport(me + glm::normalize(to) * std::min(glm::length(to), 4.0f * dt) + glm::vec3(0, 0.02f, 0));
            }
            if (m_demoTimer > 90.0f) next("AGILITY TIMED OUT");
        } else {
            next("agility done");
        }
        break;
    case 17:
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

} // namespace pet_companion
