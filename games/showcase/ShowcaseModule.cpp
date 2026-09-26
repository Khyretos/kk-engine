#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/FracturePattern.h"
#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <typeindex>

namespace kke_showcase {

namespace {

// Movement speeds (m/s) and the blend-space points they line up with: the
// animations' own foot speeds, so feet don't slide.
constexpr float kWalkSpeed = 1.6f, kJogSpeed = 3.6f, kSprintSpeed = 6.2f, kCrouchSpeed = 1.4f;

void appendBox(const glm::mat4& m, const glm::vec3& half, const glm::vec3& color, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 n[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    const glm::mat3 nm = glm::mat3(m);
    for (const glm::vec3& normal : n) {
        glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        glm::vec3 w = glm::cross(normal, u);
        uint32_t base = static_cast<uint32_t>(v.size());
        // Slightly darker sides: shape reads better without textures.
        glm::vec3 c = color * (normal.y > 0.5f ? 1.0f : normal.y < -0.5f ? 0.6f : 0.85f);
        for (glm::vec2 k : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) }) {
            glm::vec3 p = (normal + u * k.x + w * k.y) * half;
            v.push_back({ glm::vec3(m * glm::vec4(p, 1.0f)), c, glm::normalize(nm * normal), glm::vec2(0.0f) });
        }
        // Counter-clockwise from outside.
        glm::vec3 a = v[base].position, b = v[base + 1].position, cc = v[base + 2].position;
        if (glm::dot(glm::cross(b - a, cc - a), nm * normal) >= 0.0f) idx.insert(idx.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        else idx.insert(idx.end(), { base, base + 2, base + 1, base, base + 3, base + 2 });
    }
}

} // namespace

std::vector<kke::ModuleDependency> ShowcaseModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "collision, crates and the character controller" },
             { std::type_index(typeid(kke::ModelModule)), true, "draws the animated character" } };
}

void ShowcaseModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_models = app.getModule<kke::ModelModule>();
#if KKE_ENABLE_FEMFX
    m_femfx = app.getModule<kke::PhysicsModule>();
#endif
    m_level = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_capsule = std::make_unique<kke::DynamicMeshRenderer>(app);
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> i;
        for (glm::vec3 c : { glm::vec3(0.62f, 0.45f, 0.28f), glm::vec3(0.5f, 0.36f, 0.22f), glm::vec3(0.3f, 0.6f, 0.4f), glm::vec3(0.75f, 0.75f, 0.8f) }) {
            v.clear();
            i.clear();
            appendBox(glm::mat4(1.0f), glm::vec3(1.0f), c, v, i);
            m_cubes.push_back(std::make_unique<kke::DynamicMeshRenderer>(app));
            m_cubes.back()->upload(v, i);
        }
        v.clear();
        i.clear();
        // Fallback body when the animation library isn't installed.
        appendBox(glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.9f, 0)), glm::vec3(0.28f, 0.9f, 0.2f), glm::vec3(0.2f, 0.45f, 0.9f), v, i);
        m_capsule->upload(v, i);
    }
    buildLevel();
    spawnCrates();
    spawnBreakables();
    setupPlayer();
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.yaw = 0.0f;
    m_rig.pitch = -12.0f;
    app.window().setQuitOnEscape(false);
    m_status = "Click the view to control the character (Esc releases the mouse)";
}

void ShowcaseModule::addStaticBox(const Box& b, std::vector<kke::Vertex>& v, std::vector<uint32_t>& i) {
    glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), b.center), glm::radians(b.yaw), glm::vec3(0, 1, 0));
    appendBox(m, b.half, b.color, v, i);
    kke::RigidWorld::BodyDesc d;
    d.shape = kke::RigidWorld::Shape::Box;
    d.motion = kke::RigidWorld::Motion::Static;
    d.halfExtents = b.half;
    d.position = b.center;
    d.rotation = glm::angleAxis(glm::radians(b.yaw), glm::vec3(0, 1, 0));
    d.material = 1; // "stone" for future impact sounds
    m_rigid->world().add(d);
}

// A small course: floor, walls, stairs, a ramp, a high ledge, a moving
// platform, the crate pile and a breaking yard.
void ShowcaseModule::buildLevel() {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    const glm::vec3 floor(0.36f, 0.4f, 0.36f), wall(0.55f, 0.52f, 0.48f), accent(0.85f, 0.55f, 0.25f), ramp(0.45f, 0.55f, 0.7f);
    addStaticBox({ { 0, -0.25f, 0 }, { 30, 0.25f, 30 }, floor }, v, idx);
    // Perimeter walls.
    addStaticBox({ { 0, 1.5f, -30 }, { 30, 1.5f, 0.3f }, wall }, v, idx);
    addStaticBox({ { 0, 1.5f, 30 }, { 30, 1.5f, 0.3f }, wall }, v, idx);
    addStaticBox({ { -30, 1.5f, 0 }, { 0.3f, 1.5f, 30 }, wall }, v, idx);
    addStaticBox({ { 30, 1.5f, 0 }, { 0.3f, 1.5f, 30 }, wall }, v, idx);
    // Stairs up to a 2 m platform (0.25 m steps: the controller climbs them).
    for (int s = 0; s < 8; ++s)
        addStaticBox({ { -8.0f, 0.125f + s * 0.25f, -2.0f - s * 0.4f }, { 1.5f, 0.125f + s * 0.25f, 0.2f }, accent }, v, idx);
    addStaticBox({ { -8.0f, 1.0f, -7.0f }, { 3.0f, 1.0f, 2.0f }, wall }, v, idx);
    // A ramp (about 20 degrees) down the other side.
    {
        Box r{ { -3.2f, 0.95f, -7.0f }, { 2.2f, 0.12f, 1.8f }, ramp };
        glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), r.center), glm::radians(-24.0f), glm::vec3(0, 0, 1));
        appendBox(m, r.half, r.color, v, idx);
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = r.half;
        d.position = r.center;
        d.rotation = glm::angleAxis(glm::radians(-24.0f), glm::vec3(0, 0, 1));
        m_rigid->world().add(d);
    }
    // A too-steep slope (50+ degrees): you slide off it.
    {
        glm::vec3 c(8.0f, 1.2f, -10.0f), h(2.0f, 0.12f, 2.0f);
        glm::mat4 m = glm::rotate(glm::translate(glm::mat4(1.0f), c), glm::radians(55.0f), glm::vec3(1, 0, 0));
        appendBox(m, h, glm::vec3(0.7f, 0.3f, 0.3f), v, idx);
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = h;
        d.position = c;
        d.rotation = glm::angleAxis(glm::radians(55.0f), glm::vec3(1, 0, 0));
        m_rigid->world().add(d);
    }
    // Pillars to walk around and hide the camera behind (spring arm).
    for (int k = 0; k < 4; ++k) addStaticBox({ { -14.0f + k * 2.5f, 1.5f, -14.0f }, { 0.4f, 1.5f, 0.4f }, wall }, v, idx);
    // Breaking yard floor marker.
    addStaticBox({ { 14.0f, 0.01f, -6.0f }, { 5.5f, 0.01f, 4.5f }, glm::vec3(0.3f, 0.3f, 0.25f) }, v, idx);
    m_level->upload(v, idx);

    // Moving platform (kinematic): stand on it, it carries you.
    kke::RigidWorld::BodyDesc p;
    p.motion = kke::RigidWorld::Motion::Kinematic;
    p.halfExtents = m_platformHalf;
    p.position = glm::vec3(-14.0f, 0.5f, 6.0f);
    m_platform = m_rigid->world().add(p);
}

void ShowcaseModule::spawnCrates() {
    for (const Crate& c : m_crates) m_rigid->world().remove(c.body);
    m_crates.clear();
    // A pyramid of crates, and a few big light boxes to push around.
    int n = 0;
    for (int row = 0; row < 5; ++row)
        for (int k = 0; k < 5 - row; ++k) {
            kke::RigidWorld::BodyDesc d;
            d.halfExtents = glm::vec3(0.3f);
            d.density = 250.0f; // wood
            d.position = glm::vec3(4.0f + (k + row * 0.5f) * 0.62f, 0.3f + row * 0.6f, -3.0f);
            d.material = 2;
            m_crates.push_back({ m_rigid->world().add(d), d.halfExtents, n++ % 2 });
        }
    for (int k = 0; k < 3; ++k) {
        kke::RigidWorld::BodyDesc d;
        d.halfExtents = glm::vec3(0.6f);
        d.density = 60.0f;
        d.position = glm::vec3(-2.0f + k * 1.6f, 0.6f, 10.0f);
        m_crates.push_back({ m_rigid->world().add(d), d.halfExtents, 2 });
    }
}

void ShowcaseModule::spawnBreakables() {
#if KKE_ENABLE_FEMFX
    if (!m_femfx) return;
    // The breaking yard: glass on two supports, a plank bridge, a stone
    // wall (FEMFX, Voronoi pieces; shoot them with F). Supports are Jolt
    // static boxes too, so the player collides with them.
    const glm::vec3 yard(14.0f, 0.0f, -6.0f);
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
    auto block = [&](glm::vec3 size, glm::vec3 at) {
        m_femfx->spawnTetMesh(kke::PhysicsModule::buildGridBox(2, 2, 2, size.x, size.y, size.z), at, support);
        kke::RigidWorld::BodyDesc d;
        d.motion = kke::RigidWorld::Motion::Static;
        d.halfExtents = size * 0.5f;
        d.position = at;
        m_rigid->world().add(d);
    };
    block({ 0.3f, 0.5f, 1.2f }, yard + glm::vec3(-0.9f, 0.25f, 0.0f));
    block({ 0.3f, 0.5f, 1.2f }, yard + glm::vec3(0.9f, 0.25f, 0.0f));
    const glm::vec3 hit(0.0f);
    m_femfx->spawnPatternedBox({ 12, 1, 8 }, { 2.1f, 0.05f, 1.2f }, yard + glm::vec3(0, 0.525f, 0), glass, static_cast<int>(kke::FracturePattern::Radial), 0.45f, 0,
                               glm::vec3(0.0f), 3.0f, &hit);
    block({ 0.3f, 0.5f, 0.6f }, yard + glm::vec3(-1.05f, 0.25f, 3.0f));
    block({ 0.3f, 0.5f, 0.6f }, yard + glm::vec3(1.05f, 0.25f, 3.0f));
    m_femfx->spawnPatternedBox({ 16, 1, 2 }, { 2.4f, 0.12f, 0.3f }, yard + glm::vec3(0, 0.561f, 3.0f), wood, static_cast<int>(kke::FracturePattern::Splinters), 0.35f, 0,
                               glm::vec3(0.0f), 3.0f);
    m_femfx->spawnPatternedBox({ 8, 6, 2 }, { 1.4f, 1.0f, 0.25f }, yard + glm::vec3(3.5f, 0.501f, -2.0f), stone, static_cast<int>(kke::FracturePattern::Voronoi), 0.3f, 3,
                               glm::vec3(0.0f), 3.0f);
#endif
}

void ShowcaseModule::setupPlayer() {
    kke::RigidWorld::CharacterDesc cd;
    cd.position = m_spawn;
    m_player = m_rigid->world().addCharacter(cd);
    m_facing = 180.0f;

    // Quaternius' Universal Animation Library (CC0): a mannequin with 43
    // clips. Found in assets/animations/ (or KKE_ANIMATIONS_DIR); without
    // it the character is a box.
    const char* base = SDL_GetBasePath();
    std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    std::string file = dir.empty() ? std::string() : (std::filesystem::path(dir) / "UAL1_Standard.fbx").string();
    if (file.empty() || !std::filesystem::exists(file)) {
        kke::log::get(name())->warn("animation library not found (assets/animations/UAL1_Standard.fbx): the character is a box");
        return;
    }
    m_charModel = m_models->load(file);
    const kke::ModelData* d = m_charModel ? m_models->model(m_charModel) : nullptr;
    if (!d || d->animations.empty()) return;
    m_charInstance = m_models->spawn(m_charModel, glm::mat4(1.0f));
    m_models->setOverlayEnabled(m_charInstance, false);
    m_animSet = std::make_unique<kke::AnimationSet>(*d);
    m_anim = std::make_unique<kke::Animator>(*m_animSet);
    const kke::AnimationSet& s = *m_animSet;
    m_stMove = m_anim->addBlendState("move", { { { s.find("|Idle_Loop"), 0.0f },
                                                 { s.find("|Walk_Loop"), kWalkSpeed },
                                                 { s.find("Jog_Fwd_Loop"), kJogSpeed },
                                                 { s.find("Sprint_Loop"), kSprintSpeed } } });
    m_stCrouch = m_anim->addBlendState("crouch", { { { s.find("Crouch_Idle_Loop"), 0.0f }, { s.find("Crouch_Fwd_Loop"), kCrouchSpeed } } });
    m_stJump = m_anim->addClipState("jump", s.find("Jump_Start"), false, 2.0f);
    m_stFall = m_anim->addClipState("fall", s.find("Jump_Loop"), true);
    m_stLand = m_anim->addClipState("land", s.find("Jump_Land"), false, 1.8f);
    m_anim->play(m_stMove, 0.0f);
    kke::log::get(name())->info("character: {} bones, {} clips", d->bones.size(), d->animations.size());
}

void ShowcaseModule::setCaptured(bool on) {
    m_captured = on;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), on);
}

void ShowcaseModule::onEvent(const SDL_Event& e) {
    ImGuiIO& io = ImGui::GetIO();
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && !io.WantCaptureMouse && e.button.button == SDL_BUTTON_LEFT) {
        setCaptured(true);
        return;
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION && m_captured) {
        const float sens = 0.12f;
        m_rig.addLook(e.motion.xrel * sens, -e.motion.yrel * sens);
    }
    if (e.type == SDL_EVENT_MOUSE_WHEEL && m_captured)
        m_rig.settings.armLength = std::clamp(m_rig.settings.armLength - e.wheel.y * 0.4f, 1.5f, 10.0f);
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && m_captured && e.button.button == SDL_BUTTON_LEFT) shoot();
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && m_captured && e.button.button == SDL_BUTTON_RIGHT) forcePush();
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat || io.WantTextInput) return;
    switch (e.key.key) {
    case SDLK_ESCAPE: setCaptured(false); break;
    case SDLK_SPACE: m_jumpQueued = true; break;
    case SDLK_V:
        m_rig.mode = m_rig.mode == kke::CameraRig::Mode::ThirdPerson ? kke::CameraRig::Mode::FirstPerson : kke::CameraRig::Mode::ThirdPerson;
        break;
    case SDLK_C: m_crouch = !m_crouch; break;
    case SDLK_F: shoot(); break;
    case SDLK_E: forcePush(); break;
    case SDLK_R:
        spawnCrates();
        m_rigid->world().teleportCharacter(m_player, m_spawn);
        break;
    case SDLK_F1:
        m_showPanels = !m_showPanels;
        for (kke::Module* p : m_panels) p->setUiVisible(m_showPanels);
        break;
    default: break;
    }
}

// Shoots a heavy FEMFX ball from the camera (breaks the yard's glass,
// wood and stone) and knocks any Jolt body the view points at.
void ShowcaseModule::shoot() {
    const kke::Camera& cam = m_app->camera();
    glm::vec3 dir = glm::normalize(cam.target - cam.position);
#if KKE_ENABLE_FEMFX
    if (m_femfx) {
        kke::Material iron;
        iron.density = 7800.0f; iron.stiffness = 2.0e7f; iron.poissonsRatio = 0.3f;
        iron.fractureStressThreshold = 1.0e12f; iron.metallic = 0.9f; iron.roughness = 0.35f; iron.textureId = 2;
        m_femfx->spawnFracturableTetMesh(kke::PhysicsModule::buildSphere(3, 0.15f), cam.position + dir * 1.0f, iron, dir * 22.0f);
    }
#endif
    forcePush();
}

void ShowcaseModule::forcePush() {
    const kke::Camera& cam = m_app->camera();
    glm::vec3 dir = glm::normalize(cam.target - cam.position);
    auto hit = m_rigid->world().raycast(cam.position, dir, 30.0f);
    if (!hit.hit) return;
    for (const Crate& c : m_crates)
        if (c.body == hit.body) m_rigid->world().addImpulse(c.body, dir * 60.0f, hit.point);
}

void ShowcaseModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    // Platform: back and forth, up and down.
    m_platformTime += ctx.fixedDt;
    glm::vec3 p(-14.0f + 5.0f * std::sin(m_platformTime * 0.5f), 0.6f + 1.2f * (0.5f + 0.5f * std::sin(m_platformTime * 0.35f)), 6.0f);
    m_rigid->world().moveKinematic(m_platform, p, glm::quat(1, 0, 0, 0), ctx.fixedDt);
}

void ShowcaseModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    m_fps = m_fps * 0.95f + (dt > 0.0f ? 1.0f / dt : 0.0f) * 0.05f;
    kke::RigidWorld& w = m_rigid->world();

    // Input -> desired velocity, relative to the camera.
    glm::vec3 move(0.0f);
    if (m_captured) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_W]) move += m_rig.forward();
        if (keys[SDL_SCANCODE_S]) move -= m_rig.forward();
        if (keys[SDL_SCANCODE_D]) move += m_rig.right();
        if (keys[SDL_SCANCODE_A]) move -= m_rig.right();
        m_sprint = keys[SDL_SCANCODE_LSHIFT];
        m_walk = keys[SDL_SCANCODE_LALT];
    }
    float speed = m_crouch ? kCrouchSpeed : m_sprint ? kSprintSpeed : m_walk ? kWalkSpeed : kJogSpeed;
    if (glm::length(move) > 1e-3f) move = glm::normalize(move) * speed;
    kke::RigidWorld::CharacterInput in;
    in.move = move;
    in.jump = m_jumpQueued && w.characterOnGround(m_player) && !m_crouch;
    in.jumpSpeed = 5.2f;
    if (in.jump && m_anim) m_anim->play(m_stJump, 0.08f, true);
    m_jumpQueued = false;
    w.setCharacterInput(m_player, in);

    const glm::vec3 feet = w.characterPosition(m_player);
    const glm::vec3 vel = w.characterVelocity(m_player);
    const float groundSpeed = glm::length(glm::vec2(vel.x, vel.z));
    // Face where we're going (third person), or where we look (first).
    if (m_rig.mode == kke::CameraRig::Mode::FirstPerson) m_facing = m_rig.yaw;
    else if (groundSpeed > 0.3f) {
        float target = glm::degrees(std::atan2(vel.x, -vel.z));
        float diff = std::remainder(target - m_facing, 360.0f);
        m_facing += diff * std::min(1.0f, 12.0f * dt);
    }
    if (feet.y < -20.0f) w.teleportCharacter(m_player, m_spawn); // fell out of the world

    updateAnimation(dt, groundSpeed, w.characterOnGround(m_player));
    if (m_charInstance) {
        // The mannequin faces -Z, like the rig at yaw 0.
        glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-m_facing), glm::vec3(0, 1, 0));
        m_models->setTransform(m_charInstance, t);
        m_models->setVisible(m_charInstance, m_rig.mode != kke::CameraRig::Mode::FirstPerson);
    }

    // Camera (collides with the level through Jolt ray casts).
    m_rig.update(dt, feet, [&w](const glm::vec3& from, const glm::vec3& d, float maxD) {
        auto h = w.raycast(from, d, maxD);
        return h.hit ? h.distance : maxD;
    }, m_app->camera());

    // Lighting from the panel.
    kke::Light& sun = m_app->lighting().lights[0];
    const float az = glm::radians(m_sunAzimuth), el = glm::radians(m_sunElevation);
    sun.enabled = true;
    sun.isDirectional = true;
    sun.direction = -glm::normalize(glm::vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)));
    sun.color = m_sunColor;
    sun.intensity = m_sunIntensity;
    m_app->lighting().ambientColor = glm::vec3(m_ambient);
}

void ShowcaseModule::updateAnimation(float dt, float speed, bool grounded) {
    if (!m_anim) return;
    const int cur = m_anim->current();
    if (!grounded) {
        m_airTime += dt;
        // Walking off a ledge (not a jump): fall after a moment.
        if (cur == m_stJump && m_anim->finished()) m_anim->play(m_stFall, 0.15f);
        else if (cur != m_stJump && cur != m_stFall && m_airTime > 0.2f) m_anim->play(m_stFall, 0.2f);
    } else {
        if ((cur == m_stFall || (cur == m_stJump && m_airTime > 0.15f)) && m_airTime > 0.35f) m_anim->play(m_stLand, 0.06f);
        m_airTime = 0.0f;
        const int ground = m_crouch ? m_stCrouch : m_stMove;
        const bool landing = m_anim->current() == m_stLand && !m_anim->finished() && speed < 1.0f;
        const bool jumping = m_anim->current() == m_stJump && m_anim->stateTime() < 0.2f;
        if (!landing && !jumping && m_anim->current() != ground) m_anim->play(ground, 0.2f);
    }
    m_anim->setParameter(speed);
    m_anim->update(dt);
    if (std::vector<glm::mat4>* locals = m_models->boneLocals(m_charInstance)) kke::poseToLocals(m_anim->pose(), *locals);
}

void ShowcaseModule::render(const kke::RenderContext& ctx) {
    m_level->draw(ctx, glm::mat4(1.0f), 0.0f, 0.85f);
    kke::RigidWorld& w = m_rigid->world();
    for (const Crate& c : m_crates) {
        glm::mat4 t = glm::scale(w.transform(c.body), c.half);
        m_cubes[c.cube]->draw(ctx, t, 0.0f, 0.7f);
    }
    m_cubes[3]->draw(ctx, glm::scale(w.transform(m_platform), m_platformHalf), 0.3f, 0.4f);
    if (!m_charInstance && m_rig.mode != kke::CameraRig::Mode::FirstPerson) {
        glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), w.characterPosition(m_player)), glm::radians(-m_facing), glm::vec3(0, 1, 0));
        m_capsule->draw(ctx, t, 0.0f, 0.6f);
    }
}

void ShowcaseModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_level->drawShadow(ctx);
    kke::RigidWorld& w = m_rigid->world();
    for (const Crate& c : m_crates) m_cubes[c.cube]->drawShadow(ctx, glm::scale(w.transform(c.body), c.half));
    m_cubes[3]->drawShadow(ctx, glm::scale(w.transform(m_platform), m_platformHalf));
    if (!m_charInstance) m_capsule->drawShadow(ctx, glm::translate(glm::mat4(1.0f), w.characterPosition(m_player)));
}

void ShowcaseModule::renderUi() {
    const float s = ImGui::GetFontSize() / 13.0f;
    ImGui::SetNextWindowPos(ImVec2(10 * s, 10 * s), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300 * s, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("KKE Showcase");
    ImGui::TextWrapped("%s", m_status.c_str());
    ImGui::Text("%.0f FPS", m_fps);
    kke::RigidWorld& w = m_rigid->world();
    glm::vec3 v = w.characterVelocity(m_player);
    ImGui::Text("Speed %.1f m/s  %s", glm::length(glm::vec2(v.x, v.z)), w.characterOnGround(m_player) ? "on ground" : "in the air");
    ImGui::Text("Rigid bodies %zu (%zu awake), %.2f ms", w.bodyCount(), w.activeBodyCount(), w.lastStepMs());
    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("WASD move, Shift sprint, Alt walk, Space jump,\nC crouch, mouse look, wheel zoom, V first/third person\n"
                               "Left click / F shoot (breaks the yard), right click / E push\n"
                               "R reset crates + player, F1 engine panels, Esc mouse");
    }
    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::SliderFloat("Sun direction", &m_sunAzimuth, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("Sun height", &m_sunElevation, 2.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Sun strength", &m_sunIntensity, 0.0f, 3.0f);
        ImGui::ColorEdit3("Sun colour", &m_sunColor.x);
        ImGui::SliderFloat("Ambient", &m_ambient, 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Camera")) {
        bool third = m_rig.mode == kke::CameraRig::Mode::ThirdPerson;
        if (ImGui::Checkbox("Third person (V)", &third)) m_rig.mode = third ? kke::CameraRig::Mode::ThirdPerson : kke::CameraRig::Mode::FirstPerson;
        ImGui::SliderFloat("Arm length", &m_rig.settings.armLength, 1.5f, 10.0f);
        ImGui::SliderFloat("Shoulder", &m_rig.settings.shoulderOffset, -1.0f, 1.0f);
        ImGui::SliderFloat("Lag", &m_rig.settings.positionLag, 0.0f, 30.0f);
        ImGui::SliderFloat("Field of view", &m_rig.settings.fovDegrees, 40.0f, 100.0f);
    }
    ImGui::End();
}

} // namespace kke_showcase
