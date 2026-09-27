#include "PetModule.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/Log.h"
#include "kke/Picking.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/ScriptModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <typeindex>

namespace pet_companion {

namespace {

constexpr float kBallRadius = 0.11f;
constexpr float kDogHeight = 0.55f;

glm::vec3 flat(const glm::vec3& v) { return glm::vec3(v.x, 0.0f, v.z); }

float approach(float value, float target, float rate, float dt) { return value + (target - value) * (1.0f - std::exp(-rate * dt)); }

float angleDelta(float from, float to) { return std::fmod(to - from + 540.0f, 360.0f) - 180.0f; }

// AiWorld's yaw (0 = +Z, 90 = +X) of a direction, and a body standing there facing it.
float aiYaw(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, d.z)); }
glm::mat4 bodyAt(const glm::vec3& feet, float yaw) {
    return glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(yaw), glm::vec3(0, 1, 0));
}

// The wheel, clockwise from the top.
const kke::OrderKind kWheel[] = { kke::OrderKind::Follow, kke::OrderKind::Fetch, kke::OrderKind::Move, kke::OrderKind::Drop,
                                  kke::OrderKind::Free,   kke::OrderKind::Pet,   kke::OrderKind::Stay, kke::OrderKind::Sit };
const char* kWheelIcons[] = { "👋", "🎾", "📍", "👇", "🌼", "❤", "✋", "🐕" };
const char* kWheelLabels[] = { "Come", "Fetch", "Go there", "Drop it", "At ease", "Pet", "Stay", "Sit" };

bool envOn(const char* name) {
    const char* v = std::getenv(name);
    return v && *v && std::string(v) != "0";
}

} // namespace

PetModule::PetModule() = default;
PetModule::~PetModule() = default;

std::vector<kke::ModuleDependency> PetModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the characters, the ball and the garden" },
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
    quick("pet.drop", "Drop it", SDL_SCANCODE_5, SDL_GAMEPAD_BUTTON_INVALID);
    quick("pet.mouse", "Free the mouse / look with it", SDL_SCANCODE_ESCAPE, SDL_GAMEPAD_BUTTON_INVALID);
    quick("panels", "Developer panels", SDL_SCANCODE_F1, SDL_GAMEPAD_BUTTON_INVALID);
    m_input->commitDefaults();
    app.window().setQuitOnEscape(false);

    m_scenery = std::make_unique<command_kit::Scenery>(app, *m_models, *m_rigid);
    buildGarden();

    kke::RigidWorld& w = m_rigid->world();
    kke::RigidWorld::CharacterDesc pd;
    pd.position = glm::vec3(0.0f, 0.05f, 5.0f);
    m_playerChar = w.addCharacter(pd);
    m_loco = std::make_unique<kke::Locomotion>(w, m_playerChar);
    m_kit = std::make_unique<command_kit::HumanoidKit>(app);
    m_kit->load(*m_models);
    m_body = std::make_unique<command_kit::Humanoid>(*m_kit, *m_models, glm::vec3(0.55f, 0.75f, 1.0f));
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.pitch = -16.0f;
    m_rig.settings.armLength = 4.2f;

    kke::RigidWorld::CharacterDesc dd;
    dd.radius = 0.2f;
    dd.height = kDogHeight;
    dd.mass = 12.0f;
    dd.stepUp = 0.2f;
    dd.position = glm::vec3(1.6f, 0.05f, 3.5f);
    m_dogChar = w.addCharacter(dd);
    loadDog();

    addBall({ -2.5f, 0.3f, 1.0f });
    addBall({ 3.5f, 0.3f, -2.0f });

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
    setUpAi();
    if (auto* scripts = app.getModule<kke::ScriptModule>()) m_script = std::make_unique<kke::OrderScript>(scripts->vm(), m_board, *this);

    m_hud = std::make_unique<command_kit::CommandHud>(app);
    m_hud->build("YOUR DOG");
    m_hud->onButton = [this](int b) { pressButton(b); };
    std::vector<command_kit::CommandHud::WheelItem> wheel;
    for (size_t i = 0; i < std::size(kWheel); ++i) wheel.push_back({ kWheelIcons[i], kWheelLabels[i] });
    m_hud->setWheel(std::move(wheel));
    m_cmd.overUi = [this](const glm::vec2& p) { return m_hud->overButtons(p); };

    m_demo = envOn("KKE_PET_DEMO");
    if (const char* q = std::getenv("KKE_PET_QUIT")) m_quitAfter = float(std::atof(q));
    if (m_demo && m_quitAfter < 0.0f) m_quitAfter = 90.0f;
    m_scenery->logUsed(name());
    kke::log::get(name())->info("the garden is ready: {}, its brain the AI core's \"dog\"",
                                m_dogModel ? "the pug from Quaternius' Farm Animals" : "no dog model found, the dog is a block");
}

void PetModule::shutdown() {
    // Everything holding GPU or UI resources goes before the modules that own them.
    m_script.reset();
    m_bridge.reset();
    m_hud.reset();
    m_dogBlock.reset();
    m_ballBlock.reset();
    m_body.reset();
    m_kit.reset();
    m_scenery.reset();
}

// ---------------------------------------------------------------- the garden

void PetModule::buildGarden() {
    command_kit::Scenery& s = *m_scenery;
    s.ground(40.0f, { 0.32f, 0.5f, 0.26f });
    const std::vector<std::string> town{ "POLYGON_Town", "PolygonTown" };
    // A fenced garden, 24 m across, the doghouse at the back.
    bool fence = false;
    for (int i = -4; i < 4; ++i) {
        const float x = float(i) * 2.5f + 1.25f;
        fence |= s.place("SM_Env_Fence_Wood_Straight_01", { x - 1.25f, 0.0f, -12.0f }, 0.0f, 1.0f, true, town) != 0;
        s.place("SM_Env_Fence_Wood_Straight_01", { x - 1.25f, 0.0f, 12.0f }, 0.0f, 1.0f, true, town);
        s.place("SM_Env_Fence_Wood_Straight_01", { -10.0f, 0.0f, x - 1.25f }, 90.0f, 1.0f, true, town);
        s.place("SM_Env_Fence_Wood_Straight_01", { 10.0f, 0.0f, x - 1.25f }, 90.0f, 1.0f, true, town);
    }
    if (!fence) {
        const glm::vec3 wood(0.45f, 0.32f, 0.2f);
        s.block({ 0, 0.5f, -12 }, { 10, 0.5f, 0.05f }, wood);
        s.block({ 0, 0.5f, 12 }, { 10, 0.5f, 0.05f }, wood);
        s.block({ -10, 0.5f, 0 }, { 0.05f, 0.5f, 12 }, wood);
        s.block({ 10, 0.5f, 0 }, { 0.05f, 0.5f, 12 }, wood);
    }
    if (!s.place("SM_Prop_Doghouse_01", { -6.0f, 0.0f, -8.5f }, 25.0f, 1.0f, true, town))
        s.block({ -6, 0.6f, -8.5f }, { 0.7f, 0.6f, 0.6f }, { 0.7f, 0.25f, 0.2f });
    if (!s.place("SM_Env_Tree_01", { 6.5f, 0.0f, -7.0f }, 0.0f, 1.0f, true, town)) s.block({ 6.5f, 2, -7 }, { 0.3f, 2, 0.3f }, { 0.4f, 0.3f, 0.2f });
    s.place("SM_Env_Tree_02", { -7.5f, 0.0f, 6.5f }, 40.0f, 1.0f, true, town);
    s.place("SM_Env_Hedge_01", { 7.0f, 0.0f, 7.5f }, 90.0f, 1.0f, true, town);
    s.place("SM_Prop_Barrel_01", { 8.3f, 0.0f, -1.0f }, 0.0f, 1.0f, true, town);
    for (int i = 0; i < 18; ++i) {
        const float a = float(i) * 2.39996f, r = 3.0f + float(i % 6) * 1.2f;
        s.place("SM_Env_Grass_01", { std::cos(a) * r, 0.0f, std::sin(a) * r }, float(i) * 37.0f, 1.0f, false, town);
    }
}

void PetModule::loadDog() {
    m_dogModel = 0;
    const kke::ModelModule::ModelId id = m_scenery->model("Pug", { "Farm Animals Animated  by Quaternius" }, true);
    const kke::ModelData* d = id ? m_models->model(id) : nullptr;
    if (d && !d->bones.empty()) {
        m_dogModel = m_models->spawn(id);
        m_models->setOverlayEnabled(m_dogModel, false);
        m_dogScale = kDogHeight * 0.95f / std::max(0.1f, d->boundsMax.y - d->boundsMin.y);
        m_dogRig = kke::ModelData{};
        m_dogRig.bones = d->bones;
        m_dogRig.animations = d->animations;
        m_dogSet = std::make_unique<kke::AnimationSet>(m_dogRig);
        m_dogAnim = std::make_unique<kke::Animator>(*m_dogSet);
        m_dogIdle = m_dogAnim->addClipState("idle", m_dogSet->find("Idle"), true);
        m_dogJump = m_dogAnim->addClipState("jump", m_dogSet->find("Jump"), false, 1.2f);
        m_dogAnim->play(m_dogIdle, 0.0f);
        auto bone = [&](const char* n) {
            for (size_t b = 0; b < m_dogRig.bones.size(); ++b)
                if (m_dogRig.bones[b].name == n) return int(b);
            return -1;
        };
        for (const char* n : { "FrontUpLeg.L", "FrontUpLeg.R", "BackUpLeg.L", "BackUpLeg.R" }) m_legs.push_back(bone(n));
        for (const char* n : { "FrontLowLeg.L", "FrontLowLeg.R", "BackLowLeg.L", "BackLowLeg.R" }) m_knees.push_back(bone(n));
        m_hips = bone("Hips");
        // Procedural legs: the pug faces +Z like the gait's body, so only its scale lies between.
        m_legChains = kke::quadrupedLegChains(m_dogRig);
        std::vector<kke::LegDesc> legs = kke::legsFromSkeleton(m_dogRig, m_legChains, glm::scale(glm::mat4(1.0f), glm::vec3(m_dogScale)));
        if (legs.size() == 4 && m_hips >= 0) {
            kke::GaitSettings gs;
            gs.stepHeight = 0.3f;
            m_gait = kke::ProceduralGait(std::move(legs), gs);
        } else {
            kke::log::get(name())->warn("the pug has {} of 4 leg chains: its legs stay still", legs.size());
        }
        m_look = kke::LookAt::quadruped(m_dogRig);
        return;
    }
    // No pack: a brown block with a nose.
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    command_kit::appendBox({ 0, 0.3f, 0 }, { 0.15f, 0.15f, 0.3f }, { 0.75f, 0.55f, 0.35f }, v, idx);
    command_kit::appendBox({ 0, 0.45f, -0.33f }, { 0.1f, 0.1f, 0.1f }, { 0.35f, 0.25f, 0.15f }, v, idx);
    m_dogBlock = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
    m_dogBlock->upload(v, idx);
}

void PetModule::addBall(const glm::vec3& pos) {
    Ball b;
    b.id = kFirstBall + uint32_t(m_balls.size());
    b.pos = pos;
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Sphere;
    d.radius = kBallRadius;
    d.position = pos;
    d.mass = 0.06f;
    d.restitution = 0.6f;
    d.friction = 0.5f;
    b.body = m_rigid->world().add(d);
    if (const kke::ModelModule::ModelId m = m_scenery->model("SM_Item_Ball_Soccer_01", { "POLYGON_Town" })) {
        b.model = m_models->spawn(m);
        m_models->setOverlayEnabled(b.model, false);
    } else if (!m_ballBlock) {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> idx;
        command_kit::appendBox(glm::vec3(0.0f), glm::vec3(kBallRadius * 0.85f), { 0.95f, 0.85f, 0.2f }, v, idx);
        m_ballBlock = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
        m_ballBlock->upload(v, idx);
    }
    m_balls.push_back(b);
}

PetModule::Ball* PetModule::ball(uint32_t id) {
    for (Ball& b : m_balls)
        if (b.id == id) return &b;
    return nullptr;
}

glm::vec3 PetModule::positionOf(uint32_t thing) const {
    const kke::RigidWorld& w = m_rigid->world();
    if (thing == kPlayer) return w.characterPosition(m_playerChar);
    if (thing == kDog) return w.characterPosition(m_dogChar);
    for (const Ball& b : m_balls)
        if (b.id == thing) return b.pos;
    return glm::vec3(NAN);
}

bool PetModule::exists(uint32_t thing) const {
    return thing == kPlayer || thing == kDog || std::any_of(m_balls.begin(), m_balls.end(), [&](const Ball& b) { return b.id == thing; });
}

// ---------------------------------------------------------------- orders

void PetModule::give(kke::OrderKind kind, uint32_t target, const glm::vec3* point) {
    kke::Order o;
    o.kind = kind;
    o.units = { kDog };
    o.issuer = kPlayer;
    o.target = target;
    if (kind == kke::OrderKind::Follow && !target) o.target = kPlayer;
    if (kind == kke::OrderKind::Pet) o.target = kPlayer;
    if (kind == kke::OrderKind::Fetch && !o.target) {
        // The nearest ball nobody is holding.
        const glm::vec3 dog = positionOf(kDog);
        float best = 1e9f;
        for (const Ball& b : m_balls)
            if (b.held != Ball::Held::Player && glm::length(b.pos - dog) < best) {
                best = glm::length(b.pos - dog);
                o.target = b.id;
            }
        if (!o.target) {
            m_hud->toast("You have the ball: throw it first");
            return;
        }
    }
    if (point) {
        o.point = *point;
        o.hasPoint = true;
    }
    std::string why;
    if (!m_board.issue(o, &why)) {
        kke::log::get(name())->info("order not given: {}", why);
        return;
    }
    playerSay(std::string(kke::orderLabel(kind)) + "!");
}

kke::PointerTarget PetModule::pick(const glm::vec2& pointer) const {
    const kke::Camera& cam = m_app->camera();
    int w = 1, h = 1;
    SDL_GetWindowSize(m_app->window().handle(), &w, &h);
    const glm::mat4 view = glm::lookAt(cam.position, cam.target, cam.up);
    const glm::mat4 proj = kke::engineProjection(cam.fovDegrees, float(w) / float(std::max(h, 1)), cam.nearPlane, cam.farPlane);
    const kke::Ray ray = kke::screenToRay(pointer, { float(w), float(h) }, view, proj);
    kke::PointerTarget t;
    float best = 1e30f;
    auto test = [&](uint32_t id, kke::Relation rel, const glm::vec3& mn, const glm::vec3& mx) {
        const float d = kke::rayAabb(ray, mn, mx);
        if (d >= 0.0f && d < best) {
            best = d;
            t.thing = id;
            t.relation = rel;
            t.point = ray.at(d);
        }
    };
    // Generous boxes: a ball is small, and a finger is big.
    for (const Ball& b : m_balls)
        if (b.held == Ball::Held::No) test(b.id, kke::Relation::Item, b.pos - glm::vec3(0.35f), b.pos + glm::vec3(0.35f));
    const glm::vec3 dog = positionOf(kDog), me = positionOf(kPlayer);
    test(kDog, kke::Relation::Own, dog + glm::vec3(-0.45f, 0.0f, -0.45f), dog + glm::vec3(0.45f, 0.7f, 0.45f));
    test(kPlayer, kke::Relation::Self, me + glm::vec3(-0.3f, 0.0f, -0.3f), me + glm::vec3(0.3f, 1.8f, 0.3f));
    if (t.thing) return t;
    const kke::RigidWorld::RayHit hit = m_rigid->world().raycast(ray.origin, ray.direction, 200.0f);
    if (hit.hit) t.point = hit.point;
    else if (float d = kke::rayPlaneY(ray, 0.0f); d > 0.0f) t.point = ray.at(d);
    else t.point = positionOf(kDog);
    return t;
}

void PetModule::giveContext(const glm::vec2& pointer, bool force) {
    const kke::PointerTarget t = pick(pointer);
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
    case 5: pet(); break;
    case 6: throwBall(); break;
    default: break;
    }
}

void PetModule::pet() { give(kke::OrderKind::Pet); }

void PetModule::throwBall() {
    const glm::vec3 me = positionOf(kPlayer);
    for (Ball& b : m_balls) {
        if (b.held != Ball::Held::Player) continue;
        const glm::vec3 fwd = glm::normalize(flat(m_rig.forward()) + glm::vec3(0.0f, 0.001f, 0.0f));
        const glm::vec3 hand = me + glm::vec3(0.0f, 1.5f, 0.0f) + fwd * 0.5f;
        b.pos = hand;
        release(b, fwd * 10.0f + glm::vec3(0.0f, 4.0f, 0.0f));
        b.thrownAgo = 0.0f;
        m_body->act("Spell_Simple_Shoot", false, 1.4f, 0.08f, true);
        m_excited = std::min(1.0f, m_excited + 0.4f);
        kke::log::get(name())->info("ball thrown");
        return;
    }
    // Not holding one: pick up the nearest within reach.
    Ball* nearest = nullptr;
    float best = 1.8f;
    for (Ball& b : m_balls)
        if (b.held == Ball::Held::No && glm::length(flat(b.pos - me)) < best) {
            best = glm::length(flat(b.pos - me));
            nearest = &b;
        }
    if (!nearest) {
        m_hud->toast("No ball near you");
        return;
    }
    grab(*nearest);
    nearest->held = Ball::Held::Player;
    m_body->act("PickUp_Table", false, 1.6f, 0.08f, true);
    m_hud->toast("Got the ball: throw it!");
}

void PetModule::grab(Ball& b) {
    if (b.body != kke::RigidWorld::kNoBody) m_rigid->world().remove(b.body);
    b.body = kke::RigidWorld::kNoBody;
    b.held = Ball::Held::Dog;
    // A ball in a mouth or a hand isn't a thing on the lawn: the AI stops seeing it.
    m_ai.setEnabled(b.id, false);
}

void PetModule::release(Ball& b, const glm::vec3& velocity) {
    if (b.body == kke::RigidWorld::kNoBody) {
        kke::RigidWorld::BodyDesc d;
        d.shape = kke::RigidWorld::Shape::Sphere;
        d.radius = kBallRadius;
        d.position = b.pos;
        d.velocity = velocity;
        d.mass = 0.06f;
        d.restitution = 0.6f;
        d.friction = 0.5f;
        b.body = m_rigid->world().add(d);
    }
    b.held = Ball::Held::No;
    m_ai.setTransform(b.id, b.pos, glm::vec3(0.0f), 0.0f);
    m_ai.setEnabled(b.id, true);
}

void PetModule::playerSay(const std::string& text) { m_hud->toast(text, 1.2f); }

// ---------------------------------------------------------------- the dog: the AI core decides
//
// The dog is an agent of the AI core's built-in "dog" species; you, the
// balls and the garden are what it perceives. Orders go board -> bridge ->
// AiWorld::order; with none, what you're doing becomes a soft AI order
// (never a command: the board stays empty). This part only moves the
// Jolt body the way the AI wants and shows how the dog feels.

void PetModule::setUpAi() {
    // The built-in dog, pug-sized, living around the middle of the garden.
    kke::ai::Species dog = *m_ai.species("dog");
    dog.label = "Pug";
    dog.walkSpeed = 1.2f;
    dog.runSpeed = 4.5f;
    dog.radius = 0.25f;
    dog.homeRadius = 7.0f;
    dog.actions.clear(); // rebuilt from the changes
    m_ai.defineSpecies(dog);
    // A ball: something to see and fetch, with no life of its own.
    kke::ai::Species toy;
    toy.id = "ball";
    toy.label = "Ball";
    toy.radius = kBallRadius;
    toy.scent = 0.0f;
    toy.needs.clear();
    toy.eats.clear();
    toy.drinks.clear();
    m_ai.defineSpecies(toy);

    m_ai.addActor(kPlayer, "farmer", positionOf(kPlayer));
    m_ai.addAgent(kDog, "dog", positionOf(kDog));
    for (const Ball& b : m_balls) m_ai.addActor(b.id, "ball", b.pos);
    // What stands in the garden (buildGarden): walked around, not into.
    for (const kke::ai::CircleObstacle& o : std::initializer_list<kke::ai::CircleObstacle>{
             { { -6.0f, 0.0f, -8.5f }, 1.0f }, { { 6.5f, 0.0f, -7.0f }, 0.6f }, { { -7.5f, 0.0f, 6.5f }, 0.6f },
             { { 7.0f, 0.0f, 7.5f }, 1.4f }, { { 8.3f, 0.0f, -1.0f }, 0.5f } })
        m_ai.addObstacle(o);

    m_bridge = std::make_unique<kke::AiOrderBridge>(m_board, m_ai);
    m_bridge->followDistance = 1.6f;
    m_bridge->pickUp = [this](uint32_t, uint32_t thing) {
        Ball* b = ball(thing);
        if (!b || b->held != Ball::Held::No) return false; // you picked it up first
        grab(*b);
        kke::log::get(name())->info("got the ball");
        return true;
    };
    m_bridge->deliver = [this](uint32_t, uint32_t thing, uint32_t) {
        if (Ball* b = ball(thing)) release(*b, glm::vec3(0.0f));
        m_happy = std::min(1.0f, m_happy + 0.15f);
        m_joyTime = 1.2f;
        m_hud->toast("Good dog!");
    };
    m_bridge->drop = [this](uint32_t unit) {
        if (Ball* b = ball(m_bridge->carrying(unit))) release(*b, glm::vec3(0.0f));
    };
    // It came for the pat: you kneel, it sits, and the order ends when the pat does.
    m_bridge->petStart = [this](uint32_t, uint32_t) { m_petTime = m_kneel = 2.4f; };
}

void PetModule::updateDog(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 dog = positionOf(kDog), me = positionOf(kPlayer);
    const glm::vec3 myVel = flat(w.characterVelocity(m_playerChar));
    const glm::vec3 dogVel = flat(w.characterVelocity(m_dogChar));
    m_ai.setTransform(kPlayer, me, myVel, aiYaw(m_loco->facing()));
    m_ai.setTransform(kDog, dog, dogVel, m_dogYaw);
    for (const Ball& b : m_balls)
        if (b.held == Ball::Held::No) m_ai.setTransform(b.id, b.pos, glm::vec3(0.0f), 0.0f);

    // A good companion fetches a ball you throw without being told.
    kke::OrderKind now = m_board.currentKind(kDog);
    if (now == kke::OrderKind::None || now == kke::OrderKind::Free || now == kke::OrderKind::Follow) {
        for (const Ball& b : m_balls)
            if (b.held == Ball::Held::No && b.thrownAgo < 1.0f) {
                give(kke::OrderKind::Fetch, b.id);
                kke::log::get(name())->info("the dog goes after the ball on its own");
                break;
            }
    }
    const kke::UnitOrder* o = m_board.current(kDog);
    if (o && o->status == kke::UnitOrder::Status::Given) m_board.markRunning(kDog);
    now = m_board.currentKind(kDog);

    // No command: it reads what you're doing (kke::IntentReader) and plays along.
    if (now == kke::OrderKind::None || now == kke::OrderKind::Free) {
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
        if (soft.kind != kke::ai::Order::Kind::None) {
            want.kind = in.kind;
            want.thing = in.thing;
        }
        if (!m_reflex.on || want.kind != m_reflex.kind || want.thing != m_reflex.thing) {
            if (soft.kind == kke::ai::Order::Kind::None) m_ai.clearOrder(kDog); // its own life: sniff, rest, watch you
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
        m_happy = std::min(1.0f, m_happy + dt * 0.12f);
        if (m_petTime <= 0.0f) {
            m_board.complete(kDog, true);
            m_joyTime = 1.2f;
            m_hud->toast("❤");
        }
    } else if (now != kke::OrderKind::Pet) {
        m_petTime = 0.0f;
    }

    // The body goes where the AI wants, except while it sits, is petted or jumps for joy.
    const kke::ai::Agent* a = m_ai.agent(kDog);
    const bool sitting = now == kke::OrderKind::Sit || a->anim == "rest" || m_petTime > 0.0f;
    if (m_joyTime > 0.0f) m_joyTime -= dt;
    glm::vec3 want = flat(a->desiredVelocity);
    if (sitting || m_joyTime > 0.0f) want = glm::vec3(0.0f);
    kke::RigidWorld::CharacterInput ci;
    ci.move = want;
    w.setCharacterInput(m_dogChar, ci);
    m_sit = approach(m_sit, sitting ? 1.0f : 0.0f, 6.0f, dt);

    // Face where it runs; standing, what it attends to, else you.
    float face = m_dogYaw;
    glm::vec3 look = me + glm::vec3(0.0f, 1.2f, 0.0f);
    if (a->hasLookAt) look = a->lookAt;
    else if (a->focus && m_ai.agent(a->focus)) look = m_ai.agent(a->focus)->position;
    else if (m_reflex.on && m_reflex.thing && m_reflex.thing != kDog) look = positionOf(m_reflex.thing);
    else if (now == kke::OrderKind::Fetch && o && !m_bridge->carrying(kDog)) look = positionOf(o->target);
    if (glm::length(dogVel) > 0.4f) face = aiYaw(dogVel);
    else if (glm::length(flat(look - dog)) > 0.1f) face = aiYaw(flat(look - dog));
    const float before = m_dogYaw;
    m_dogYaw += angleDelta(m_dogYaw, face) * (1.0f - std::exp(-8.0f * dt));
    m_dogYaw = std::fmod(m_dogYaw + 540.0f, 360.0f) - 180.0f;
    m_turnRate = dt > 0.0f ? angleDelta(before, m_dogYaw) / dt : 0.0f;
    m_lookTarget = look;

    // Feelings settle slowly toward content.
    m_excited = approach(m_excited, 0.2f, 0.25f, dt);
    m_happy = approach(m_happy, 0.55f, 0.02f, dt);
}

std::string PetModule::doing() const {
    if (m_joyTime > 0.0f) return "jumping for joy";
    if (m_petTime > 0.0f) return "enjoying a pat";
    switch (m_board.currentKind(kDog)) {
    case kke::OrderKind::Follow: return "at your side";
    case kke::OrderKind::Stay: return "staying put";
    case kke::OrderKind::Sit: return "sitting";
    case kke::OrderKind::Move: return "on its way";
    case kke::OrderKind::Pet: return "coming for a pat";
    case kke::OrderKind::Fetch: return m_bridge->carrying(kDog) ? "bringing the ball back" : "chasing the ball";
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
    if (act == "rest") return "having a rest";
    if (act == "watch") return "watching";
    if (act == "investigate") return "having a look";
    if (act == "idle" || act.empty()) return "looking around";
    return act;
}

void PetModule::animateDog(float dt) {
    const glm::vec3 dog = positionOf(kDog);
    const glm::vec3 vel = flat(m_rigid->world().characterVelocity(m_dogChar));
    const glm::mat4 body = bodyAt(dog, m_dogYaw);
    // Sitting: the back end down, nose up.
    const glm::mat4 xf = glm::rotate(glm::translate(body, glm::vec3(0.0f, -0.06f * m_sit, 0.0f)), glm::radians(-24.0f * m_sit), glm::vec3(1, 0, 0));
    if (!m_dogModel) return;
    const glm::mat4 modelWorld = glm::scale(xf, glm::vec3(m_dogScale));
    m_models->setTransform(m_dogModel, modelWorld);
    if (!m_dogAnim) return;
    const bool joy = m_joyTime > 0.0f;
    if (joy && m_dogAnim->current() != m_dogJump) m_dogAnim->play(m_dogJump, 0.1f, true);
    if (!joy && m_dogAnim->current() == m_dogJump && m_dogAnim->finished()) m_dogAnim->play(m_dogIdle, 0.2f);
    m_dogAnim->update(dt);
    kke::Pose pose = m_dogAnim->pose();

    // Legs: kke::ProceduralGait plans the steps on the (flat) lawn.
    if (m_gait.valid()) {
        const kke::ProceduralGait::SurfaceQuery ground = [](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            hit = glm::vec3(from.x, 0.0f, from.z);
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
            return true;
        };
        if (!m_gaitReady) {
            m_gait.reset(body, ground);
            m_gaitReady = true;
        }
        m_gait.update(body, vel, m_turnRate, ground, dt);
        kke::applyGait(m_dogRig, pose, m_legChains, m_hips, m_gait, modelWorld, (1.0f - m_sit) * (joy ? 0.0f : 1.0f));
    }
    // Sitting folds the back legs under and straightens the front ones.
    for (size_t i = 0; i < m_legs.size() && m_sit > 0.01f; ++i) {
        if (m_legs[i] < 0) continue;
        auto& up = pose[size_t(m_legs[i])];
        if (i >= 2) {
            up.r = up.r * glm::angleAxis(glm::radians(-55.0f) * m_sit, glm::vec3(1, 0, 0));
            if (m_knees[i] >= 0) pose[size_t(m_knees[i])].r *= glm::angleAxis(glm::radians(95.0f) * m_sit, glm::vec3(1, 0, 0));
        } else {
            up.r = up.r * glm::angleAxis(glm::radians(24.0f) * m_sit, glm::vec3(1, 0, 0));
        }
    }
    // Head: on what it attends to.
    if (m_look.valid()) {
        const glm::vec3 target = glm::vec3(glm::inverse(modelWorld) * glm::vec4(m_lookTarget, 1.0f));
        m_look.apply(m_dogRig, pose, &target, dt, joy ? 0.0f : 1.0f);
    }
    if (std::vector<glm::mat4>* locals = m_models->boneLocals(m_dogModel)) kke::poseToLocals(pose, *locals);
}

// ---------------------------------------------------------------- the player, the balls

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
    if (m_kneel > 0.0f) {
        m_kneel -= dt;
        li = {};
        const glm::vec3 d = flat(positionOf(kDog) - positionOf(kPlayer));
        if (glm::length(d) > 0.1f) m_loco->setFacing(glm::normalize(d));
        m_body->act("Fixing_Kneeling", true, 1.0f, 0.2f);
    } else if (m_body->acting() == "Fixing_Kneeling" || (m_body->actionFinished() && !m_body->acting().empty())) {
        m_body->act("");
    }
    m_loco->update(li, dt);
    const glm::vec3 feet = world.characterPosition(m_playerChar);
    if (feet.y < -20.0f) m_loco->teleport({ 0.0f, 0.1f, 5.0f });
    m_body->update(feet, m_loco->facingYaw(), m_loco->groundSpeed(), dt);
    m_rig.update(dt, feet, [&world](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
        const auto hit = world.raycast(from, dir, maxDist);
        return hit.hit ? hit.distance : maxDist;
    }, m_app->camera());

    // What the player seems to be doing, for the dog.
    kke::IntentSample s;
    s.position = feet;
    s.velocity = world.characterVelocity(m_playerChar);
    s.view = glm::normalize(m_app->camera().target - m_app->camera().position);
    std::vector<kke::IntentCandidate> things;
    for (const Ball& b : m_balls)
        if (b.held == Ball::Held::No) things.push_back({ b.id, b.pos, kke::Relation::Item });
    things.push_back({ kDog, positionOf(kDog), kke::Relation::Own });
    m_intent.update(s, things, dt);
}

void PetModule::updateBalls(float dt) {
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 me = positionOf(kPlayer), dog = positionOf(kDog);
    const glm::vec3 myFwd = m_loco->facing();
    const glm::vec3 dogFwd(std::sin(glm::radians(m_dogYaw)), 0.0f, std::cos(glm::radians(m_dogYaw)));
    for (Ball& b : m_balls) {
        b.thrownAgo += dt;
        glm::quat rot(1.0f, 0.0f, 0.0f, 0.0f);
        switch (b.held) {
        case Ball::Held::No:
            if (b.body != kke::RigidWorld::kNoBody) {
                b.pos = w.position(b.body);
                rot = w.rotation(b.body);
            }
            // Out over the fence: back into the garden.
            if (std::abs(b.pos.x) > 30.0f || std::abs(b.pos.z) > 30.0f || b.pos.y < -5.0f) {
                w.remove(b.body);
                b.body = kke::RigidWorld::kNoBody;
                b.pos = glm::vec3(0.0f, 0.5f, 0.0f);
                release(b, glm::vec3(0.0f));
            }
            break;
        case Ball::Held::Player: b.pos = me + glm::vec3(0.0f, 1.05f, 0.0f) + myFwd * 0.35f; break;
        case Ball::Held::Dog: b.pos = dog + dogFwd * 0.3f + glm::vec3(0.0f, 0.28f, 0.0f); break;
        }
        // The ball the dog just brought: yours again when it's at your feet.
        const bool fetching = m_board.currentKind(kDog) == kke::OrderKind::Fetch && m_board.current(kDog)->target == b.id;
        if (b.held == Ball::Held::No && !fetching && b.thrownAgo > 2.0f && glm::length(flat(b.pos - me)) < 1.3f &&
            std::none_of(m_balls.begin(), m_balls.end(), [](const Ball& o) { return o.held == Ball::Held::Player; })) {
            grab(b);
            b.held = Ball::Held::Player;
        }
        if (b.model) {
            const float scale = kBallRadius / 0.15f; // the pack's ball is 0.3 m across
            glm::mat4 xf = glm::translate(glm::mat4(1.0f), b.pos - glm::vec3(0.0f, kBallRadius, 0.0f)) * glm::mat4_cast(rot);
            m_models->setTransform(b.model, glm::scale(xf, glm::vec3(scale)));
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
        if (in.pressed("interact")) pet();
        // Left mouse throws while the mouse turns the camera; RT always.
        if (in.pressed("fire") && (m_captured || m_cmd.padActive())) throwBall();
    }
    if (m_demo) runDemo(dt);

    updatePlayer(dt);
    updateDog(dt);
    updateBalls(dt);
    animateDog(dt);
    if (m_script) m_script->fireEvents();
    updateHud(dt);
}

void PetModule::updateHud(float dt) {
    const command_kit::CommandInput::Frame& f = m_cmd.frame();
    const kke::UnitOrder* o = m_board.current(kDog);
    auto moodText = [&]() -> std::string {
        if (m_happy > 0.85f) return "Mood: over the moon";
        if (m_excited > 0.6f) return "Mood: excited";
        if (m_happy > 0.6f) return "Mood: happy";
        return "Mood: content";
    };
    std::string ballText = "Ball: on the grass";
    for (const Ball& b : m_balls) {
        if (b.held == Ball::Held::Player) ballText = "Ball: in your hand (throw it)";
        if (b.held == Ball::Held::Dog) ballText = "Ball: in the dog's mouth";
    }
    const kke::Intent& in = m_intent.intent();
    std::string you = std::string("You: ") + kke::intentName(in.kind);
    if (in.thing == kDog) you += " the dog";
    else if (in.thing) you += " a ball";
    m_hud->setLines({ { std::string("Order: ") + (o ? kke::orderLabel(o->kind) : "none"), "#ffcf5c" },
                      { "Doing: " + doing() },
                      { moodText(), m_happy > 0.6f ? "#6fe39a" : "#e8ecf4" },
                      { you, "#aab3cc" },
                      { ballText, "#aab3cc" } });
    const kke::OrderKind k = o ? o->kind : kke::OrderKind::None;
    m_hud->setButtons({ { "👋", "Come", "1", k == kke::OrderKind::Follow },
                        { "🐕", "Sit", "2", k == kke::OrderKind::Sit },
                        { "✋", "Stay", "3", k == kke::OrderKind::Stay },
                        { "🎾", "Fetch", "4", k == kke::OrderKind::Fetch },
                        { "👇", "Drop it", "5", k == kke::OrderKind::Drop },
                        { "❤", "Pet", "E", k == kke::OrderKind::Pet },
                        { "🥏", "Throw", "click", false } });
    m_hud->setHint(m_cmd.padActive() ? "Left stick move · RB order at the ring · hold LB: order wheel · D-pad quick orders · Y pet · RT throw"
                   : m_captured      ? "WASD move · right click: order at the ring · hold Tab: order wheel · 1-5 orders · E pet · click throw · Esc free the mouse"
                                     : "Click or tap: order there (tap the dog to pet it) · hold: order wheel · Esc: look with the mouse");
    m_hud->update(f, dt);
}

void PetModule::render(const kke::RenderContext& ctx) {
    m_scenery->render(ctx);
    m_body->render(ctx);
    if (m_dogBlock) m_dogBlock->draw(ctx, bodyAt(positionOf(kDog), m_dogYaw + 180.0f), 0.0f, 0.7f);
    if (m_ballBlock)
        for (const Ball& b : m_balls) m_ballBlock->draw(ctx, glm::translate(glm::mat4(1.0f), b.pos), 0.0f, 0.5f);
}

void PetModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_scenery->renderShadow(ctx);
    m_body->renderShadow(ctx);
    if (m_dogBlock)
        m_dogBlock->drawShadow(ctx, bodyAt(positionOf(kDog), m_dogYaw + 180.0f));
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
            // Walk to the ball by the doghouse side and pick it up.
            const glm::vec3 ballPos = m_balls[0].pos;
            m_loco->teleport(ballPos + glm::vec3(0.6f, -ballPos.y + 0.05f, 0.6f));
            throwBall();
            next("picked up a ball");
        }
        break;
    case 1:
        if (m_demoTimer > 1.0f) {
            m_rig.yaw = 0.0f; // throw toward -Z
            throwBall();
            next("threw the ball");
        }
        break;
    case 2:
        if (m_board.currentKind(kDog) != kke::OrderKind::Fetch && m_demoTimer > 1.0f) {
            kke::log::get(name())->info("demo: fetch finished after {:.1f} s, the ball is {:.1f} m from the player", m_demoTimer,
                                        glm::length(flat(m_balls[0].pos - me)));
            next("fetched");
        } else if (m_demoTimer > 20.0f) {
            next("FETCH TIMED OUT");
        }
        break;
    case 3:
        give(kke::OrderKind::Sit);
        next("sit");
        break;
    case 4:
        if (m_demoTimer > 2.0f) {
            kke::log::get(name())->info("demo: sitting {:.2f} (1 = fully)", m_sit);
            give(kke::OrderKind::Stay);
            m_demoWant = dog;
            next("stay, and the player walks away");
        }
        break;
    case 5:
        m_loco->teleport(me + glm::vec3(2.0f * dt, 0.0f, 0.0f)); // walks off
        if (m_demoTimer > 3.0f) {
            kke::log::get(name())->info("demo: the dog stayed within {:.2f} m of its spot", glm::length(flat(dog - m_demoWant)));
            give(kke::OrderKind::Follow);
            m_demoBlocked = 0;
            next("come, and the player runs");
        }
        break;
    case 6:
        m_loco->teleport(me + glm::vec3(0.0f, 0.0f, -3.0f * dt)); // and runs, the dog at its heels
        if (kke::inLeadersWay(me, glm::vec3(0.0f, 0.0f, -3.0f), dog)) ++m_demoBlocked;
        if (m_demoTimer > 3.5f) {
            kke::log::get(name())->info("demo: following, {:.1f} m from the player, in the way on {} frames", glm::length(flat(dog - me)),
                                        m_demoBlocked);
            pet();
            next("pet");
        }
        break;
    case 7:
        if (m_board.currentKind(kDog) != kke::OrderKind::Pet) {
            kke::log::get(name())->info("demo: petted, happiness {:.2f}", m_happy);
            const glm::vec3 there(4.0f, 0.0f, -4.0f);
            give(kke::OrderKind::Move, 0, &there);
            next("go there");
        } else if (m_demoTimer > 15.0f) {
            next("PET TIMED OUT");
        }
        break;
    case 8:
        if (m_board.currentKind(kDog) == kke::OrderKind::Stay) {
            kke::log::get(name())->info("demo: arrived {:.2f} m from the spot and waits there",
                                        glm::length(flat(dog - glm::vec3(4.0f, 0.0f, -4.0f))));
            next("done");
        } else if (m_demoTimer > 15.0f) {
            next("GO THERE TIMED OUT");
        }
        break;
    case 9:
        if (m_demoTimer > 1.5f) {
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
