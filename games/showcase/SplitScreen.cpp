// Split screen (ACTION_PLAN.md 1.3): up to four players on one machine,
// each drawn in their own part of the window (Application::views(),
// kke/Viewports.h). Players 2-4 each take the next controller; player 1
// keeps the keyboard, mouse and everything nobody else took. A player
// without a controller runs the parkour lane by itself (like
// KKE_DEMO_AUTOPILOT), so split screen can be tried with no controllers.
// KKE_SPLIT=2..4 starts with that many players.

#include "ShowcaseModule.h"
#include "kke/Application.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace kke_showcase {

namespace {
// The parkour lane, for players without a controller: they start a few
// seconds apart so they don't run into each other.
const glm::vec3 kLaneStart(20.0f, 0.05f, 28.0f);
constexpr float kLaneEndZ = 7.0f;
constexpr float kLaneSprintFromZ = 14.8f; // sprint from the block to the 2.1 m ledge
constexpr float kLaneGap = 2.5f;          // seconds between runners
constexpr float kLaneMaxTime = 25.0f;
} // namespace

void ShowcaseModule::setLocalPlayers(int count) {
    count = std::clamp(count, 1, static_cast<int>(kke::kMaxViews));
    kke::RigidWorld& w = m_rigid->world();
    while (static_cast<int>(m_locals.size()) + 1 > count) {
        LocalPlayer& p = m_locals.back();
        if (p.instance) m_models->remove(p.instance);
        p.anim.reset();
        p.loco.reset();
        w.removeCharacter(p.id);
        m_locals.pop_back();
    }
    while (static_cast<int>(m_locals.size()) + 1 < count) {
        const int n = static_cast<int>(m_locals.size()) + 2; // player number
        LocalPlayer p;
        kke::RigidWorld::CharacterDesc cd;
        cd.position = w.characterPosition(m_player) + glm::vec3(1.2f * static_cast<float>(n - 1), 0.0f, 0.0f);
        p.id = w.addCharacter(cd);
        p.loco = std::make_unique<kke::Locomotion>(w, p.id);
        p.loco->setFacing(glm::vec3(0, 0, -1));
        p.rig.mode = kke::CameraRig::Mode::ThirdPerson;
        p.rig.yaw = 0.0f;
        p.rig.pitch = -12.0f;
        p.runTime = -kLaneGap * static_cast<float>(n - 1);
        m_locals.push_back(std::move(p));
    }
    m_input->setPlayers(count);
    assignControllers();
    if (m_locals.empty()) m_app->views().clear();
}

void ShowcaseModule::assignControllers() {
    // Controllers in the order they were plugged in, one per player 2-4.
    std::vector<uint32_t> pads;
    for (const auto& d : m_input->devices().devices())
        if (d.connected && d.kind == kke::InputDevices::Kind::Gamepad) pads.push_back(d.ref);
    std::vector<uint32_t> taken;
    for (size_t i = 0; i < m_locals.size(); ++i) {
        LocalPlayer& p = m_locals[i];
        const uint32_t pad = i < pads.size() ? pads[i] : 0;
        if (pad == 0 && p.pad != 0) p.runTime = -kLaneGap * static_cast<float>(i + 1); // lost it: back to the lane
        if (pad != 0 && p.pad == 0) p.loco->teleport(m_spawn + glm::vec3(1.2f * static_cast<float>(i + 1), 0.0f, 0.0f));
        p.pad = pad;
        if (pad) taken.push_back(pad);
        m_input->assignDevices(static_cast<int>(i) + 1, pad ? std::vector<uint32_t>{ pad } : std::vector<uint32_t>{});
    }
    // Player 1: everything else. Rebuilt every frame, as keyboards and
    // mice show up in the device list when first used.
    std::vector<uint32_t> rest;
    if (!taken.empty())
        for (const auto& d : m_input->devices().devices())
            if (std::find(taken.begin(), taken.end(), d.ref) == taken.end()) rest.push_back(d.ref);
    m_input->assignDevices(0, std::move(rest));
}

void ShowcaseModule::updateLocalPlayers(float dt) {
    std::vector<kke::Application::View>& views = m_app->views();
    views.clear();
    if (m_locals.empty() && !m_overhead) return;
    if (!m_locals.empty()) assignControllers();
    kke::RigidWorld& w = m_rigid->world();
    auto ray = [&w](const glm::vec3& from, const glm::vec3& d, float maxD) {
        auto h = w.raycast(from, d, maxD);
        return h.hit ? h.distance : maxD;
    };
    using State = kke::Locomotion::State;
    for (size_t i = 0; i < m_locals.size(); ++i) {
        LocalPlayer& p = m_locals[i];
        kke::Locomotion& loco = *p.loco;
        const glm::vec3 at = w.characterPosition(p.id);
        const bool hanging = loco.state() == State::Hang;
        kke::Locomotion::Input in;
        bool sprint = false, walk = false, wantCrouch = p.crouch;
        if (p.pad) {
            kke::InputMap& map = m_input->map(static_cast<int>(i) + 1);
            const glm::vec2 rate = map.axis2("look.rate");
            p.rig.addLook(rate.x * m_stickSpeed * dt, rate.y * m_stickSpeed * 0.7f * dt);
            const glm::vec2 move = map.axis2("move");
            in.move = p.rig.forward() * move.y + p.rig.right() * move.x;
            in.move.y = 0.0f;
            if (glm::length(in.move) > 1e-3f) in.move = glm::normalize(in.move) * std::min(1.0f, glm::length(move));
            sprint = map.held("sprint");
            walk = map.held("walk");
            wantCrouch = map.held("crouch");
            if (map.pressed("jump")) p.jumpQueued = true;
            if (map.pressed("reset")) loco.teleport(m_spawn + glm::vec3(1.2f * static_cast<float>(i + 1), 0.0f, 0.0f));
        } else {
            // No controller: down the lane at a run, "go up" whenever
            // the sensors see something.
            if (p.runTime < 0.0f && p.runTime + dt >= 0.0f) loco.teleport(kLaneStart);
            p.runTime += dt;
            p.rig.yaw = 0.0f;
            if (p.runTime >= 0.0f) {
                in.move = glm::vec3(0, 0, -1);
                sprint = at.z < kLaneSprintFromZ;
                const auto& ls = loco.settings();
                if (loco.state() == State::Ground && loco.probe(in.move, sprint ? ls.sprintSensor : ls.walkSensor).kind != kke::Locomotion::Obstacle::Kind::None)
                    p.jumpQueued = true;
                if (at.z < kLaneEndZ || p.runTime > kLaneMaxTime) {
                    loco.teleport(kLaneStart);
                    p.runTime = 0.0f;
                }
            }
        }
        if (!hanging && wantCrouch != p.crouch && w.setCharacterHeight(p.id, wantCrouch ? 1.0f : 1.8f)) p.crouch = wantCrouch;
        in.fast = sprint && !p.crouch;
        in.slow = walk;
        in.crouch = hanging ? wantCrouch : p.crouch;
        in.goUp = p.jumpQueued;
        p.jumpQueued = false;
        loco.update(in, dt);
        glm::vec3 feet = w.characterPosition(p.id);
        if (feet.y < -20.0f) loco.teleport(m_spawn); // fell out of the world

        // The same camera settings as player 1 (field of view, clip planes).
        const kke::Camera& main = m_app->camera();
        p.camera.fovDegrees = main.fovDegrees;
        p.camera.nearPlane = main.nearPlane;
        p.camera.farPlane = main.farPlane;
        const float pivot = p.crouch ? 0.85f : 1.5f;
        p.rig.settings.pivotHeight += (pivot - p.rig.settings.pivotHeight) * std::min(1.0f, 10.0f * dt);
        p.rig.settings.eyeHeight = p.rig.settings.pivotHeight + 0.15f;
        p.rig.update(dt, feet, ray, p.camera);

        // The character: our model with its own animator, like a network
        // player's (no IK), or a capsule without a character model.
        const float facing = loco.facingYaw();
        if (!m_charModel || !m_animSet) {
            m_avatarCapsules.push_back(glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(-facing), glm::vec3(0, 1, 0)));
            continue;
        }
        const glm::mat4 t = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(m_modelYaw - facing), glm::vec3(0, 1, 0));
        if (!p.instance) {
            p.instance = m_models->spawn(m_charModel, t);
            m_models->setOverlayEnabled(p.instance, false);
            p.anim = std::make_unique<kke::Animator>(*m_animSet);
            addAnimatorStates(*p.anim);
            p.anim->play(m_stMove, 0.0f);
        }
        MotionInfo m;
        m.state = loco.state();
        m.speed = loco.groundSpeed();
        m.progress = loco.traversalProgress();
        m.stateTime = loco.stateTime();
        m.fallHeight = loco.fallHeight();
        m.obstacleHeight = loco.lastObstacle().height;
        m.crouch = p.crouch;
        m.landed = loco.landed();
        m.jumped = loco.jumped();
        animate(*p.anim, m, dt);
        m_models->setTransform(p.instance, t);
        if (std::vector<glm::mat4>* locals = m_models->boneLocals(p.instance)) kke::poseToLocals(p.anim->pose(), *locals);
    }

    // One view per player: player 1 is the engine's camera.
    const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(m_locals.size()) + 1, m_splitSideBySide);
    views.push_back({ m_app->camera(), rects[0] });
    for (size_t i = 0; i < m_locals.size(); ++i) views.push_back({ m_locals[i].camera, rects[i + 1] });
    // The overhead view goes last, drawn over the top right corner (it
    // fits with up to three players).
    if (m_overhead && views.size() < kke::kMaxViews) {
        const glm::vec3 feet = w.characterPosition(m_player);
        kke::Camera top = m_app->camera();
        top.position = feet + glm::vec3(0.0f, 14.0f, 0.01f);
        top.target = feet;
        top.fovDegrees = 50.0f;
        views.push_back({ top, kke::pictureInPicture(1, views.size() > 1 ? 0.18f : 0.25f) });
    }
}

} // namespace kke_showcase
