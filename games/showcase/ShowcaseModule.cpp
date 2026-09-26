#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/FracturePattern.h"
#include "kke/Log.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/modules/PhysicsBridgeModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/SettingsModule.h"
#if KKE_ENABLE_NET
#include "kke/modules/NetModule.h"
#include "kke/net/BitStream.h"
#endif
#if KKE_ENABLE_FEMFX
#include "kke/modules/PhysicsModule.h"
#endif

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <typeindex>

namespace kke_showcase {

namespace {

// Blend-space points: the animations' own foot speeds, which are also
// kke::Locomotion's default walk/run/sprint/crouch speeds, so feet don't slide.
constexpr float kWalkSpeed = 1.6f, kJogSpeed = 3.6f, kSprintSpeed = 6.2f, kCrouchSpeed = 1.4f;
// UAL2 clips the demo plays: vault and climbs (in place, lift removed).
constexpr const char* kTraversalClips[] = { "SafetyVault", "ClimbUp_1m", "ClimbUp_2m" };
// The breaking yard (glass, plank, stone wall).
const glm::vec3 kYard(14.0f, 0.0f, -6.0f);
// Climbs up walls this high (m) or more take the 2 m clip.
constexpr float kClimbHighFrom = 1.6f;

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

#if KKE_ENABLE_NET
// Game events (NetModule::sendEvent), serialized the docs/NETWORKING.md way:
// one function per message for both directions.
enum EventKind : uint16_t { kEventShoot = 1, kEventPush = 2, kEventReset = 3 };
struct ShotEvent { glm::vec3 from{0.0f}, dir{0.0f, 0.0f, -1.0f}; };
struct PushEvent { uint16_t body = 0; glm::vec3 dir{0.0f}, point{0.0f}; };
template <typename Stream> bool serialize(Stream& s, ShotEvent& e) {
    s.vec3(e.from, glm::vec3(-4096.0f, -512.0f, -4096.0f), glm::vec3(4096.0f, 1536.0f, 4096.0f), 1.0f / 256.0f);
    s.vec3(e.dir, 1.0f, 1.0f / 2048.0f);
    return s.ok();
}
template <typename Stream> bool serialize(Stream& s, PushEvent& e) {
    s.integer(e.body, 0, 65535);
    s.vec3(e.dir, 1.0f, 1.0f / 2048.0f);
    s.vec3(e.point, glm::vec3(-4096.0f, -512.0f, -4096.0f), glm::vec3(4096.0f, 1536.0f, 4096.0f), 1.0f / 256.0f);
    return s.ok();
}
template <typename T> std::vector<uint8_t> pack(T value) {
    std::vector<uint8_t> out;
    {
        kke::net::WriteStream w(out);
        serialize(w, value);
    }
    return out;
}
template <typename T> bool unpack(const std::vector<uint8_t>& data, T& value) {
    kke::net::ReadStream r(data.data(), data.size());
    return serialize(r, value) && r.ok();
}
#endif

// Showcase bits in NetPlayerState::flags (kPlayerTeleported is the engine's).
constexpr uint8_t kFlagCrouch = 0x01;

} // namespace

std::vector<kke::ModuleDependency> ShowcaseModule::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "collision, crates and the character controller" },
             { std::type_index(typeid(kke::InputModule)), true, "actions: keyboard/mouse, controllers, rebinding" },
             { std::type_index(typeid(kke::ModelModule)), true, "draws the animated character" },
#if KKE_ENABLE_NET
             { std::type_index(typeid(kke::NetModule)), false, "multiplayer: joins before the crates spawn, so they follow the host" },
#endif
    };
}

void ShowcaseModule::init(kke::Application& app) {
    m_app = &app;
    m_rigid = app.getModule<kke::RigidBodyModule>();
    m_models = app.getModule<kke::ModelModule>();
    m_input = app.getModule<kke::InputModule>();
#if KKE_ENABLE_NET
    m_net = app.getModule<kke::NetModule>();
    if (m_net) {
        m_net->onEvent = [this](const kke::net::GameEventMsg& e) { onNetEvent(e.kind, e.fromPlayer, e.payload); };
        m_net->onCorrection = [this](const glm::vec3& p) { m_loco->teleport(p); };
    }
#endif
    // The showcase is a developer demo: its ImGui panel is the UI, so the
    // "debug overlay" setting stays on here.
    if (kke::SettingsModule* sm = app.getModule<kke::SettingsModule>()) {
        sm->settings().graphics.showDebugOverlay = true;
        sm->apply();
    }
    {
        kke::InputMap& in = m_input->map(0);
        kke::InputModule::defineCharacterActions(in);
        in.defineAction({ "reset", "Reset crates + player", "Showcase", "game" });
        in.defineAction({ "panels", "Engine panels", "Showcase", "game" });
        in.addBinding(kke::InputModule::bind("reset", kke::InputModule::key(SDL_SCANCODE_R)));
        in.addBinding(kke::InputModule::bind("reset", kke::InputModule::pad(SDL_GAMEPAD_BUTTON_START)));
        in.addBinding(kke::InputModule::bind("panels", kke::InputModule::key(SDL_SCANCODE_F1)));
        in.addBinding(kke::InputModule::bind("panels", kke::InputModule::pad(SDL_GAMEPAD_BUTTON_BACK)));
        // Left-click shoots, but not the click that grabs the mouse (see onEvent).
        m_input->commitDefaults();
        if (const char* lefty = std::getenv("KKE_LEFT_HANDED"); lefty && *lefty == '1') kke::InputModule::mirrorKeyboard(in);
    }
#if KKE_ENABLE_FEMFX
    m_femfx = app.getModule<kke::PhysicsModule>();
#endif
    m_level = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_capsule = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_crateBatch = std::make_unique<kke::DynamicMeshRenderer>(app);
    {
        std::vector<kke::Vertex> v;
        std::vector<uint32_t> i;
        for (glm::vec3 c : { glm::vec3(0.62f, 0.45f, 0.28f), glm::vec3(0.5f, 0.36f, 0.22f), glm::vec3(0.3f, 0.6f, 0.4f), glm::vec3(0.75f, 0.75f, 0.8f) }) {
            v.clear();
            i.clear();
            appendBox(glm::mat4(1.0f), glm::vec3(1.0f), c, v, i);
            m_cubeColors.push_back(c);
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
    findScenes();
    // KKE_SCENE=town_block (or a path): start in that scene.
    if (const char* want = std::getenv("KKE_SCENE"); want && *want)
        for (size_t k = 0; k < m_scenes.size(); ++k)
            if (m_scenes[k].path.find(want) != std::string::npos) visitScene(k);
    if (const char* a = std::getenv("KKE_DEMO_AUTOPILOT"); a && *a && *a != '0') {
        // In a scene (KKE_SCENE): from its spawn toward -Z. Otherwise the
        // course's parkour lane.
        m_autopilot = true;
        if (m_autopilotStart == glm::vec3(0.0f)) {
            m_autopilotStart = glm::vec3(20.0f, 0.05f, 28.0f);
            m_loco->teleport(m_autopilotStart);
        }
        m_rig.yaw = 0.0f; // looking down the lane (-Z)
        m_status = "Autopilot";
    }
    // KKE_START_AT=x,y,z[,facing yaw in degrees]: start there (screenshots
    // of one spot, e.g. "-3.2,1.4,-7,90" stands on the ramp facing down it).
    if (const char* at = std::getenv("KKE_START_AT"); at && *at) {
        float x = 0, y = 0, z = 0, yaw = 0;
        if (std::sscanf(at, "%f,%f,%f,%f", &x, &y, &z, &yaw) >= 3) {
            m_loco->teleport(glm::vec3(x, y, z));
            m_loco->setFacing(glm::vec3(std::sin(glm::radians(yaw)), 0.0f, -std::cos(glm::radians(yaw))));
            m_rig.yaw = yaw; // the camera looks the same way
        }
    }
    if (const char* h = std::getenv("KKE_DEMO_HANG"); h && *h && *h != '0') {
        m_demoHang = 0.0f;
        m_loco->teleport(glm::vec3(17.2f, 0.05f, 12.0f));
        m_loco->setFacing(glm::vec3(-1, 0, 0));
        m_rig.yaw = -90.0f;
        m_rig.pitch = 8.0f;
    }
    if (const char* sp = std::getenv("KKE_SPLIT"); sp && *sp) setLocalPlayers(std::atoi(sp));
    if (const char* pip = std::getenv("KKE_OVERHEAD"); pip && *pip && *pip != '0') m_overhead = true;
    if (const char* b = std::getenv("KKE_DEMO_BRIDGE"); b && *b && *b != '0') {
        m_demoBridge = 0.0f;
        m_loco->teleport(kYard + glm::vec3(0.0f, 0.05f, 3.2f));
        m_rig.yaw = 0.0f;
        m_rig.pitch = -20.0f;
    }
    if (const char* b = std::getenv("KKE_BRIDGE"); b && *b == '0')
        if (auto* bridge = m_app->getModule<kke::PhysicsBridgeModule>()) bridge->enabled = false;
    if (const char* st = std::getenv("KKE_STRESS_TEST"); st && *st && *st != '0') {
        m_stressQuitAtEnd = true;
        startStressTest();
    }
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
    // A low roof (1.2 m clearance) on corner posts: crouch (C) to get under.
    addStaticBox({ { 10.0f, 1.35f, 6.0f }, { 1.5f, 0.15f, 1.5f }, accent }, v, idx);
    for (glm::vec2 c : { glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, 1) })
        addStaticBox({ { 10.0f + c.x * 1.4f, 0.6f, 6.0f + c.y * 1.4f }, { 0.1f, 0.6f, 0.1f }, wall }, v, idx);
    buildParkourLane(v, idx);
    buildPool(v, idx);
    buildLava(v, idx);
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

// The parkour lane (x = 20, run toward -Z from z = 28): nothing here is
// marked up. kke::Locomotion's sensors decide what each piece is: a fence
// and a low wall to vault, a chest-high block to climb, a 2.1 m ledge only
// a sprint reaches, and a 3 m wall that is just a wall.
void ShowcaseModule::buildParkourLane(std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) {
    const glm::vec3 fence(0.75f, 0.62f, 0.35f), block(0.5f, 0.55f, 0.62f), tall(0.45f, 0.42f, 0.4f);
    const float x = 20.0f;
    addStaticBox({ { x, 0.005f, 17.0f }, { 2.6f, 0.005f, 11.5f }, glm::vec3(0.42f, 0.44f, 0.4f) }, v, idx); // lane marking
    addStaticBox({ { x, 0.5f, 24.0f }, { 2.5f, 0.5f, 0.15f }, fence }, v, idx);         // 1.0 m fence: vault
    addStaticBox({ { x, 0.3f, 20.5f }, { 2.5f, 0.3f, 0.25f }, fence }, v, idx);         // 0.6 m wall: vault
    addStaticBox({ { x, 0.75f, 16.0f }, { 2.5f, 0.75f, 1.2f }, block }, v, idx);        // 1.5 m block: climb
    addStaticBox({ { x, 1.05f, 9.5f }, { 2.5f, 1.05f, 1.5f }, block }, v, idx);         // 2.1 m ledge: sprint + climb
    addStaticBox({ { x - 4.0f, 1.5f, 12.0f }, { 0.3f, 1.5f, 4.0f }, tall }, v, idx);    // 3 m wall: no
}

void ShowcaseModule::findScenes() {
    const char* base = SDL_GetBasePath();
    std::string dir = kke::findAssetFolder("scenes", { "KKE_SCENES_DIR" }, base ? base : "");
    if (dir.empty()) return;
    std::vector<std::string> files;
    for (const auto& e : std::filesystem::directory_iterator(dir))
        if (e.path().string().ends_with(".scene.json")) files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    for (const std::string& f : files) {
        try {
            SceneEntry e;
            e.path = f;
            e.file = kke::SceneFile::load(f);
            e.origin = glm::vec3(200.0f * static_cast<float>(m_scenes.size() + 1), 0.0f, 0.0f);
            m_scenes.push_back(std::move(e));
        } catch (const std::exception& ex) {
            kke::log::get(name())->warn("{}", ex.what());
        }
    }
}

void ShowcaseModule::scanCatalog() {
    if (m_catalogScanned) return;
    const char* base = SDL_GetBasePath();
    m_assetDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "");
    if (!m_assetDir.empty()) m_catalog = kke::AssetCatalog::scan(m_assetDir);
    m_catalogScanned = true;
    for (const kke::CatalogAsset& a : m_catalog.assets)
        if (a.skinned && a.name.rfind("SK_Character", 0) == 0) m_characters.push_back(a.name);
}

void ShowcaseModule::visitScene(size_t index) {
    SceneEntry& e = m_scenes[index];
    if (!e.isLoaded) {
        scanCatalog();
        e.loaded = kke::loadScene(e.file, m_catalog, *m_models, &m_rigid->world(), e.origin);
        if (e.file.groundSize.x > 0.0f) {
            std::vector<kke::Vertex> v;
            std::vector<uint32_t> i;
            appendBox(glm::translate(glm::mat4(1.0f), e.origin + glm::vec3(0, -0.25f, 0)),
                      glm::vec3(e.file.groundSize.x * 0.5f, 0.25f, e.file.groundSize.y * 0.5f), e.file.groundColor, v, i);
            e.ground = std::make_unique<kke::DynamicMeshRenderer>(*m_app);
            e.ground->upload(v, i);
        }
        e.isLoaded = true;
    }
    m_autopilot = false;
    m_autopilotStart = e.origin + e.file.spawn;
    m_autopilotEndZ = e.origin.z + e.file.spawn.z - 45.0f;
    m_loco->teleport(e.origin + e.file.spawn);
    m_loco->setFacing(glm::vec3(std::sin(glm::radians(e.file.spawnYaw)), 0.0f, -std::cos(glm::radians(e.file.spawnYaw))));
    m_rig.yaw = e.file.spawnYaw;
    // The scene's lighting, as saved from the sandbox: the sun and ambient
    // go into the Lighting panel's values (it drives light 0 every frame),
    // point lights into slots 2-3 (0-1 are the sun and the sky fill).
    if (e.file.hasSun) {
        const glm::vec3 toSun = -e.file.sunDirection;
        m_sunElevation = glm::degrees(std::asin(std::clamp(toSun.y, -1.0f, 1.0f)));
        m_sunAzimuth = glm::degrees(std::atan2(toSun.x, toSun.z));
        m_sunColor = e.file.sunColor;
        m_sunIntensity = e.file.sunIntensity;
    }
    if (e.file.hasAmbient) m_ambient = std::max({ e.file.ambient.r, e.file.ambient.g, e.file.ambient.b });
    for (int i = 0; i < 2; ++i) {
        kke::Light& l = m_app->lighting().lights[2 + i];
        l.enabled = i < static_cast<int>(e.file.lights.size());
        if (!l.enabled) continue;
        l.isDirectional = false;
        l.position = e.origin + e.file.lights[i].position;
        l.color = e.file.lights[i].color;
        l.intensity = e.file.lights[i].intensity;
    }
    m_status = e.file.name + (e.loaded.missing.empty() ? std::string() : " (" + std::to_string(e.loaded.missing.size()) + " assets missing: install its packs)");
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
    spawnPoolFloaters(n);
    // Small crates under the breaking yard's glass: with FEMFX and the
    // bridge (PhysicsBridgeModule) the shards land on them and knock them
    // about.
    m_yardCratesFirst = m_crates.size();
    for (const glm::vec3 at : { glm::vec3(-0.3f, 0.15f, -0.2f), glm::vec3(0.3f, 0.15f, -0.2f), glm::vec3(0.0f, 0.15f, 0.3f) }) {
        kke::RigidWorld::BodyDesc d;
        d.halfExtents = glm::vec3(0.15f);
        d.density = 250.0f;
        d.position = kYard + at;
        d.material = 2;
        m_crates.push_back({ m_rigid->world().add(d), d.halfExtents, n++ % 2 });
    }
    replicateBodies();
}

// The same bodies in the same order on every machine: network id 0 is the
// platform, 1.. the crates.
void ShowcaseModule::replicateBodies() {
#if KKE_ENABLE_NET
    if (!m_net) return;
    m_net->clearBodies();
    m_net->replicateBody(m_platform);
    for (const Crate& c : m_crates) m_net->replicateBody(c.body);
#endif
}

void ShowcaseModule::resetCourse() {
#if KKE_ENABLE_NET
    if (m_net && !m_net->authority()) {
        m_net->sendEvent(kEventReset, {}); // the host's crates are the real ones
        return;
    }
#endif
    spawnCrates();
}

void ShowcaseModule::spawnBreakables() {
#if KKE_ENABLE_FEMFX
    if (!m_femfx) return;
    // The breaking yard: glass on two supports, a plank bridge, a stone
    // wall (FEMFX, Voronoi pieces; shoot them with F). Supports are Jolt
    // static boxes too, so the player collides with them.
    const glm::vec3 yard = kYard;
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
        const kke::RigidWorld::BodyId id = m_rigid->world().add(d);
        // It's a FEMFX block too: FEMFX mustn't see a second one in the same place.
        if (auto* bridge = m_app->getModule<kke::PhysicsBridgeModule>()) bridge->ignore(id);
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
    m_loco = std::make_unique<kke::Locomotion>(m_rigid->world(), m_player);
    m_loco->setFacing(glm::vec3(0, 0, 1));
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
    m_ualModel = m_models->load(file);
    const kke::ModelData* d = m_ualModel ? m_models->model(m_ualModel) : nullptr;
    if (!d || d->animations.empty()) return;
    // Volume 2 next to it: real vault and climb clips. Only the clips the
    // demo plays are kept (the file has 134).
    const std::string file2 = (std::filesystem::path(dir) / "UAL2.fbx").string();
    if (std::filesystem::exists(file2)) {
        auto ual2 = std::make_unique<kke::ModelData>(kke::loadModel(file2));
        std::erase_if(ual2->animations, [](const kke::ModelAnimation& a) {
            for (const char* keep : kTraversalClips)
                if (a.name.find(keep) != std::string::npos) return false;
            return true;
        });
        ual2->meshes.clear();
        if (!ual2->bones.empty() && !ual2->animations.empty()) m_ual2 = std::move(ual2);
    } else {
        kke::log::get(name())->info("UAL2.fbx not in {}: vault and climb use stand-in poses", dir);
    }
    const char* who = std::getenv("KKE_CHARACTER"); // e.g. SK_Character_Father_01
    useCharacter(who ? who : "");
}

void ShowcaseModule::useCharacter(const std::string& asset) {
    const kke::ModelData* ual = m_ualModel ? m_models->model(m_ualModel) : nullptr;
    if (!ual) return;
    kke::ModelModule::ModelId model = m_ualModel;
    if (!asset.empty()) {
        scanCatalog();
        const kke::CatalogAsset* a = m_catalog.find(asset);
        if (!a) {
            kke::log::get(name())->warn("character '{}' not in the asset catalog", asset);
            return;
        }
        model = m_models->load(a->path, kke::packLoadOptions(m_catalog, *a));
    }
    const kke::ModelData* d = model ? m_models->model(model) : nullptr;
    if (!d || d->bones.empty()) {
        kke::log::get(name())->warn("character '{}' has no skeleton", asset);
        return;
    }
    // The rig: this character's bones, and the UAL clips made for them.
    m_rigData = kke::ModelData{};
    m_rigData.bones = d->bones;
    if (model == m_ualModel) {
        m_rigData.animations = ual->animations;
    } else {
        kke::BoneMatch match = kke::matchBones(*ual, *d);
        m_rigData.animations = kke::retargetAnimations(*ual, *d, match);
        std::string missing;
        for (const std::string& n : match.unmatchedTarget) missing += (missing.empty() ? "" : ", ") + n;
        kke::log::get(name())->info("'{}': {} of {} bones take the UAL clips{}{}", asset, match.matched, d->bones.size(),
                                    missing.empty() ? "" : "; at rest: ", missing);
    }
    if (m_ual2) {
        std::vector<kke::ModelAnimation> more = kke::retargetAnimations(*m_ual2, *d, kke::matchBones(*m_ual2, *d));
        for (kke::ModelAnimation& a : more) m_rigData.animations.push_back(std::move(a));
    }
    if (m_charInstance) m_models->remove(m_charInstance);
    for (auto& [id, a] : m_avatars)
        if (a.instance) m_models->remove(a.instance);
    m_avatars.clear(); // respawned with the new model and rig next frame
    for (LocalPlayer& p : m_locals) {
        if (p.instance) m_models->remove(p.instance);
        p.instance = 0;
        p.anim.reset();
    }
    m_charModel = model;
    m_character = model == m_ualModel ? std::string() : asset;
    m_charInstance = m_models->spawn(m_charModel, glm::mat4(1.0f));
    m_models->setOverlayEnabled(m_charInstance, false);
    buildAnimator();
    m_steps = kke::CharacterFootsteps();
    m_steps.bind(m_rigData);
    m_feet = kke::FootPlacer(m_rigData, kke::findChain(m_rigData, "thigh_l", "calf_l", "foot_l"),
                             kke::findChain(m_rigData, "thigh_r", "calf_r", "foot_r"), [&] {
                                 for (size_t b = 0; b < m_rigData.bones.size(); ++b)
                                     if (kke::canonicalBoneName(m_rigData.bones[b].name) == "pelvis") return static_cast<int>(b);
                                 return -1;
                             }());
    const glm::vec3 fwd = kke::modelForward(m_rigData);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
    m_armL = kke::findChain(m_rigData, "upperarm_l", "lowerarm_l", "hand_l");
    m_armR = kke::findChain(m_rigData, "upperarm_r", "lowerarm_r", "hand_r");
}

void ShowcaseModule::buildAnimator() {
    m_anim.reset();
    m_animSet = std::make_unique<kke::AnimationSet>(m_rigData);
    // The traversal clips lift the body up and over, in place, then snap
    // back: Locomotion moves the capsule up, so the lift comes out.
    for (size_t b = 0; b < m_rigData.bones.size(); ++b)
        if (kke::canonicalBoneName(m_rigData.bones[b].name) == "pelvis")
            for (const char* clip : kTraversalClips) m_animSet->removeLift(m_rigData, static_cast<int>(b), clip);
    m_anim = std::make_unique<kke::Animator>(*m_animSet);
    addAnimatorStates(*m_anim);
    m_anim->play(m_stMove, 0.0f);
    kke::log::get(name())->info("character: {} bones, {} clips", m_rigData.bones.size(), m_rigData.animations.size());
}

// The same states in the same order on every animator (ours and the other
// players'), so the m_st* indices mean the same on all of them.
void ShowcaseModule::addAnimatorStates(kke::Animator& a) {
    const kke::AnimationSet& s = *m_animSet;
    m_stMove = a.addBlendState("move", { { { s.find("|Idle_Loop"), 0.0f },
                                                 { s.find("|Walk_Loop"), kWalkSpeed },
                                                 { s.find("Jog_Fwd_Loop"), kJogSpeed },
                                                 { s.find("Sprint_Loop"), kSprintSpeed } } });
    m_stCrouch = a.addBlendState("crouch", { { { s.find("Crouch_Idle_Loop"), 0.0f }, { s.find("Crouch_Fwd_Loop"), kCrouchSpeed } } });
    m_stJump = a.addClipState("jump", s.find("Jump_Start"), false, 2.0f);
    m_stFall = a.addClipState("fall", s.find("Jump_Loop"), true);
    m_stLand = a.addClipState("land", s.find("Jump_Land"), false, 1.8f);
    // Vault and climb: the Universal Animation Library "Standard" set has
    // no vault or climb clips, so these use its closest poses as
    // stand-ins (tucked jump for the vault, the take-off reach and a
    // crouch step for the climb), and hand IK puts the hands on the edge.
    // A pack with real ones ("Vault", "Climb") is picked up by name;
    // Locomotion moves the capsule either way, the clips only provide
    // the pose.
    auto pick = [&](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (int c = s.find(n); c >= 0) return c;
        return -1;
    };
    m_stVault = a.addClipState("vault", pick({ "Vault", "Jump_Loop" }), true, 1.4f);
    m_stClimbUp = a.addClipState("climb_up", pick({ "Climb_Up", "Climb", "Jump_Start" }), false, 0.7f);
    m_stHang = a.addClipState("hang", pick({ "Hang_Idle", "Hang", "Jump_Loop" }), true, 0.35f);
    m_stClimbOver = a.addClipState("climb_over", pick({ "Climb_Over", "Crouch_Fwd_Loop" }), true, 1.3f);
    // UAL2's clips, when present: one clip for the whole move, posed by
    // Locomotion's progress (so hands meet the edge whatever the timing).
    auto real = [&](const char* clip, const char* state) { return s.find(clip) >= 0 ? a.addClipState(state, s.find(clip), false) : -1; };
    m_stVaultClip = real("SafetyVault", "vault_clip");
    m_stClimbLow = real("ClimbUp_1m", "climb_low");
    m_stClimbHigh = real("ClimbUp_2m", "climb_high");
}

void ShowcaseModule::applyIk(float dt) {
    std::vector<glm::mat4>* locals = m_models->boneLocals(m_charInstance);
    if (!locals || !m_anim) return;
    kke::Pose pose = m_anim->pose();
    using State = kke::Locomotion::State;
    const kke::Locomotion::State st = m_loco->state();
    const glm::mat4 toWorld = m_models->transform(m_charInstance);
    const glm::mat4 toModel = glm::inverse(toWorld);
    const float k = 1.0f - std::exp(-10.0f * dt);

    // Feet: on the ground only (in the air they'd reach for the floor).
    m_footWeight += ((m_footIk && st == State::Ground ? 1.0f : 0.0f) - m_footWeight) * k;
    kke::RigidWorld& w = m_rigid->world();
    auto ground = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        const glm::vec3 start = glm::vec3(toWorld * glm::vec4(from, 1.0f));
        kke::RigidWorld::RayHit h = w.raycast(start, glm::vec3(0, -1, 0), 1.2f);
        if (!h.hit || h.normal.y < 0.5f) return false;
        hit = glm::vec3(toModel * glm::vec4(h.point, 1.0f));
        normal = glm::normalize(glm::mat3(toModel) * h.normal);
        return true;
    };
    m_feet.apply(m_rigData, pose, kke::FootPlacer::SurfaceQuery(ground), dt, m_footWeight);
    // Step sounds where the (placed) feet actually come down.
    if (m_steps.bound()) {
        const glm::vec3 v = w.characterVelocity(m_player);
        m_steps.update(kke::poseToModel(m_rigData, pose), toWorld, w, m_app->getModule<kke::AudioModule>(), glm::length(glm::vec2(v.x, v.z)),
                       st == State::Ground && w.characterOnGround(m_player), dt);
    }

    // Hands: on the top edge during the first part of a vault or climb,
    // where the stand-in clips have no hand plant of their own.
    const bool reach = m_handIk && (st == State::Hang || (st == State::Climb && m_loco->traversalProgress() < 0.7f) ||
                                    (st == State::Vault && m_loco->traversalProgress() < 0.45f));
    m_handWeight += ((reach ? 1.0f : 0.0f) - m_handWeight) * (1.0f - std::exp(-18.0f * dt));
    if (m_handWeight > 0.01f) {
        const kke::Locomotion::Obstacle& o = m_loco->lastObstacle();
        const glm::vec3 in = -o.normal;
        const glm::vec3 side(in.z, 0.0f, -in.x);
        // Hanging (and shimmying) the edge is where the hands are now.
        const glm::vec3 grip = st == State::Hang ? m_loco->hangEdge() : glm::vec3(o.face.x, o.target.y, o.face.z);
        const glm::vec3 edge(grip.x + in.x * 0.08f, grip.y + 0.02f, grip.z + in.z * 0.08f);
        const std::vector<glm::mat4> world = kke::poseToModel(m_rigData, pose);
        for (int i = 0; i < 2; ++i) {
            const kke::TwoBoneChain& arm = i == 0 ? m_armL : m_armR;
            if (!arm.valid()) continue;
            // Shoulder-width apart along the edge; which side is which
            // comes from where the shoulders are.
            const glm::vec3 shoulder = glm::vec3(toWorld * world[arm.upper][3]);
            const float s = glm::dot(shoulder - edge, side) > 0.0f ? 1.0f : -1.0f;
            const glm::vec3 hand = edge + side * (0.22f * s);
            const glm::vec3 elbow = glm::vec3(toWorld * world[arm.lower][3]);
            const glm::vec3 pole = elbow - in * 0.3f + side * (0.3f * s) - glm::vec3(0, 0.2f, 0);
            kke::solveTwoBone(m_rigData, pose, arm, glm::vec3(toModel * glm::vec4(hand, 1.0f)),
                              glm::vec3(toModel * glm::vec4(pole, 1.0f)), m_handWeight);
        }
    }
    kke::poseToLocals(pose, *locals);
}

void ShowcaseModule::setCaptured(bool on) {
    m_captured = on;
    SDL_SetWindowRelativeMouseMode(m_app->window().handle(), on);
}

// Only what isn't an action: clicking the view grabs the mouse, Esc
// lets it go (Esc stays fixed so you can never lock yourself out).
void ShowcaseModule::onEvent(const SDL_Event& e) {
    ImGuiIO& io = ImGui::GetIO();
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && !m_captured && !io.WantCaptureMouse && e.button.button == SDL_BUTTON_LEFT) {
        setCaptured(true);
        m_swallowFire = true; // this click grabbed the mouse; it isn't a shot
        return;
    }
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.key == SDLK_ESCAPE) setCaptured(false);
}

// Everything else goes through the input map (rebindable, controllers,
// left-handed preset): see InputModule::defineCharacterActions.
void ShowcaseModule::readActions(float dt) {
    kke::InputMap& in = m_input->map(0);
    // Typing in an ImGui field: the game doesn't hear the keys.
    in.setContextEnabled("game", !ImGui::GetIO().WantTextInput);
    const bool mouseLeft = m_input->devices().value({ kke::SourceKind::MouseButton, 0, SDL_BUTTON_LEFT, 0 }, nullptr) > 0.5f;
    if (!mouseLeft) m_swallowFire = false;

    if (m_captured) {
        const glm::vec2 look = in.axis2("look");
        m_rig.addLook(look.x * m_mouseSensitivity, look.y * m_mouseSensitivity);
    }
    const glm::vec2 rate = in.axis2("look.rate"); // stick / gyro: 1 = full speed
    m_rig.addLook(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);
    if (in.axis("camera.zoom") != 0.0f && m_captured)
        m_rig.settings.armLength = std::clamp(m_rig.settings.armLength - in.axis("camera.zoom") * 0.4f, 1.5f, 10.0f);
    if (in.pressed("camera.toggle"))
        m_rig.mode = m_rig.mode == kke::CameraRig::Mode::ThirdPerson ? kke::CameraRig::Mode::FirstPerson : kke::CameraRig::Mode::ThirdPerson;

    m_moveInput = in.axis2("move");
    m_sprint = in.held("sprint");
    m_walk = in.held("walk");
    m_wantCrouch = in.held("crouch"); // a toggle by default; rebind as hold if you prefer
    if (in.pressed("jump")) m_jumpQueued = true;

    // Fire: once on press, then 4 shots/s while held.
    const bool fireOk = !(mouseLeft && (!m_captured || m_swallowFire));
    m_fireCooldown -= dt;
    if (fireOk && in.held("fire") && (in.pressed("fire") || m_fireCooldown <= 0.0f)) {
        shoot();
        m_fireCooldown = 0.25f;
    }
    if (in.pressed("interact")) forcePush();
    if (in.pressed("reset")) {
        resetCourse();
        m_loco->teleport(m_spawn);
    }
    if (in.pressed("panels")) {
        m_showPanels = !m_showPanels;
        for (kke::Module* p : m_panels) p->setUiVisible(m_showPanels);
    }
}

// Shoots a heavy FEMFX ball from the camera (breaks the yard's glass,
// wood and stone) and knocks any Jolt body the view points at.
void ShowcaseModule::shoot() {
    const kke::Camera& cam = m_app->camera();
    const glm::vec3 dir = glm::normalize(cam.target - cam.position);
    const glm::vec3 from = cam.position + dir * 1.0f;
    spawnBall(from, dir);
#if KKE_ENABLE_NET
    if (m_net) m_net->sendEvent(kEventShoot, pack(ShotEvent{ from, dir })); // everyone sees the ball
#endif
    forcePush();
}

void ShowcaseModule::updateBridgeDemo(float dt) {
    const float before = m_demoBridge;
    m_demoBridge += dt;
    auto at = [&](float mark) { return before < mark && m_demoBridge >= mark; };
    kke::RigidWorld& w = m_rigid->world();
    if (at(3.0f)) { // once the glass has settled and is armed to break
        m_yardCratesStart.clear();
        for (size_t i = m_yardCratesFirst; i < m_yardCratesFirst + 3 && i < m_crates.size(); ++i) m_yardCratesStart.push_back(w.position(m_crates[i].body));
        spawnBall(kYard + glm::vec3(0.1f, 2.5f, 0.05f), glm::vec3(0, -1, 0));
    }
    if (at(7.0f)) {
        std::string moved;
        for (size_t k = 0; k < m_yardCratesStart.size(); ++k) {
            const glm::vec3 d = w.position(m_crates[m_yardCratesFirst + k].body) - m_yardCratesStart[k];
            moved += fmt::format("{}crate {}: {:.3f} m", k ? ", " : "", k + 1, glm::length(d));
        }
        kke::log::get(name())->info("bridge demo: after the glass broke, {}", moved);
        // Then an iron ball thrown along the ground into the pyramid's
        // bottom-left crate: FEMFX hitting Jolt sideways.
        m_yardCratesStart.assign(1, w.position(m_crates[0].body));
        spawnBall(w.position(m_crates[0].body) + glm::vec3(0.0f, 0.0f, 2.5f), glm::vec3(0, 0, -1));
    }
    if (at(10.0f) && !m_yardCratesStart.empty()) {
        const glm::vec3 d = w.position(m_crates[0].body) - m_yardCratesStart[0];
        kke::log::get(name())->info("bridge demo: the ball knocked the crate {:.2f} m ({:.2f} m back)", glm::length(d), -d.z);
    }
}

void ShowcaseModule::spawnBall(const glm::vec3& from, const glm::vec3& dir) {
#if KKE_ENABLE_FEMFX
    if (m_femfx) {
        kke::Material iron;
        iron.density = 7800.0f; iron.stiffness = 2.0e7f; iron.poissonsRatio = 0.3f;
        iron.fractureStressThreshold = 1.0e12f; iron.metallic = 0.9f; iron.roughness = 0.35f; iron.textureId = 2;
        m_femfx->spawnFracturableTetMesh(kke::PhysicsModule::buildSphere(3, 0.15f), from, iron, dir * 22.0f);
    }
#else
    (void)from;
    (void)dir;
#endif
}

void ShowcaseModule::forcePush() {
    const kke::Camera& cam = m_app->camera();
    glm::vec3 dir = glm::normalize(cam.target - cam.position);
    auto hit = m_rigid->world().raycast(cam.position, dir, 30.0f);
    if (!hit.hit) return;
    for (size_t i = 0; i < m_crates.size(); ++i) {
        if (m_crates[i].body != hit.body) continue;
        // Here right away; on a client also on the host, whose crate ours follows.
        m_rigid->world().addImpulse(m_crates[i].body, dir * 60.0f, hit.point);
#if KKE_ENABLE_NET
        if (m_net && !m_net->authority()) m_net->sendEvent(kEventPush, pack(PushEvent{ static_cast<uint16_t>(i + 1), dir, hit.point }));
#endif
    }
}

// Host: a client's shot (show it, pass it on), push (apply it), reset.
// Client: another player's shot, relayed by the host.
void ShowcaseModule::onNetEvent(uint16_t kind, uint8_t from, const std::vector<uint8_t>& payload) {
#if KKE_ENABLE_NET
    const bool host = m_net && m_net->role() == kke::NetModule::Role::Host;
    switch (kind) {
    case kEventShoot: {
        ShotEvent e;
        if (!unpack(payload, e) || glm::length(e.dir) < 0.5f) return;
        spawnBall(e.from, glm::normalize(e.dir));
        if (host) m_net->relayEvent({ from, kind, payload });
        break;
    }
    case kEventPush: {
        PushEvent e;
        if (!host || !unpack(payload, e) || e.body == 0 || e.body > m_crates.size()) return;
        // Only near where the crate is (a stale or made-up push does nothing).
        const kke::RigidWorld::BodyId b = m_crates[e.body - 1].body;
        if (glm::length(m_rigid->world().position(b) - e.point) > 2.0f) return;
        m_rigid->world().addImpulse(b, glm::normalize(e.dir) * 60.0f, e.point);
        break;
    }
    case kEventReset:
        if (host) spawnCrates();
        break;
    default:
        break;
    }
#else
    (void)kind;
    (void)from;
    (void)payload;
#endif
}

void ShowcaseModule::fixedUpdate(const kke::FixedUpdateContext& ctx) {
    // Local only: every machine runs its own lava (what it looks like
    // isn't gameplay state).
    if (m_lava) m_lava->fixedUpdate(ctx.fixedDt, m_rigid->world(), lavaWatched());
    // Platform: back and forth, up and down.
    m_platformTime += ctx.fixedDt;
#if KKE_ENABLE_NET
    if (m_net && !m_net->authority()) return; // a client's platform follows the host's
#endif
    glm::vec3 p(-14.0f + 5.0f * std::sin(m_platformTime * 0.5f), 0.6f + 1.2f * (0.5f + 0.5f * std::sin(m_platformTime * 0.35f)), 6.0f);
    m_rigid->world().moveKinematic(m_platform, p, glm::quat(1, 0, 0, 0), ctx.fixedDt);
    floatBodies(ctx.fixedDt);
}

void ShowcaseModule::update(const kke::UpdateContext& ctx) {
    const float dt = ctx.dt;
    m_fps = m_fps * 0.95f + (dt > 0.0f ? 1.0f / dt : 0.0f) * 0.05f;
    updateStressTest(dt);
    batchCrates();
    if (m_lava) m_lava->update();
    kke::RigidWorld& w = m_rigid->world();

    // Crouch: a 1.0 m capsule. Standing up waits until there's headroom.
    // Hanging, crouch means "let go" (below), not a smaller capsule.
    const bool hanging = m_loco->state() == kke::Locomotion::State::Hang;
    if (!hanging && m_wantCrouch != m_crouch && w.setCharacterHeight(m_player, m_wantCrouch ? 1.0f : 1.8f)) m_crouch = m_wantCrouch;
    const float target = m_crouch ? 0.85f : 1.5f;
    m_rig.settings.pivotHeight += (target - m_rig.settings.pivotHeight) * std::min(1.0f, 10.0f * dt);
    m_rig.settings.eyeHeight = m_rig.settings.pivotHeight + 0.15f;

    readActions(dt);
    // Actions -> the movement layer, camera-relative. An analog stick gives
    // partial speeds (walk by tilting a little). What "go up" becomes
    // (vault, climb or jump) is Locomotion's call, from what its sensors
    // see in front of the character.
    kke::Locomotion::Input in;
    in.move = m_rig.forward() * m_moveInput.y + m_rig.right() * m_moveInput.x;
    in.move.y = 0.0f;
    if (glm::length(in.move) > 1e-3f) in.move = glm::normalize(in.move) * std::min(1.0f, glm::length(m_moveInput));
    if (m_autopilot) {
        // Down the lane (or a scene's trail) toward -Z at a run; "go up"
        // whenever the sensors see something (a player's timing). On the
        // course it sprints from the block to the 2.1 m ledge.
        m_autopilotTime += dt;
        in.move = glm::vec3(0, 0, -1);
        const glm::vec3 at = w.characterPosition(m_player);
        const bool onCourse = std::abs(at.x) < 100.0f;
        m_sprint = onCourse ? at.z < 14.8f : true;
        const auto& ls = m_loco->settings();
        if (m_loco->state() == kke::Locomotion::State::Ground &&
            m_loco->probe(in.move, m_sprint ? ls.sprintSensor : ls.walkSensor).kind != kke::Locomotion::Obstacle::Kind::None)
            m_jumpQueued = true;
        const float endZ = onCourse ? 7.0f : m_autopilotEndZ;
        if (at.z < endZ || m_autopilotTime > 25.0f) { // the end: again
            m_loco->teleport(m_autopilotStart);
            m_autopilotTime = 0.0f;
        }
    }
    if (m_demoHang >= 0.0f) {
        // KKE_DEMO_HANG=1: jump at the 3 m wall, hang, shimmy along it and
        // around its end, jump back off (screenshots of the ledge moves).
        m_demoHang += dt;
        const float t = m_demoHang;
        in.move = t < 1.6f ? glm::vec3(-1, 0, 0) : t > 2.2f && t < 6.5f ? glm::vec3(0, 0, -1) : glm::vec3(0.0f);
        auto at = [&](float mark) { return t >= mark && t - dt < mark; };
        if (at(0.3f)) m_jumpQueued = true;
        if (at(6.9f)) m_demoAway = -m_loco->facing();         // away from the wall...
        if (t > 6.9f && t < 7.5f) in.move = m_demoAway;
        if (at(7.0f)) m_jumpQueued = true;                    // ...and jump: off the ledge
        if (t > 10.0f) {
            m_loco->teleport(glm::vec3(17.2f, 0.05f, 12.0f));
            m_demoHang = 0.0f;
        }
    }
    // Wading through the pool: no running.
    const bool wading = inPool(w.characterPosition(m_player));
    if (m_demoBridge >= 0.0f) updateBridgeDemo(dt);
    in.fast = m_sprint && !m_crouch && !wading;
    in.slow = m_walk || wading;
    in.crouch = hanging ? m_wantCrouch != m_crouch : m_crouch;
    in.goUp = m_jumpQueued;
    m_jumpQueued = false;
    if (m_rig.mode == kke::CameraRig::Mode::FirstPerson) m_loco->setFacing(m_rig.forward());
    const kke::Locomotion::State before = m_loco->state();
    m_loco->update(in, dt);
    if (m_autopilot && m_loco->state() != before &&
        (m_loco->state() == kke::Locomotion::State::Vault || m_loco->state() == kke::Locomotion::State::Climb)) {
        const kke::Locomotion::Obstacle& o = m_loco->lastObstacle();
        kke::log::get(name())->info("autopilot: {} at z {:.1f} ({:.2f} m high, {:.2f} m deep)",
                                    m_loco->state() == kke::Locomotion::State::Vault ? "vault" : "climb", w.characterPosition(m_player).z, o.height,
                                    o.depth > 100.0f ? 0.0f : o.depth);
    }
    if (m_demoHang >= 0.0f && m_loco->state() != before) {
        static const char* const names[] = { "ground", "air", "vault", "climb", "hang" };
        const glm::vec3 f = w.characterPosition(m_player);
        kke::log::get(name())->info("hang demo: {} at {:.2f} {:.2f} {:.2f} ({:.1f} s)", names[static_cast<int>(m_loco->state())], f.x, f.y, f.z,
                                    m_demoHang);
    }
    if (m_loco->jumped() && m_anim) m_anim->play(m_stJump, 0.08f, true);

    const glm::vec3 feet = w.characterPosition(m_player);
    // Face where Locomotion says (it turns at a limited rate, squares up
    // to obstacles), or where we look in first person.
    m_facing = m_rig.mode == kke::CameraRig::Mode::FirstPerson ? m_rig.yaw : m_loco->facingYaw();
    if (feet.y < -20.0f) m_loco->teleport(m_spawn); // fell out of the world

    updateAnimation(dt);
    sendNetState(feet);
    updateAvatars(dt);
    if (m_charInstance) {
        // Turn the model so it faces -Z (UAL's mannequin already does;
        // Synty characters face +Z), then like the rig at yaw 0.
        glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(m_modelYaw - m_facing), glm::vec3(0, 1, 0));
        m_models->setTransform(m_charInstance, t);
        m_models->setVisible(m_charInstance, m_rig.mode != kke::CameraRig::Mode::FirstPerson);
        applyIk(dt);
    }

    // Camera (collides with the level through Jolt ray casts).
    m_rig.update(dt, feet, [&w](const glm::vec3& from, const glm::vec3& d, float maxD) {
        auto h = w.raycast(from, d, maxD);
        return h.hit ? h.distance : maxD;
    }, m_app->camera());
    updateLocalPlayers(dt);

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

void ShowcaseModule::updateAnimation(float dt) {
    if (!m_anim) return;
    MotionInfo m;
    m.state = m_loco->state();
    m.speed = m_loco->groundSpeed();
    m.progress = m_loco->traversalProgress();
    m.stateTime = m_loco->stateTime();
    m.fallHeight = m_loco->fallHeight();
    m.obstacleHeight = m_loco->lastObstacle().height;
    m.crouch = m_crouch;
    m.landed = m_loco->landed();
    animate(*m_anim, m, dt);
}

void ShowcaseModule::animate(kke::Animator& a, const MotionInfo& m, float dt) {
    using State = kke::Locomotion::State;
    if (m.jumped) a.play(m_stJump, 0.08f, true);
    const int cur = a.current();
    switch (m.state) {
    case State::Vault:
        if (m_stVaultClip >= 0) {
            if (cur != m_stVaultClip) a.play(m_stVaultClip, 0.08f);
            a.setProgress(m.progress);
        } else if (cur != m_stVault) a.play(m_stVault, 0.08f);
        break;
    case State::Hang:
        // No hang clip in the UAL sets: the fall pose (legs down), slowed,
        // with hand IK on the edge. A pack with "Hang_Idle" is used by name.
        if (cur != m_stHang) a.play(m_stHang, 0.12f);
        break;
    case State::Climb:
        if (m_stClimbLow >= 0 && m_stClimbHigh >= 0) {
            // Low walls take the 1 m climb (a hop and push), high ones the 2 m.
            const int clip = m.obstacleHeight < kClimbHighFrom ? m_stClimbLow : m_stClimbHigh;
            if (cur != clip) a.play(clip, 0.1f);
            a.setProgress(m.progress);
            break;
        }
        // Stand-ins: hands up the wall first, then the step over the edge.
        if (m.progress < 0.6f) { if (cur != m_stClimbUp) a.play(m_stClimbUp, 0.1f); }
        else if (cur != m_stClimbOver) a.play(m_stClimbOver, 0.15f);
        break;
    case State::Air:
        if (cur == m_stJump && a.finished()) a.play(m_stFall, 0.15f);
        // Walked off an edge (not a jump): fall after a moment.
        else if (cur != m_stJump && cur != m_stFall && m.stateTime > 0.15f) a.play(m_stFall, 0.2f);
        break;
    case State::Ground: {
        if (m.landed && m.fallHeight > 0.6f) a.play(m_stLand, 0.06f);
        const int ground = m.crouch ? m_stCrouch : m_stMove;
        const bool landing = a.current() == m_stLand && !a.finished() && m.speed < 1.0f;
        const bool jumping = a.current() == m_stJump && a.stateTime() < 0.2f;
        if (!landing && !jumping && a.current() != ground) a.play(ground, m.landed ? 0.12f : 0.2f);
        break;
    }
    }
    a.setParameter(m.speed); // measured speed: legs match the ground, in turns too
    a.update(dt);
}

// Our player as the others should see it (NetModule sends it ~30x a second).
void ShowcaseModule::sendNetState(const glm::vec3& feet) {
#if KKE_ENABLE_NET
    if (!m_net) return;
    kke::net::NetPlayerState st;
    st.position = feet;
    st.velocity = m_rigid->world().characterVelocity(m_player);
    st.yaw = m_facing;
    st.state = static_cast<uint8_t>(m_loco->state());
    st.speed = m_loco->groundSpeed();
    st.progress = m_loco->traversalProgress();
    // Vault / climb: the obstacle's height (which clip); else the fall.
    const bool traversing = m_loco->state() == kke::Locomotion::State::Vault || m_loco->state() == kke::Locomotion::State::Climb;
    st.aux = traversing ? m_loco->lastObstacle().height : m_loco->fallHeight();
    st.flags = m_crouch ? kFlagCrouch : 0;
    // Resets, scene visits and falling out of the world: a jump the
    // server's speed check should allow (and viewers shouldn't smooth).
    if (glm::length(feet - m_lastFeet) > 3.0f) st.flags |= kke::net::kPlayerTeleported;
    m_lastFeet = feet;
    m_net->setLocalPlayer(st);
#else
    (void)feet;
#endif
}

// Everyone else: our character model with its own animator, driven by the
// state they send (interpolated ~100 ms behind), or a box without a model.
void ShowcaseModule::updateAvatars(float dt) {
    m_avatarCapsules.clear();
#if KKE_ENABLE_NET
    const std::vector<kke::net::RemotePlayer> none;
    const auto& players = m_net ? m_net->remotePlayers() : none;
    for (auto it = m_avatars.begin(); it != m_avatars.end();) {
        const bool here = std::any_of(players.begin(), players.end(), [&](const kke::net::RemotePlayer& p) { return p.id == it->first && p.hasState; });
        if (here) { ++it; continue; }
        if (it->second.instance) m_models->remove(it->second.instance);
        it = m_avatars.erase(it);
    }
    using State = kke::Locomotion::State;
    for (const kke::net::RemotePlayer& p : players) {
        if (!p.hasState) continue;
        const kke::net::NetPlayerState& s = p.state;
        const glm::mat4 base = glm::rotate(glm::translate(glm::mat4(1.0f), s.position), glm::radians(-s.yaw), glm::vec3(0, 1, 0));
        if (!m_charModel || !m_animSet) {
            m_avatarCapsules.push_back(base);
            continue;
        }
        Avatar& a = m_avatars[p.id];
        if (!a.instance) {
            a.instance = m_models->spawn(m_charModel, base);
            m_models->setOverlayEnabled(a.instance, false);
            a.anim = std::make_unique<kke::Animator>(*m_animSet);
            addAnimatorStates(*a.anim);
        }
        const State now = static_cast<State>(std::min<int>(s.state, static_cast<int>(State::Hang)));
        MotionInfo m;
        m.state = now;
        m.speed = s.speed;
        m.progress = s.progress;
        m.crouch = (s.flags & kFlagCrouch) != 0;
        if (static_cast<int>(now) != a.lastState) {
            // What a state change means: a take-off going up is a jump,
            // arriving on the ground is a landing from aux metres.
            m.jumped = now == State::Air && a.lastState == static_cast<int>(State::Ground) && s.velocity.y > 1.0f;
            m.landed = now == State::Ground && a.lastState == static_cast<int>(State::Air);
            a.stateTime = 0.0f;
            a.lastState = static_cast<int>(now);
        }
        a.stateTime += dt;
        m.stateTime = a.stateTime;
        m.fallHeight = s.aux;
        m.obstacleHeight = s.aux;
        animate(*a.anim, m, dt);
        m_models->setTransform(a.instance, glm::rotate(glm::translate(glm::mat4(1.0f), s.position), glm::radians(m_modelYaw - s.yaw), glm::vec3(0, 1, 0)));
        if (std::vector<glm::mat4>* locals = m_models->boneLocals(a.instance)) kke::poseToLocals(a.anim->pose(), *locals);
    }
#else
    (void)dt;
#endif
}

// ~0.1 ms for 300 crates on one core; far cheaper than 600 draws.
void ShowcaseModule::batchCrates() {
    static std::vector<kke::Vertex> v;
    static std::vector<uint32_t> i;
    v.clear();
    i.clear();
    const kke::RigidWorld& w = m_rigid->world();
    for (const Crate& c : m_crates) appendBox(w.transform(c.body), c.half, m_cubeColors[c.cube], v, i);
    m_crateBatchIndices = i.size();
    if (!i.empty()) m_crateBatch->upload(v, i);
}

void ShowcaseModule::render(const kke::RenderContext& ctx) {
    m_level->draw(ctx, glm::mat4(1.0f), 0.0f, 0.85f);
    drawPool(ctx);
    if (m_lava) m_lava->render(ctx);
    for (const SceneEntry& e : m_scenes)
        if (e.ground) e.ground->draw(ctx, glm::mat4(1.0f), 0.0f, 0.95f);
    kke::RigidWorld& w = m_rigid->world();
    if (m_crateBatchIndices) m_crateBatch->draw(ctx, glm::mat4(1.0f), 0.0f, 0.7f);
    m_cubes[3]->draw(ctx, glm::scale(w.transform(m_platform), m_platformHalf), 0.3f, 0.4f);
    if (!m_charInstance && m_rig.mode != kke::CameraRig::Mode::FirstPerson) {
        glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), w.characterPosition(m_player)), glm::radians(-m_facing), glm::vec3(0, 1, 0));
        m_capsule->draw(ctx, t, 0.0f, 0.6f);
    }
    for (const glm::mat4& t : m_avatarCapsules) m_capsule->draw(ctx, t, 0.0f, 0.6f);
}

void ShowcaseModule::prepass(const kke::PrepassContext& ctx) {
    if (m_lava) m_lava->prepass(ctx);
}

void ShowcaseModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    m_level->drawShadow(ctx);
    if (m_lava) m_lava->renderShadow(ctx);
    for (const SceneEntry& e : m_scenes)
        if (e.ground) e.ground->drawShadow(ctx);
    kke::RigidWorld& w = m_rigid->world();
    if (m_crateBatchIndices) m_crateBatch->drawShadow(ctx);
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
    ImGui::Text("Speed %.1f m/s  %s", glm::length(glm::vec2(v.x, v.z)), m_loco->state() == kke::Locomotion::State::Hang ? "hanging" : w.characterOnGround(m_player) ? "on ground" : "in the air");
    const glm::vec3 feet = w.characterPosition(m_player);
    ImGui::Text("At %.1f %.1f %.1f, %s (%.1f m)", feet.x, feet.y, feet.z, m_crouch ? "crouched" : "standing", w.characterHeight(m_player));
    if (m_wantCrouch != m_crouch) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "No room to stand up");
    ImGui::Text("Rigid bodies %zu (%zu awake), %.2f ms", w.bodyCount(), w.activeBodyCount(), w.lastStepMs());
    if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextUnformatted("Keyboard: WASD move, Shift sprint, Alt walk, Space jump / vault / climb,\nC crouch / let go, mouse look, wheel zoom, V view, left click shoot, E push, R reset,\nF1 engine panels, Esc frees the mouse.\n"
                               "Controller: left stick move, right stick look, A jump / vault / climb, B crouch,\nL3 sprint, RT shoot, Y push, R3 view, Start reset, Back panels.");
        ImGui::SliderFloat("Mouse sensitivity", &m_mouseSensitivity, 0.02f, 0.5f, "%.2f deg/px");
        ImGui::SliderFloat("Stick / gyro speed", &m_stickSpeed, 45.0f, 540.0f, "%.0f deg/s");
        if (ImGui::Button("Left-handed keys (mirror)")) kke::InputModule::mirrorKeyboard(m_input->map(0));
        ImGui::SameLine();
        if (ImGui::Button("Save bindings")) m_status = m_input->save() ? "Bindings saved to " + m_input->path() : "Could not save bindings";
        ImGui::SameLine();
        if (ImGui::Button("Defaults")) m_input->map(0).restoreDefaults();
        if (ImGui::TreeNode("Current bindings")) {
            const kke::InputMap& in = m_input->map(0);
            for (const kke::ActionDef& a : in.actions()) {
                if (a.context != "game") continue;
                std::string text;
                for (size_t i : in.bindingsFor(a.id)) {
                    const kke::Binding& bnd = in.bindings()[i];
                    if (!text.empty()) text += ", ";
                    for (const kke::InputSource& mod : bnd.modifiers) text += m_input->devices().describe(mod) + "+";
                    text += m_input->devices().describe(bnd.source);
                }
                ImGui::Text("%s%s: %s", in.held(a.id) ? "> " : "  ", a.label.c_str(), text.c_str());
            }
            ImGui::TextDisabled("Rebind everything in the RmlUi demo's Input screen (same input.json).");
            ImGui::TreePop();
        }
    }
    if (ImGui::CollapsingHeader("Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (m_scenes.empty()) ImGui::TextWrapped("No scenes/*.scene.json found.");
        for (size_t k = 0; k < m_scenes.size(); ++k) {
            ImGui::PushID(static_cast<int>(k));
            if (ImGui::Button("Go")) visitScene(k);
            ImGui::SameLine();
            ImGui::TextUnformatted(m_scenes[k].file.name.c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", m_scenes[k].file.description.c_str());
            ImGui::PopID();
        }
        if (ImGui::Button("Back to the course")) { m_loco->teleport(m_spawn); m_rig.yaw = 0.0f; }
    }
    if (ImGui::CollapsingHeader("Movement")) {
        static const char* kStates[] = { "ground", "air", "vault", "climb" };
        static const char* kKinds[] = { "nothing", "vault", "climb" };
        ImGui::Text("State: %s", kStates[static_cast<int>(m_loco->state())]);
        const kke::Locomotion::Sensor& sensor = m_sprint ? m_loco->settings().sprintSensor : m_loco->settings().walkSensor;
        kke::Locomotion::Obstacle ahead = m_loco->probe(m_loco->facing(), sensor);
        ImGui::Text("Ahead: %s (%.2f m high, %.2f m deep)", kKinds[static_cast<int>(ahead.kind)], ahead.height,
                    ahead.depth > 100.0f ? 0.0f : ahead.depth);
        kke::Locomotion::Settings& ms = m_loco->settings();
        ImGui::SliderFloat("Turn rate", &ms.turnRate, 90.0f, 1440.0f, "%.0f deg/s");
        ImGui::SliderFloat("Sprint turn rate", &ms.sprintTurnRate, 90.0f, 1440.0f, "%.0f deg/s");
        ImGui::SliderFloat("Speed in sharp turns", &ms.turnSpeedFactor, 0.2f, 1.0f);
        ImGui::SliderFloat("Air steering", &ms.airAcceleration, 0.0f, 20.0f, "%.1f m/s2");
        ImGui::SliderFloat("Vault clearance", &ms.vaultClearance, 0.0f, 0.6f, "%.2f m");
        ImGui::SliderFloat("Climb time", &ms.climbTime, 0.3f, 2.0f, "%.2f s");
        ImGui::TextWrapped("Parkour lane at x = 20: fence and low wall (vault), block (climb), 2.1 m ledge (sprint, then climb), 3 m wall (jump at it to hang: A/D shimmy, also round corners, Space climb, back + Space jump off, C let go).");
    }
    if (ImGui::CollapsingHeader("Performance")) {
        const kke::ResourceBudget& b = m_app->resourceBudget();
        ImGui::Text("%d worker thread(s) of %u cores, frame cap %s, now %.0f", b.workerThreads, kke::usableCpuCount(),
                    b.frameRateLimit > 0.0f ? std::to_string(static_cast<int>(b.frameRateLimit)).c_str() : "vsync/none",
                    m_app->effectiveFrameRateLimit());
        if (kke::SettingsModule* sm = m_app->getModule<kke::SettingsModule>()) {
            kke::EngineSettings::Performance& p = sm->settings().performance;
            bool changed = ImGui::Checkbox("Use everything (all cores, no caps)", &p.useEverything);
            changed |= ImGui::SliderFloat("Background frame cap", &p.backgroundFrameRate, 0.0f, 60.0f, "%.0f fps");
            changed |= ImGui::SliderFloat("3D render scale", &p.renderScale, 0.5f, 1.0f, "%.2f");
            changed |= ImGui::SliderInt("Worker threads (0 = auto, restart)", &p.workerThreads, 0, static_cast<int>(kke::usableCpuCount()) * 2);
            if (changed) sm->apply();
            if (ImGui::Button("Save settings")) sm->save();
        }
        ImGui::Separator();
        if (m_stressActive) {
            ImGui::TextUnformatted("Stress test running...");
        } else {
            if (ImGui::Button("Run stress test (36 s)")) startStressTest();
            ImGui::SameLine();
            ImGui::TextDisabled("walk, 300 crates, impacts; writes one report");
        }
        if (!m_stressReport.empty()) {
            const kke::FrameStats::Summary all = m_stressStats.overall();
            ImGui::Text("Last: %s, %.0f fps avg, %.0f fps 1%% low", kke::FrameStats::verdict(all), all.fpsAvg, all.low1Fps);
            ImGui::TextWrapped("%s.txt", m_stressReport.c_str());
        }
    }
    if (ImGui::CollapsingHeader("Character")) {
        ImGui::Checkbox("Feet on the ground (foot IK)", &m_footIk);
        ImGui::Checkbox("Hands on edges (hand IK)", &m_handIk);
        ImGui::Text("Hips lowered %.2f m", -m_feet.pelvisOffset());
        scanCatalog();
        const char* current = m_character.empty() ? "UAL mannequin" : m_character.c_str();
        if (ImGui::BeginCombo("Character", current)) {
            if (ImGui::Selectable("UAL mannequin", m_character.empty())) useCharacter("");
            for (const std::string& c : m_characters)
                if (ImGui::Selectable(c.c_str(), c == m_character)) useCharacter(c);
            ImGui::EndCombo();
        }
        ImGui::TextWrapped("Synty characters wear the UAL clips, retargeted by bone name.");
    }
    if (ImGui::CollapsingHeader("Lighting")) {
        ImGui::SliderFloat("Sun direction", &m_sunAzimuth, -180.0f, 180.0f, "%.0f deg");
        ImGui::SliderFloat("Sun height", &m_sunElevation, 2.0f, 90.0f, "%.0f deg");
        ImGui::SliderFloat("Sun strength", &m_sunIntensity, 0.0f, 3.0f);
        ImGui::ColorEdit3("Sun colour", &m_sunColor.x);
        ImGui::SliderFloat("Ambient", &m_ambient, 0.0f, 1.0f);
    }
    if (m_lava && ImGui::CollapsingHeader("Lava")) {
        ImGui::Text("Block: %s, %.0f %% left", m_lava->blockName(), m_lava->blockLeft() * 100.0f);
        ImGui::Text("Liquid: %zu / %zu particles, %.2f ms a step", m_lava->particles(), m_lava->budget(), m_lava->stepMs());
        if (ImGui::Button("Next block")) m_lava->next();
        ImGui::TextDisabled("Runs only while a camera is within 18 m.");
    }
    if (ImGui::CollapsingHeader("Split screen")) {
        int players = static_cast<int>(m_locals.size()) + 1;
        if (ImGui::SliderInt("Players", &players, 1, static_cast<int>(kke::kMaxViews))) setLocalPlayers(players);
        ImGui::Checkbox("Two players side by side (off: stacked)", &m_splitSideBySide);
        ImGui::Checkbox("Overhead view (picture-in-picture)", &m_overhead);
        ImGui::TextUnformatted("Player 1: keyboard and mouse, and any controller nobody else has.");
        for (size_t i = 0; i < m_locals.size(); ++i) {
            const kke::InputDevices::Device* d = m_locals[i].pad ? m_input->devices().find(m_locals[i].pad) : nullptr;
            ImGui::Text("Player %zu: %s", i + 2, d ? d->label().c_str() : "no controller, runs the lane");
        }
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
