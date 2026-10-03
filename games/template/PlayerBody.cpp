#include "PlayerBody.h"

#include "kke/AssetCatalog.h"
#include "kke/Locomotion.h"
#include "kke/Log.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>

namespace starter {

PlayerBody::PlayerBody() = default;
PlayerBody::~PlayerBody() = default;

bool PlayerBody::load(kke::ModelModule& models) {
    m_models = &models;
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::filesystem::path file = dir.empty() ? std::filesystem::path() : std::filesystem::path(dir) / "UAL1_Standard.fbx";
    std::error_code ec;
    if (file.empty() || !std::filesystem::exists(file, ec)) {
        kke::log::get("Player")->info("assets/animations/UAL1_Standard.fbx not found: the player is a block");
        return false;
    }
    m_model = models.load(file.string());
    m_data = m_model ? models.model(m_model) : nullptr;
    if (!m_data || m_data->bones.empty() || m_data->animations.empty()) return false;

    // The clips as poses, and the states the player moves through. One
    // number, the speed, blends standing, walking, jogging and sprinting
    // (Locomotion's speeds, so the feet never slide).
    m_set = std::make_unique<kke::AnimationSet>(*m_data);
    m_anim = std::make_unique<kke::Animator>(*m_set);
    const kke::Locomotion::Settings speeds;
    m_move = m_anim->addBlendState("move", { { { m_set->find("|Idle_Loop"), 0.0f },
                                               { m_set->find("|Walk_Loop"), speeds.walkSpeed },
                                               { m_set->find("Jog_Fwd_Loop"), speeds.runSpeed },
                                               { m_set->find("Sprint_Loop"), speeds.sprintSpeed } } });
    m_jump = m_anim->addClipState("jump", m_set->find("Jump_Start"), false, 2.0f);
    m_fall = m_anim->addClipState("fall", m_set->find("Jump_Loop"), true);
    m_land = m_anim->addClipState("land", m_set->find("Jump_Land"), false, 1.8f);
    // This set has no vault or climb clips: the tucked jump stands in, and
    // the hands go on the edge with IK.
    m_vault = m_anim->addClipState("vault", m_set->find("Jump_Loop"), true, 1.4f);
    m_climb = m_anim->addClipState("climb", m_set->find("Jump_Start"), false, 0.7f);
    m_anim->play(m_move, 0.0f);

    m_ik = kke::CharacterIk(*m_data, m_data);
    const glm::vec3 fwd = kke::modelForward(*m_data);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
    m_instance = models.spawn(m_model);
    return true;
}

void PlayerBody::update(const kke::Locomotion& loco, kke::RigidWorld& world, kke::RigidWorld::CharacterId id, const glm::vec3& feet,
                        bool visible, float dt) {
    if (!m_instance) return;
    m_models->setVisible(m_instance, visible);
    m_models->setTransform(m_instance, glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(m_modelYaw - loco.facingYaw()),
                                                   glm::vec3(0, 1, 0)));
    animate(loco);
    m_anim->update(dt);
    pose(loco, world, id, dt);
}

// Which clip: what Locomotion is doing picks it.
void PlayerBody::animate(const kke::Locomotion& loco) {
    using State = kke::Locomotion::State;
    kke::Animator& a = *m_anim;
    if (loco.jumped()) a.play(m_jump, 0.08f, true);
    const int cur = a.current();
    switch (loco.state()) {
    case State::Vault:
        if (cur != m_vault) a.play(m_vault, 0.08f);
        break;
    case State::Climb:
    case State::Hang:
        if (cur != m_climb) a.play(m_climb, 0.1f);
        break;
    case State::Air:
    case State::Leap:
    case State::WallRun:
        if (cur == m_jump && a.finished()) a.play(m_fall, 0.15f);
        // Walked off an edge (not a jump): fall after a moment.
        else if (cur != m_jump && cur != m_fall && loco.stateTime() > 0.15f) a.play(m_fall, 0.2f);
        break;
    case State::Ground: {
        if (loco.landed() && loco.fallHeight() > 0.6f) a.play(m_land, 0.06f);
        const bool landing = a.current() == m_land && !a.finished() && loco.groundSpeed() < 1.0f;
        const bool jumping = a.current() == m_jump && a.stateTime() < 0.2f;
        if (!landing && !jumping && a.current() != m_move) a.play(m_move, loco.landed() ? 0.12f : 0.2f);
        break;
    }
    }
    a.setParameter(loco.groundSpeed()); // the measured speed: the legs match the ground, in turns too
}

// The clip is the base; CharacterIk puts the feet and hands where the
// world says they are and leans the body into its speed changes.
void PlayerBody::pose(const kke::Locomotion& loco, kke::RigidWorld& world, kke::RigidWorld::CharacterId id, float dt) {
    std::vector<glm::mat4>* locals = m_models->boneLocals(m_instance);
    if (!locals) return;
    using State = kke::Locomotion::State;
    kke::Pose pose = m_anim->pose();
    const State st = loco.state();
    const float progress = loco.traversalProgress();
    auto ground = [&world](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        const kke::RigidWorld::RayHit h = world.raycast(from, glm::vec3(0, -1, 0), 1.2f);
        if (!h.hit || h.normal.y < 0.5f) return false;
        hit = h.point;
        normal = h.normal;
        return true;
    };
    m_ik.feetOnGround(st == State::Ground || (st == State::Climb && progress > 0.75f));
    // Hands on the top edge while going over it.
    if ((st == State::Vault && progress < 0.45f) || (st == State::Climb && progress < 0.7f) || st == State::Hang) {
        const kke::Locomotion::Obstacle& o = loco.lastObstacle();
        const glm::vec3 in = -o.normal;
        const glm::vec3 side(in.z, 0.0f, -in.x);
        const glm::vec3 grip = st == State::Hang ? loco.hangEdge() : glm::vec3(o.face.x, o.target.y, o.face.z);
        const glm::vec3 edge(grip.x + in.x * 0.08f, grip.y + 0.02f, grip.z + in.z * 0.08f);
        // Left hand to the character's left (-side seen facing the wall).
        for (int i = 0; i < 2; ++i) {
            const float s = i == kke::CharacterIk::Left ? 1.0f : -1.0f;
            const glm::vec3 hand = edge + side * (0.22f * s);
            m_ik.hand(static_cast<kke::CharacterIk::Side>(i), hand, hand - in * 0.3f + side * (0.35f * s) - glm::vec3(0, 0.5f, 0));
        }
    }
    const glm::vec3 velocity = st == State::Ground || st == State::Air ? world.characterVelocity(id) : glm::vec3(0.0f);
    m_ik.apply(*m_data, pose, m_models->transform(m_instance), ground, velocity, dt);
    kke::poseToLocals(pose, *locals);
}

} // namespace starter
