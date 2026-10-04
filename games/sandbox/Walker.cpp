#include "Walker.h"

#include "kke/AnimRig.h"
#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Equipment.h"
#include "kke/Log.h"
#include "kke/SceneLoader.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace kke_sandbox {

namespace {

// The clips' own foot speeds (m/s), so the legs match the ground.
constexpr float kWalkSpeed = 1.6f, kJogSpeed = 3.6f, kSprintSpeed = 6.2f, kCrouchSpeed = 1.4f;

const char* kLog = "Sandbox";

} // namespace

Walker::~Walker() {
    // The modules may be gone already at destruction: end() is the
    // sandbox's to call from shutdown(). Only forget what we'd point at.
    m_world = nullptr;
    m_models = nullptr;
}

void Walker::loadMannequin() {
    if (m_ualTried) return;
    m_ualTried = true;
    // Quaternius' Universal Animation Library (CC0), shipped with the
    // engine: a mannequin and 43 clips.
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string file = dir.empty() ? std::string() : (std::filesystem::path(dir) / "UAL1_Standard.fbx").string();
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        kke::log::get(kLog)->warn("assets/animations/UAL1_Standard.fbx not found: the walker is a capsule");
        return;
    }
    m_ual = m_models->load(file);
    if (const kke::ModelData* d = m_ual ? m_models->model(m_ual) : nullptr; !d || d->animations.empty()) {
        kke::log::get(kLog)->warn("'{}' has no clips: the walker is a capsule", file);
        m_ual = 0;
    }
}

void Walker::begin(kke::Application& app, kke::RigidWorld& world, const glm::vec3& feet, float yaw) {
    if (m_id) end();
    m_app = &app;
    m_world = &world;
    m_models = app.getModule<kke::ModelModule>();
    kke::RigidWorld::CharacterDesc cd;
    cd.position = feet;
    m_id = world.addCharacter(cd);
    m_loco = std::make_unique<kke::Locomotion>(world, m_id);
    const glm::vec3 dir(-std::sin(glm::radians(yaw)), 0.0f, -std::cos(glm::radians(yaw)));
    m_loco->setFacing(dir);
    m_rig.mode = kke::CameraRig::Mode::ThirdPerson;
    m_rig.yaw = yaw;
    m_rig.pitch = -12.0f;
    m_crouch = false;
    m_jump = false;
    if (!m_models) return;
    loadMannequin();
    if (!m_instance) useCharacter(nullptr, m_character);
    else m_models->setVisible(m_instance, true);
    m_ik.reset();
}

void Walker::end() {
    if (m_world && m_id) m_world->removeCharacter(m_id);
    m_id = 0;
    m_loco.reset();
    if (m_models && m_instance) m_models->setVisible(m_instance, false);
}

bool Walker::useCharacter(const kke::AssetCatalog* catalog, const std::string& asset) {
    const kke::ModelData* ual = m_ual ? m_models->model(m_ual) : nullptr;
    if (!ual) return false;
    kke::ModelModule::ModelId model = m_ual;
    if (!asset.empty() && catalog) {
        const kke::CatalogAsset* a = catalog->find(asset);
        if (!a) return false;
        model = m_models->load(a->path, kke::packLoadOptions(*catalog, *a));
    }
    const kke::ModelData* d = model ? m_models->model(model) : nullptr;
    if (!d || d->bones.empty()) {
        kke::log::get(kLog)->warn("walker: '{}' has no skeleton", asset);
        return false;
    }
    // The rig: this model's bones and the UAL clips made for them.
    m_rigData = kke::ModelData{};
    m_rigData.bones = d->bones;
    if (model == m_ual) {
        m_rigData.animations = ual->animations;
    } else {
        const kke::BoneMatch match = kke::matchBones(*ual, *d);
        m_rigData.animations = kke::retargetAnimations(*ual, *d, match);
    }
    if (m_instance) m_models->remove(m_instance);
    m_model = model;
    m_character = model == m_ual ? std::string() : asset;
    m_instance = m_models->spawn(m_model, glm::mat4(1.0f));
    m_models->setOverlayEnabled(m_instance, false);
    m_models->setVisible(m_instance, m_id != 0);
    buildAnimator();
    m_ik = kke::CharacterIk(m_rigData, d); // the body's capsules fitted to this mesh
    const kke::HandRig left = kke::makeHandRig(m_rigData, true), right = kke::makeHandRig(m_rigData, false);
    m_handBone[0] = left.hand;
    m_handBone[1] = right.hand;
    const glm::vec3 fwd = kke::modelForward(m_rigData);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
    m_lastBones.clear();
    return true;
}

void Walker::buildAnimator() {
    m_set = std::make_unique<kke::AnimationSet>(m_rigData);
    m_anim = std::make_unique<kke::Animator>(*m_set);
    const kke::AnimationSet& s = *m_set;
    m_stMove = m_anim->addBlendState("move", { { { s.find("|Idle_Loop"), 0.0f },
                                                 { s.find("|Walk_Loop"), kWalkSpeed },
                                                 { s.find("Jog_Fwd_Loop"), kJogSpeed },
                                                 { s.find("Sprint_Loop"), kSprintSpeed } } });
    m_stCrouch = m_anim->addBlendState("crouch", { { { s.find("Crouch_Idle_Loop"), 0.0f }, { s.find("Crouch_Fwd_Loop"), kCrouchSpeed } } });
    m_stJump = m_anim->addClipState("jump", s.find("Jump_Start"), false, 2.0f);
    m_stFall = m_anim->addClipState("fall", s.find("Jump_Loop"), true);
    m_stLand = m_anim->addClipState("land", s.find("Jump_Land"), false, 1.8f);
    m_stOnce = m_stHold = -1;
    m_onceClip.clear();
    const std::string hold = m_holdClip;
    m_holdClip.clear();
    holdPose(hold);
    m_anim->play(m_stMove, 0.0f);
}

bool Walker::playOnce(const std::string& clip, float speed) {
    if (!m_anim || !m_set) return false;
    const int c = m_set->find(clip);
    if (c < 0) return false;
    if (clip != m_onceClip) {
        m_stOnce = m_anim->addClipState("once:" + clip, c, false, speed);
        m_onceClip = clip;
    }
    m_anim->play(m_stOnce, 0.08f, true);
    return true;
}

void Walker::holdPose(const std::string& clip) {
    if (clip == m_holdClip) return;
    m_holdClip = clip;
    m_stHold = -1;
    if (clip.empty() || !m_anim || !m_set) return;
    const int c = m_set->find(clip);
    if (c >= 0) m_stHold = m_anim->addClipState("hold:" + clip, c, true);
}

void Walker::hand(kke::CharacterIk::Side side, const glm::vec3& point, const std::optional<glm::vec3>& elbow) {
    m_hands[side] = { point, elbow, true };
}

glm::vec3 Walker::feet() const {
    return m_world && m_id ? m_world->characterDrawPosition(m_id, m_app->fixedAlpha()) : glm::vec3(0.0f);
}
glm::vec3 Walker::velocity() const { return m_world && m_id ? m_world->characterVelocity(m_id) : glm::vec3(0.0f); }
float Walker::facingYaw() const { return m_loco ? m_loco->facingYaw() : 0.0f; }
glm::vec3 Walker::facing() const { return m_loco ? m_loco->facing() : glm::vec3(0.0f, 0.0f, -1.0f); }
void Walker::face(const glm::vec3& direction) {
    if (m_loco && glm::length(glm::vec2(direction.x, direction.z)) > 1e-4f) m_loco->setFacing(direction);
}
void Walker::teleport(const glm::vec3& at) {
    if (m_loco) m_loco->teleport(at);
    m_ik.reset();
}

glm::vec3 Walker::shoulder(kke::CharacterIk::Side side) const {
    const kke::HumanArm& arm = m_ik.arm(side);
    if (arm.valid() && static_cast<size_t>(arm.chain.upper) < m_lastBones.size())
        return glm::vec3(m_lastWorld * m_lastBones[static_cast<size_t>(arm.chain.upper)][3]);
    const glm::vec3 right(-facing().z, 0.0f, facing().x);
    return feet() + glm::vec3(0.0f, 1.42f, 0.0f) + right * (side == kke::CharacterIk::Right ? 0.18f : -0.18f);
}

bool Walker::palm(kke::CharacterIk::Side side, glm::mat4& out) const {
    const int bone = m_handBone[side];
    if (bone < 0 || static_cast<size_t>(bone) >= m_lastBones.size()) return false;
    const kke::HandRig rig = kke::makeHandRig(m_rigData, side == kke::CharacterIk::Left);
    out = m_lastWorld * rig.palmSocket().world(m_lastBones);
    return true;
}

void Walker::update(float dt, float alpha, const Controls& c, kke::Camera& camera) {
    if (!m_id || !m_loco) return;
    (void)alpha;
    m_rig.addLook(c.look.x, c.look.y);
    if (c.toggleView) m_rig.mode = firstPerson() ? kke::CameraRig::Mode::ThirdPerson : kke::CameraRig::Mode::FirstPerson;
    if (c.zoom != 0.0f) m_rig.settings.armLength = std::clamp(m_rig.settings.armLength - c.zoom, 1.5f, 10.0f);
    if (c.jump) m_jump = true;
    m_crouch = c.crouch;

    // Move relative to where the camera looks; what "jump" becomes (a
    // jump, a vault, a climb) is Locomotion's call.
    kke::Locomotion::Input in;
    in.move = m_rig.forward() * c.move.y + m_rig.right() * c.move.x;
    in.move.y = 0.0f;
    if (glm::length(in.move) > 1e-3f) in.move = glm::normalize(in.move) * std::min(1.0f, glm::length(c.move));
    in.fast = c.sprint && !m_crouch;
    in.slow = c.walk;
    in.crouch = m_crouch;
    in.goUp = m_jump;
    m_jump = false;
    if (firstPerson()) m_loco->setFacing(m_rig.forward());
    m_loco->update(in, dt);
    if (m_world->characterPosition(m_id).y < -20.0f) m_loco->teleport(glm::vec3(0.0f, 0.1f, 6.0f)); // fell off the world

    const glm::vec3 drawFeet = feet();
    animate(dt);
    if (m_instance) {
        // The model faces -Z (the mannequin does; Synty people face +Z and
        // are turned by m_modelYaw), then turns like the rig at yaw 0.
        const float yaw = firstPerson() ? m_rig.yaw : m_loco->facingYaw();
        m_lastWorld = glm::rotate(glm::translate(glm::mat4(1.0f), drawFeet), glm::radians(m_modelYaw - yaw), glm::vec3(0, 1, 0));
        m_models->setTransform(m_instance, m_lastWorld);
        m_models->setVisible(m_instance, !firstPerson());
        applyIk(dt);
    }
    // The camera stays out of the level, but not out of crates and
    // ragdolls: only static bodies stop the arm (a crate rolling past
    // would make it jump in and out).
    kke::RigidWorld& w = *m_world;
    m_rig.update(dt, drawFeet, [&w](const glm::vec3& from, const glm::vec3& d, float maxD) {
        const kke::RigidWorld::RayHit h = w.raycast(from, d, maxD, [](kke::RigidWorld::BodyId, kke::RigidWorld::Motion m) { return m == kke::RigidWorld::Motion::Static; });
        return h.hit ? h.distance : maxD;
    }, camera);
}

void Walker::animate(float dt) {
    if (!m_anim) return;
    using State = kke::Locomotion::State;
    if (m_loco->jumped()) m_anim->play(m_stJump, 0.08f, true);
    const int cur = m_anim->current();
    const bool once = cur == m_stOnce && m_stOnce >= 0 && !m_anim->finished();
    switch (m_loco->state()) {
    case State::Air:
        if (once) break;
        if (cur == m_stJump && m_anim->finished()) m_anim->play(m_stFall, 0.15f);
        else if (cur != m_stJump && cur != m_stFall && m_loco->stateTime() > 0.15f) m_anim->play(m_stFall, 0.2f);
        break;
    case State::Ground: {
        if (m_loco->landed() && m_loco->fallHeight() > 0.6f) m_anim->play(m_stLand, 0.06f);
        if (once) break;
        const float speed = m_loco->groundSpeed();
        // Holding a tool standing still: its pose (the pistol held up).
        const int ground = m_crouch ? m_stCrouch : (m_stHold >= 0 && speed < 0.4f) ? m_stHold : m_stMove;
        const bool landing = m_anim->current() == m_stLand && !m_anim->finished() && speed < 1.0f;
        const bool jumping = m_anim->current() == m_stJump && m_anim->stateTime() < 0.2f;
        if (!landing && !jumping && m_anim->current() != ground) m_anim->play(ground, m_loco->landed() ? 0.12f : 0.2f);
        break;
    }
    default: // vaults, climbs, hanging: the fall pose; IK puts the hands on the edge
        if (cur != m_stFall && !once) m_anim->play(m_stFall, 0.12f);
        break;
    }
    m_anim->setParameter(m_loco->groundSpeed());
    m_anim->update(dt);
}

void Walker::applyIk(float dt) {
    std::vector<glm::mat4>* locals = m_models->boneLocals(m_instance);
    if (!locals || !m_anim) return;
    kke::Pose pose = m_anim->pose();
    using State = kke::Locomotion::State;
    using Side = kke::CharacterIk::Side;
    const State st = m_loco->state();
    kke::RigidWorld& w = *m_world;
    auto ground = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        const kke::RigidWorld::RayHit h = w.raycast(from, glm::vec3(0, -1, 0), 1.2f);
        if (!h.hit || h.normal.y < 0.5f) return false;
        hit = h.point;
        normal = h.normal;
        return true;
    };
    m_ik.feetOnGround(st == State::Ground);
    // Hands: on the ledge while hanging or climbing, else where the
    // sandbox put them (a tool's handle); the rest follow the clip.
    if (st == State::Hang || st == State::Climb) {
        const glm::vec3 edge = st == State::Hang ? m_loco->hangEdge() : m_loco->lastObstacle().face + glm::vec3(0.0f, m_loco->lastObstacle().height, 0.0f);
        const glm::vec3 side(-facing().z, 0.0f, facing().x);
        m_ik.hand(Side::Left, edge - side * 0.22f);
        m_ik.hand(Side::Right, edge + side * 0.22f);
    } else {
        for (int i = 0; i < 2; ++i)
            if (m_hands[i].set) m_ik.hand(static_cast<Side>(i), m_hands[i].point, m_hands[i].elbow);
    }
    m_ik.apply(m_rigData, pose, m_lastWorld, ground, w.characterVelocity(m_id), dt);
    m_hands[0].set = m_hands[1].set = false;
    kke::poseToLocals(pose, *locals);
    m_lastBones = kke::poseToModel(m_rigData, pose);
}

} // namespace kke_sandbox
