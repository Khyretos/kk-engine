#include "JiggleDemoModule.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"
#include "kke/modules/DemoPanelModule.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/OrbitCameraModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <typeindex>

namespace kke_jiggle {

namespace {

constexpr float kWalkSpeed = 1.6f, kJogSpeed = 3.6f, kSprintSpeed = 6.2f; // UAL clip speeds (showcase)
constexpr size_t kMaxBalls = 14;
// Jelly looks: tint (sRGB), density (absorption), milkiness.
struct JellyLook { const char* name; glm::vec3 tint; float density, milkiness; };
const JellyLook kLooks[] = {
    { "Strawberry", { 0.95f, 0.12f, 0.2f }, 1.1f, 0.08f },
    { "Lime", { 0.45f, 0.95f, 0.2f }, 0.9f, 0.08f },
    { "Blue raspberry", { 0.15f, 0.55f, 0.98f }, 1.0f, 0.06f },
    { "Orange", { 1.0f, 0.55f, 0.08f }, 0.8f, 0.12f },
    { "Panna cotta", { 0.98f, 0.95f, 0.85f }, 0.4f, 0.85f },
    { "Clear gelatin", { 0.97f, 0.93f, 0.8f }, 0.35f, 0.0f },
};
constexpr int kLookCount = static_cast<int>(sizeof(kLooks) / sizeof(kLooks[0]));
// Fruit set in the jelly (rest positions, radius, colour): moves with it.
struct Fruit { glm::vec3 rest; float radius; glm::vec3 color; };
const Fruit kFruit[] = {
    { { -0.18f, 0.16f, -0.12f }, 0.045f, { 0.2f, 0.18f, 0.45f } },  // blueberries
    { { 0.2f, 0.3f, 0.1f }, 0.04f, { 0.18f, 0.16f, 0.42f } },
    { { 0.05f, 0.12f, 0.22f }, 0.042f, { 0.22f, 0.2f, 0.5f } },
    { { 0.12f, 0.2f, -0.2f }, 0.06f, { 0.95f, 0.25f, 0.3f } },    // raspberries
    { { -0.22f, 0.32f, 0.18f }, 0.055f, { 0.9f, 0.2f, 0.28f } },
    { { -0.05f, 0.28f, -0.02f }, 0.07f, { 0.98f, 0.95f, 0.8f } },  // a lychee
    { { 0.27f, 0.12f, -0.02f }, 0.05f, { 0.98f, 0.6f, 0.1f } },    // mandarin
};
const glm::vec3 kBallColors[] = { { 0.98f, 0.8f, 0.1f }, { 0.15f, 0.55f, 0.95f }, { 0.2f, 0.8f, 0.35f },
                                  { 0.95f, 0.45f, 0.1f }, { 0.75f, 0.3f, 0.9f },  { 0.95f, 0.95f, 0.95f } };

float rand01(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) * (1.0f / 16777216.0f);
}

// A flat disc (the plate) or square (the floor) facing up.
void flatShape(kke::DynamicMeshRenderer& r, float y, float radius, int sides, const glm::vec3& color) {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    v.push_back({ glm::vec3(0, y, 0), color, glm::vec3(0, 1, 0), glm::vec2(0) });
    for (int i = 0; i < sides; ++i) {
        const float a = 6.2831853f * (i + 0.5f) / sides;
        const float rr = sides == 4 ? radius * 1.41421356f : radius;
        v.push_back({ glm::vec3(std::cos(a) * rr, y, std::sin(a) * rr), color, glm::vec3(0, 1, 0), glm::vec2(0) });
        const uint32_t a0 = 1 + i, a1 = 1 + (i + 1) % sides;
        idx.insert(idx.end(), { 0u, a1, a0 });
    }
    r.upload(v, idx);
}

} // namespace

std::vector<kke::ModuleDependency> JiggleDemoModule::dependencies() const {
    return { { std::type_index(typeid(kke::ModelModule)), true, "draws the characters" } };
}

void JiggleDemoModule::init(kke::Application& app) {
    m_app = &app;
    m_models = app.getModule<kke::ModelModule>();
    m_camera = app.getModule<kke::OrbitCameraModule>();
    m_spheres = std::make_unique<kke::SphereImpostorRenderer>(app);
    m_jellyMesh = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_plate = std::make_unique<kke::DynamicMeshRenderer>(app);
    m_floor = std::make_unique<kke::DynamicMeshRenderer>(app);
    flatShape(*m_plate, 0.002f, 0.8f, 48, glm::vec3(0.92f, 0.92f, 0.9f));
    flatShape(*m_floor, -0.004f, 12.0f, 4, glm::vec3(0.32f, 0.34f, 0.37f));
    resetJelly();

    m_tissue.bust = 0.075f;
    m_tissue.glutes = 0.06f;
    m_tissue.hips = 0.03f;
    setupBodies();

    Scene start = Scene::Jelly;
    if (const char* e = std::getenv("KKE_JIGGLE_SCENE"); e && (std::strcmp(e, "body") == 0 || std::strcmp(e, "character") == 0)) start = Scene::Body;
    if (const char* e = std::getenv("KKE_JIGGLE_TWIN")) m_showTwin = *e == '1';
    if (const char* e = std::getenv("KKE_JELLY_LOOK")) m_look = std::clamp(std::atoi(e), 0, kLookCount - 1);
    m_density = kLooks[m_look].density;
    m_milkiness = kLooks[m_look].milkiness;
    if (const char* e = std::getenv("KKE_JIGGLE_MOVE")) {
        const int m = std::atoi(e);
        if (m >= 0 && m <= static_cast<int>(Move::Tour)) m_move = static_cast<Move>(m);
    }
    setScene(start);
    defineInput();
    buildPanel();
}

void JiggleDemoModule::setScene(Scene s) {
    m_scene = s;
    for (Dancer& d : m_dancers) m_models->setVisible(d.instance, s == Scene::Body && (d.jiggle || m_showTwin));
    if (!m_camera) return;
    if (s == Scene::Jelly) m_camera->setView(glm::vec3(0.0f, 0.3f, 0.0f), 2.6f, -0.45f, 0.5f);
    else m_camera->setView(glm::vec3(0.0f, 0.95f, 0.0f), 3.4f, -0.12f, 0.0f);
    // KKE_JIGGLE_VIEW="yaw,pitch,distance" (radians, metres): screenshots from a set angle.
    if (const char* v = std::getenv("KKE_JIGGLE_VIEW")) {
        float yaw = 0.0f, pitch = -0.12f, dist = 3.4f;
        if (std::sscanf(v, "%f,%f,%f", &yaw, &pitch, &dist) >= 1) m_camera->setView(m_camera->target(), dist, pitch, yaw);
    }
}

// ---------------------------------------------------------------------
// Jelly

void JiggleDemoModule::resetJelly() {
    kke::JellyBody::Params p = m_jelly.particleCount() ? m_jelly.params() : kke::JellyBody::Params{};
    if (!m_jelly.particleCount()) {
        p.min = glm::vec3(-0.42f, 0.0f, -0.42f);
        p.max = glm::vec3(0.42f, 0.5f, 0.42f);
        p.cells = glm::ivec3(7, 4, 7);
        p.stiffness = 0.22f;
        p.iterations = 3;
        p.damping = 0.015f;
    }
    m_jelly = kke::JellyBody(p);
    m_jelly.buildSurface(14);
    m_balls.clear();
    m_ballColors.clear();
    m_rainTimer = 0.0f;
}

void JiggleDemoModule::dropBall(float radius, float height) {
    if (m_balls.size() >= kMaxBalls) {
        m_balls.erase(m_balls.begin());
        m_ballColors.erase(m_ballColors.begin());
    }
    kke::JellyBody::Ball b;
    b.pos = glm::vec3((rand01(m_rng) - 0.5f) * 0.5f, height, (rand01(m_rng) - 0.5f) * 0.5f);
    b.vel = glm::vec3((rand01(m_rng) - 0.5f) * 0.15f, 0.0f, (rand01(m_rng) - 0.5f) * 0.15f);
    b.radius = radius;
    b.mass = 0.3f * std::pow(radius / 0.1f, 3.0f);
    b.restitution = 0.55f;
    m_balls.push_back(b);
    m_ballColors.push_back(kBallColors[m_colorIndex++ % (sizeof(kBallColors) / sizeof(kBallColors[0]))]);
}

void JiggleDemoModule::updateJelly(float dt) {
    if (m_rain) {
        m_rainTimer += dt;
        if (m_rainTimer >= m_rainInterval) {
            m_rainTimer = 0.0f;
            dropBall(0.07f + rand01(m_rng) * 0.05f, 1.4f + rand01(m_rng) * 0.6f);
        }
    }
    const auto t0 = std::chrono::steady_clock::now();
    m_jelly.step(dt, m_balls);
    m_jelly.deform();
    m_jellyMs = m_jellyMs * 0.9 + std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() * 0.1;
    // Balls that rolled away, or came to rest on the floor off the plate, are gone.
    for (size_t i = m_balls.size(); i-- > 0;) {
        const glm::vec3& p = m_balls[i].pos;
        const bool resting = p.y < m_balls[i].radius + 0.01f && p.x * p.x + p.z * p.z > 0.8f && glm::length(m_balls[i].vel) < 0.15f;
        if (resting || p.x * p.x + p.z * p.z > 16.0f || !std::isfinite(p.x)) {
            m_balls.erase(m_balls.begin() + static_cast<long>(i));
            m_ballColors.erase(m_ballColors.begin() + static_cast<long>(i));
        }
    }
    const auto& pos = m_jelly.surfacePositions();
    const auto& nrm = m_jelly.surfaceNormals();
    std::vector<kke::Vertex> v(pos.size());
    // (The dynamic-mesh shader reads uv.x as glow: keep it 0.)
    // drawTranslucent reads uv.x = density, uv.y = milkiness.
    const JellyLook& look = kLooks[m_look];
    for (size_t i = 0; i < pos.size(); ++i) v[i] = kke::Vertex{ pos[i], look.tint, nrm[i], glm::vec2(m_density, m_milkiness) };
    m_jellyMesh->upload(v, m_jelly.surfaceIndices());
}

// ---------------------------------------------------------------------
// Bodies

void JiggleDemoModule::setupBodies() {
    auto log = kke::log::get(name());
    const char* base = SDL_GetBasePath();
    const std::string animDir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string ualFile = animDir.empty() ? std::string() : (std::filesystem::path(animDir) / "UAL1_Standard.fbx").string();
    if (ualFile.empty() || !std::filesystem::exists(ualFile)) {
        m_bodyStatus = "Animation library not found (assets/animations/UAL1_Standard.fbx).";
        log->info("{}", m_bodyStatus); // optional download; the panel says so too
        return;
    }
    std::vector<std::string> searched;
    const std::string packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &searched);
    const kke::AssetCatalog catalog = packDir.empty() ? kke::AssetCatalog{} : kke::AssetCatalog::scan(packDir);
    const kke::CatalogAsset* asset = nullptr;
    if (const char* want = std::getenv("KKE_JIGGLE_CHARACTER")) asset = catalog.find(want);
    for (const char* c : { "SK_Character_Female_Gypsy", "SK_Character_Female_Peasant_01", "SK_Character_HipsterGirl", "SK_Character_Female_Druid",
                           "SK_Character_Dummy_Female_01" })
        if (!asset) asset = catalog.find(c, { "POLYGON_Fantasy_Characters", "POLYGON_City_Characters", "POLYGON_Prototype" });
    if (!asset) {
        m_bodyStatus = "No female Synty character found. Put POLYGON Fantasy Characters (or City Characters) in assets/synty/ "
                       "or set KKE_ASSETS_DIR.";
        log->info("{}", m_bodyStatus); // optional pack; the panel says so too
        return;
    }
    kke::ModelData ual, body;
    try {
        ual = kke::loadModel(ualFile);
        body = kke::loadModel(asset->path, kke::packLoadOptions(catalog, *asset));
    } catch (const std::exception& e) {
        m_bodyStatus = e.what();
        log->error("{}", m_bodyStatus);
        return;
    }
    m_characterName = asset->name;
    // The shape and the soft-tissue bones first, then the clips (the new
    // bones have no UAL counterpart and stay at rest in them).
    const kke::HumanoidJiggleSetup setup = kke::addHumanoidSoftTissue(body, m_tissue);
    // A character without a breast or belly bone just jiggles less: info.
    for (const std::string& m : setup.missing) log->info("'{}': soft tissue: no {}", asset->name, m);
    const kke::BoneMatch match = kke::matchBones(ual, body);
    m_rig = kke::ModelData{};
    m_rig.bones = body.bones;
    m_rig.boundsMin = body.boundsMin;
    m_rig.boundsMax = body.boundsMax;
    m_rig.animations = kke::retargetAnimations(ual, body, match);
    m_bones = setup.chains.size();
    m_zones = setup.zones.size();
    log->info("'{}': {} jiggle bones, {} skin zones, {} of {} bones take the UAL clips", asset->name, m_bones, m_zones, match.matched,
              body.bones.size());

    const kke::ModelModule::ModelId id = m_models->add(std::move(body), "jiggle:" + asset->name);
    if (!id) return;
    m_animSet = std::make_unique<kke::AnimationSet>(m_rig);
    m_anim = std::make_unique<kke::Animator>(*m_animSet);
    const kke::AnimationSet& s = *m_animSet;
    m_stMove = m_anim->addBlendState("move", { { { s.find("|Idle_Loop"), 0.0f },
                                                 { s.find("|Walk_Loop"), kWalkSpeed },
                                                 { s.find("Jog_Fwd_Loop"), kJogSpeed },
                                                 { s.find("Sprint_Loop"), kSprintSpeed } } });
    m_stJumpStart = m_anim->addClipState("jump", s.find("Jump_Start"), false, 1.6f);
    m_stJumpLoop = m_anim->addClipState("fall", s.find("Jump_Loop"), true);
    m_stLand = m_anim->addClipState("land", s.find("Jump_Land"), false, 1.5f);
    m_anim->play(m_stMove, 0.0f);

    for (int i = 0; i < 2; ++i) {
        Dancer d;
        d.jiggle = i == 1;
        d.instance = m_models->spawn(id, glm::mat4(1.0f));
        m_models->setOverlayEnabled(d.instance, false);
        if (d.jiggle) {
            d.rig = kke::JiggleRig(m_rig, setup.chains);
            d.skin = kke::JiggleSkin(m_rig, setup.zones, setup.zonePositions);
        }
        m_dancers.push_back(std::move(d));
    }
    m_bodiesReady = true;
}

void JiggleDemoModule::jump() {
    if (!m_bodiesReady || m_airborne) return;
    m_airborne = true;
    m_jumpV = 3.6f;
    m_anim->play(m_stJumpStart, 0.08f, true);
}

void JiggleDemoModule::updateBodies(float dt) {
    if (!m_bodiesReady) return;
    // The tour: stand, jog, jump, sprint, stop dead, jump, walk. Starts and
    // stops are where jiggle shows (and where bad jiggle shows most).
    float target = 0.0f;
    if (m_move == Move::Tour) {
        const float before = m_tourTime;
        m_tourTime = std::fmod(m_tourTime + dt, 17.0f);
        auto crossed = [&](float t) { return before < t && m_tourTime >= t; };
        const float t = m_tourTime;
        target = t < 2.5f ? 0.0f : t < 6.5f ? kJogSpeed : t < 10.0f ? kSprintSpeed : t < 13.0f ? 0.0f : kWalkSpeed;
        if (crossed(5.0f) || crossed(11.5f)) jump();
    } else {
        target = m_move == Move::Walk ? kWalkSpeed : m_move == Move::Jog ? kJogSpeed : m_move == Move::Sprint ? kSprintSpeed : 0.0f;
    }
    // Real people take a moment to speed up and slow down.
    const float accel = target > m_speed ? 5.0f : 9.0f;
    m_speed += std::clamp(target - m_speed, -accel * dt, accel * dt);
    const float radius = 2.3f;
    m_angle += m_speed / radius * dt;

    if (m_airborne) {
        m_jumpV -= 9.81f * dt;
        m_jumpY += m_jumpV * dt;
        if (m_anim->current() == m_stJumpStart && m_anim->finished()) m_anim->play(m_stJumpLoop, 0.1f);
        if (m_jumpY <= 0.0f) {
            m_jumpY = 0.0f;
            m_airborne = false;
            m_anim->play(m_stLand, 0.05f, true);
        }
    } else if (m_anim->current() == m_stLand && m_anim->finished()) {
        m_anim->play(m_stMove, 0.25f);
    }
    m_anim->setParameter(m_speed);
    m_anim->update(dt);

    const glm::vec3 fwd = kke::modelForward(m_rig);
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < m_dancers.size(); ++i) {
        Dancer& d = m_dancers[i];
        // Both on one circle, half a lap apart.
        const float a = m_angle + static_cast<float>(i) * 3.14159265f;
        const glm::vec3 pos(std::cos(a) * radius, m_jumpY, std::sin(a) * radius);
        const glm::vec3 tangent(-std::sin(a), 0.0f, std::cos(a));
        const float yaw = std::atan2(tangent.x, tangent.z) - std::atan2(fwd.x, fwd.z);
        const glm::mat4 toWorld = glm::rotate(glm::translate(glm::mat4(1.0f), pos), yaw, glm::vec3(0, 1, 0));
        m_models->setTransform(d.instance, toWorld);
        kke::Pose pose = m_anim->pose();
        if (d.jiggle) {
            d.rig.apply(m_rig, pose, toWorld, dt);
            d.skin.apply(m_rig, pose, toWorld, dt);
            m_models->setSkinJiggle(d.instance, d.skin.offsets());
            m_swing = std::max(d.rig.maxSwingDegrees(), m_swing * std::exp(-dt));
            m_stretchNow = std::max(d.rig.maxStretchNow(), m_stretchNow * std::exp(-dt));
            if (std::getenv("KKE_JIGGLE_TRACE")) kke::log::get(name())->info("t={:.2f} speed={:.2f} y={:.2f} swing={:.1f} stretch={:.3f}", m_tourTime, m_speed, m_jumpY, d.rig.maxSwingDegrees(), d.rig.maxStretchNow());
        }
        if (std::vector<glm::mat4>* locals = m_models->boneLocals(d.instance)) kke::poseToLocals(pose, *locals);
        // The camera follows the jiggling one (not her jumps: that would hide them).
        if (d.jiggle && m_follow && m_camera) {
            const glm::vec3 want(pos.x, 1.0f, pos.z);
            if (m_sideView) {
                // From outside the circle, looking at her side (and so across
                // the direction she runs: where lag and bounce show best).
                // Turning the camera (mouse or right stick) since the last
                // frame moves the view around her, kept relative to her side.
                if (m_sideViewSet) m_sideYawOffset += std::remainder(m_camera->yaw() - m_sideYaw, 6.2831853f);
                m_sideYaw = std::atan2(-std::cos(a), -std::sin(a)) + m_sideYawOffset;
                m_sideViewSet = true;
                m_camera->setView(want, m_camera->distance(), m_camera->pitch(), m_sideYaw);
            } else {
                m_sideViewSet = false;
                m_camera->setTarget(want);
            }
        }
    }
    m_jiggleUs = m_jiggleUs * 0.9 + std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() * 0.1;
}

// ---------------------------------------------------------------------

void JiggleDemoModule::update(const kke::UpdateContext& ctx) {
    readInput();
    m_sceneIndex = m_scene == Scene::Jelly ? 0 : 1;
    const float dt = std::min(ctx.dt, 0.1f);
    if (m_scene == Scene::Jelly) updateJelly(dt);
    else updateBodies(dt);
}

void JiggleDemoModule::render(const kke::RenderContext& ctx) {
    m_floor->draw(ctx, glm::mat4(1.0f), 0.0f, 0.9f);
    m_sphereScratch.clear();
    if (m_scene == Scene::Jelly) {
        m_plate->draw(ctx, glm::mat4(1.0f), 0.0f, 0.3f);
        for (size_t i = 0; i < m_balls.size(); ++i) m_sphereScratch.push_back({ m_balls[i].pos, m_balls[i].radius, m_ballColors[i], 0.0f, 0.35f });
        if (m_fruit)
            for (const Fruit& f : kFruit) m_sphereScratch.push_back({ m_jelly.deformedPoint(f.rest), f.radius, f.color, 0.0f, 0.45f });
        // Opaque first, then the jelly over it (it filters what's behind).
        if (!m_sphereScratch.empty()) m_spheres->draw(ctx, m_sphereScratch);
        m_sphereScratch.clear();
        if (m_translucent) m_jellyMesh->drawTranslucent(ctx, glm::mat4(1.0f), 0.06f);
        else m_jellyMesh->draw(ctx, glm::mat4(1.0f), 0.0f, 0.12f);
    } else if (m_showPoints) {
        // Where the jiggle points are (bright) against where the pose puts them (dark).
        for (Dancer& d : m_dancers) {
            if (!d.jiggle) continue;
            for (size_t i = 0; i < d.rig.pointCount(); ++i) {
                m_sphereScratch.push_back({ d.rig.pointPosition(i), 0.018f, glm::vec3(1.0f, 0.25f, 0.5f), 0.6f, 0.4f });
                m_sphereScratch.push_back({ d.rig.pointTarget(i), 0.012f, glm::vec3(0.1f, 0.3f, 0.9f), 0.0f, 0.4f });
            }
        }
    }
    if (!m_sphereScratch.empty()) m_spheres->draw(ctx, m_sphereScratch);
}

void JiggleDemoModule::renderShadow(const kke::ShadowRenderContext& ctx) {
    if (m_scene == Scene::Jelly) m_jellyMesh->drawShadow(ctx);
}

void JiggleDemoModule::defineInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    using IM = kke::InputModule;
    kke::InputMap& m = in->map(0);
    auto action = [&](const char* id, const char* label, SDL_Scancode key, SDL_GamepadButton pad) {
        m.defineAction({ id, label, "Jiggle" });
        m.addBinding(IM::bind(id, IM::key(key)));
        m.addBinding(IM::bind(id, IM::pad(pad)));
    };
    action("jiggle.scene", "Jelly / body", SDL_SCANCODE_TAB, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    action("jiggle.go", "Rain balls on / off (jelly), jump (body)", SDL_SCANCODE_SPACE, SDL_GAMEPAD_BUTTON_SOUTH);
    action("jiggle.ball", "Big ball", SDL_SCANCODE_B, SDL_GAMEPAD_BUTTON_WEST);
    action("jiggle.squish", "Squish", SDL_SCANCODE_P, SDL_GAMEPAD_BUTTON_NORTH);
    action("jiggle.reset", "Reset the jelly", SDL_SCANCODE_R, SDL_GAMEPAD_BUTTON_EAST);
    action("jiggle.move", "Next move", SDL_SCANCODE_M, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    // Keys 1-5 pick a move directly (keyboard shortcuts, rebindable; a
    // controller steps through them with jiggle.move).
    const char* moves[] = { "Idle", "Walk", "Jog", "Sprint", "Tour" };
    for (int i = 0; i < 5; ++i) {
        const std::string id = "jiggle.move" + std::to_string(i + 1);
        m.defineAction({ id, moves[i], "Jiggle" });
        m.addBinding(IM::bind(id, IM::key(static_cast<SDL_Scancode>(SDL_SCANCODE_1 + i))));
    }
    in->commitDefaults();
}

void JiggleDemoModule::readInput() {
    auto* in = m_app->getModule<kke::InputModule>();
    if (!in) return;
    const kke::InputMap& m = in->map(0);
    for (int k = 0; k < 5; ++k)
        if (m.pressed("jiggle.move" + std::to_string(k + 1))) m_move = static_cast<Move>(k);
    if (m.pressed("jiggle.scene")) setScene(m_scene == Scene::Jelly ? Scene::Body : Scene::Jelly);
    if (m.pressed("jiggle.go")) {
        if (m_scene == Scene::Jelly) m_rain = !m_rain;
        else jump();
    }
    if (m_scene == Scene::Jelly) {
        if (m.pressed("jiggle.ball")) dropBall(0.16f, 2.2f);
        if (m.pressed("jiggle.squish")) squish();
        if (m.pressed("jiggle.reset")) resetJelly();
    } else if (m.pressed("jiggle.move")) {
        m_move = static_cast<Move>((static_cast<int>(m_move) + 1) % (static_cast<int>(Move::Tour) + 1));
    }
}

void JiggleDemoModule::squish() { m_jelly.poke(glm::vec3(0.0f, m_jelly.params().max.y, 0.0f), glm::vec3(0.0f, -6.0f, 0.0f), 0.35f); }

kke::JiggleRig* JiggleDemoModule::jiggleRig() {
    if (m_scene != Scene::Body || !m_bodiesReady) return nullptr;
    for (Dancer& d : m_dancers)
        if (d.jiggle && d.rig.valid()) return &d.rig;
    return nullptr;
}

// The settings (RmlUi, kke::DemoPanelModule): View on a controller or F3
// opens them, the mouse just clicks. Rows for the other scene hide.
void JiggleDemoModule::buildPanel() {
    auto* panel = m_app->getModule<kke::DemoPanelModule>();
    if (!panel) return;
    using Panel = kke::DemoPanelModule;
    auto jelly = [this] { return m_scene == Scene::Jelly; };
    auto body = [this] { return m_scene == Scene::Body; };

    m_sceneIndex = m_scene == Scene::Jelly ? 0 : 1;
    auto& top = panel->section("Jiggle physics");
    top.choice("Scene", &m_sceneIndex, { "Jelly", "Body" }, [this] { setScene(m_sceneIndex == 0 ? Scene::Jelly : Scene::Body); });
    top.text("{jiggle.scene} jelly / body").showIf(jelly);

    auto& j = panel->section("Jelly");
    j.sectionIf(jelly);
    j.text("{jiggle.go} rain  {jiggle.ball} big ball  {jiggle.squish} squish  {jiggle.reset} reset");
    j.toggle("Rain balls", &m_rain);
    j.slider("Every", &m_rainInterval, 0.15f, 2.0f, "%.2f s", {}, 0.05f);
    j.button("Big ball", [this] { dropBall(0.16f, 2.2f); });
    j.button("Squish", [this] { squish(); });
    j.button("Reset", [this] { resetJelly(); });
    kke::JellyBody::Params& p = m_jelly.params();
    j.slider("Firmness", &p.stiffness, 0.03f, 1.0f, "%.2f", {}, 0.01f);
    j.slider("Iterations", &p.iterations, 1, 8);
    j.slider("Damping", &p.damping, 0.0f, 0.2f, "%.3f", {}, 0.005f);
    j.heading("Look");
    std::vector<std::string> looks;
    for (int i = 0; i < kLookCount; ++i) looks.push_back(kLooks[i].name);
    j.choice("Flavour", &m_look, looks, [this] {
        m_density = kLooks[m_look].density;
        m_milkiness = kLooks[m_look].milkiness;
    });
    j.toggle("Translucent", &m_translucent);
    j.toggle("Fruit inside", &m_fruit);
    j.slider("Density", &m_density, 0.0f, 4.0f, "%.2f", {}, 0.1f);
    j.slider("Milkiness", &m_milkiness, 0.0f, 1.0f, "%.2f", {}, 0.05f);
    j.text([this] {
        char buf[200];
        std::snprintf(buf, sizeof(buf), "Lattice: %zu particles, surface %zu triangles. Balls: %zu, deformation %.1f mm. Solve: %.3f ms per frame",
                      m_jelly.particleCount(), m_jelly.surfaceIndices().size() / 3, m_balls.size(),
                      static_cast<double>(m_jelly.deformation() * 1000.0f), m_jellyMs);
        return std::string(buf);
    });

    auto& b = panel->section("Body");
    b.sectionIf(body);
    b.text([this] { return m_bodiesReady ? m_characterName : m_bodyStatus; });
    b.text("{jiggle.move} next move  {jiggle.go} jump").showIf([this] { return m_bodiesReady; });
    b.toggle("Twin without jiggle", Panel::Ref<bool>([this] { return m_bodiesReady ? &m_showTwin : nullptr; }), [this] { setScene(m_scene); });
    b.note("The twin runs half a lap behind with the same body and clips, but no jiggle, to compare.").showIf([this] { return m_bodiesReady; });
    b.choice("Move", Panel::Ref<int>([this] {
                 m_moveIndex = static_cast<int>(m_move);
                 return m_bodiesReady ? &m_moveIndex : nullptr;
             }),
             { "Idle (1)", "Walk (2)", "Jog (3)", "Sprint (4)", "Tour (5)" }, [this] { m_move = static_cast<Move>(m_moveIndex); });
    b.button("Jump", [this] { jump(); }).showIf([this] { return m_bodiesReady; });
    b.toggle("Show points", Panel::Ref<bool>([this] { return m_bodiesReady ? &m_showPoints : nullptr; }));
    b.toggle("Camera follows", Panel::Ref<bool>([this] { return m_bodiesReady ? &m_follow : nullptr; }));
    b.toggle("Side view", Panel::Ref<bool>([this] { return m_bodiesReady && m_follow ? &m_sideView : nullptr; }));
    // Both breasts share one setting, both glutes another: the rows edit
    // the left one and copy it to the right.
    auto zone = [this](size_t bone, float kke::JiggleSettings::*field) {
        return Panel::Ref<float>([this, bone, field]() -> float* {
            kke::JiggleRig* rig = jiggleRig();
            if (!rig || bone >= m_bones) return nullptr;
            return &(rig->settings(bone).*field);
        });
    };
    auto share = [this] {
        kke::JiggleRig* rig = jiggleRig();
        if (!rig) return;
        for (size_t c = 1; c < m_bones && c < 2; ++c) rig->settings(c) = rig->settings(0);
        if (m_bones >= 4) rig->settings(3) = rig->settings(2);
    };
    b.heading("Breasts").showIf([this] { return jiggleRig() != nullptr; });
    b.slider("Stiffness", zone(0, &kke::JiggleSettings::stiffness), 0.02f, 1.0f, "%.2f", share, 0.02f);
    b.slider("Soften", zone(0, &kke::JiggleSettings::soften), 0.0f, 1.0f, "%.2f", share, 0.02f);
    b.slider("Stretch", zone(0, &kke::JiggleSettings::stretch), 0.0f, 0.6f, "%.2f", share, 0.02f);
    b.slider("Drag", zone(0, &kke::JiggleSettings::drag), 0.0f, 0.6f, "%.2f", share, 0.02f);
    b.slider("Gravity", zone(0, &kke::JiggleSettings::gravity), 0.0f, 2.0f, "%.2f", share, 0.05f);
    b.slider("Blend", zone(0, &kke::JiggleSettings::blend), 0.0f, 1.0f, "%.2f", share, 0.02f);
    b.heading("Glutes").showIf([this] { return jiggleRig() != nullptr && m_bones >= 4; });
    b.slider("Stiffness", zone(2, &kke::JiggleSettings::stiffness), 0.02f, 1.0f, "%.2f", share, 0.02f);
    b.slider("Drag", zone(2, &kke::JiggleSettings::drag), 0.0f, 0.6f, "%.2f", share, 0.02f);
    b.slider("Blend", zone(2, &kke::JiggleSettings::blend), 0.0f, 1.0f, "%.2f", share, 0.02f);
    b.text([this] {
        kke::JiggleRig* rig = jiggleRig();
        if (!rig) return std::string();
        char buf[240];
        std::snprintf(buf, sizeof(buf), "%zu jiggle bones, %zu skin zones, %zu points. Jiggle + pose: %.1f us per frame%s. Peak swing %.0f deg, stretch %.0f%%",
                      m_bones, m_zones, rig->pointCount(), m_jiggleUs, rig->sleeping() ? " (asleep)" : "", static_cast<double>(m_swing),
                      static_cast<double>(m_stretchNow * 100.0f));
        return std::string(buf);
    }).showIf([this] { return jiggleRig() != nullptr; });
}

} // namespace kke_jiggle
