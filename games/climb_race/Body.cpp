// The climbers' bodies: the UAL mannequin (Quaternius' Universal Animation
// Library, CC0, assets/animations/UAL1_Standard.fbx) on the capsule. On
// foot it runs the usual clips from kke::Locomotion's state; on the rock a
// hanging pose, and two-bone IK puts each hand on its hold and each foot
// on its foothold, so what you see is exactly what kke::Climber holds.

#include "ClimbRaceModule.h"

#include "kke/Application.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <filesystem>
#include <algorithm>

namespace climb_race {

namespace {
constexpr float kWalkSpeed = 1.6f, kJogSpeed = 3.6f, kSprintSpeed = 6.2f;
float yawOf(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, -d.z)); }
} // namespace

void ClimbRaceModule::loadCharacter() {
    if (!m_models) return;
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string file = dir.empty() ? std::string() : (std::filesystem::path(dir) / "UAL1_Standard.fbx").string();
    if (file.empty() || !std::filesystem::exists(file)) {
        kke::log::get(name())->warn("animation library not found (assets/animations/UAL1_Standard.fbx): the climbers are blocks");
        return;
    }
    const kke::ModelModule::ModelId loaded = m_models->load(file);
    const kke::ModelData* d = loaded ? m_models->model(loaded) : nullptr;
    if (!d || d->bones.empty() || d->animations.empty()) return;
    // The mannequin is orange; a light grey copy takes each climber's
    // colour from the menu (the tint multiplies), with its joints kept dark.
    kke::ModelData grey = *d;
    for (kke::ModelMaterial& m : grey.materials) m.baseColor = m.name.find("Joint") != std::string::npos ? glm::vec3(0.12f) : glm::vec3(0.8f);
    m_charModel = m_models->add(std::move(grey), "climb_race/climber");
    d = m_models->model(m_charModel);
    m_rigData = kke::ModelData{};
    m_rigData.bones = d->bones;
    m_rigData.animations = d->animations;
    m_animSet = std::make_unique<kke::AnimationSet>(m_rigData);
    for (int s = 0; s < 2; ++s) {
        m_arm[s] = kke::findChain(m_rigData, s == 0 ? "upperarm_l" : "upperarm_r", s == 0 ? "lowerarm_l" : "lowerarm_r", s == 0 ? "hand_l" : "hand_r");
        m_leg[s] = kke::findChain(m_rigData, s == 0 ? "thigh_l" : "thigh_r", s == 0 ? "calf_l" : "calf_r", s == 0 ? "foot_l" : "foot_r");
    }
    int pelvis = -1;
    for (size_t b = 0; b < m_rigData.bones.size(); ++b)
        if (kke::canonicalBoneName(m_rigData.bones[b].name) == "pelvis") pelvis = static_cast<int>(b);
    m_feet = kke::FootPlacer(m_rigData, m_leg[0], m_leg[1], pelvis);
    const glm::vec3 fwd = kke::modelForward(m_rigData);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
}

void ClimbRaceModule::setupBody(Racer& r) {
    if (!m_charModel || !m_animSet) return;
    r.model = m_models->spawn(m_charModel, glm::mat4(1.0f));
    m_models->setOverlayEnabled(r.model, false);
    m_models->setTint(r.model, r.tint);
    r.anim = std::make_unique<kke::Animator>(*m_animSet);
    kke::Animator& a = *r.anim;
    const kke::AnimationSet& s = *m_animSet;
    auto pick = [&](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (int c = s.find(n); c >= 0) return c;
        return -1;
    };
    // Same order on every animator, so the state numbers match.
    m_stMove = a.addBlendState("move", { { { s.find("|Idle_Loop"), 0.0f },
                                          { s.find("|Walk_Loop"), kWalkSpeed },
                                          { s.find("Jog_Fwd_Loop"), kJogSpeed },
                                          { s.find("Sprint_Loop"), kSprintSpeed } } });
    m_stJump = a.addClipState("jump", s.find("Jump_Start"), false, 2.0f);
    m_stFall = a.addClipState("fall", s.find("Jump_Loop"), true);
    m_stLand = a.addClipState("land", s.find("Jump_Land"), false, 1.8f);
    // On the rock: the tucked jump pose, slowed right down; IK does the rest.
    m_stHang = a.addClipState("hang", pick({ "Hang_Idle", "Jump_Loop" }), true, 0.25f);
    // Over the top: a crouch while the legs come up under the body.
    m_stTop = a.addClipState("top", pick({ "Crouch_Idle_Loop", "Idle_Loop" }), true);
    a.play(m_stMove, 0.0f);
}

ClimbRaceModule::BodyInput ClimbRaceModule::bodyInput(const Racer& r) const {
    BodyInput b;
    const kke::RigidWorld& w = m_rigid->world();
    b.feet = w.characterPosition(r.id);
    if (r.remote) {
        // Online: what its own machine sent (Net.cpp keeps the jump and
        // landing flags from the state's changes).
        const netrace::Pose& p = r.net;
        b.yaw = p.yaw;
        const float rad = glm::radians(p.yaw);
        b.in = glm::vec3(std::sin(rad), 0.0f, -std::cos(rad));
        b.climbing = p.climbing;
        b.mantle = p.mantle;
        b.mantleProgress = p.mantleProgress;
        b.loco = static_cast<kke::Locomotion::State>(std::min<uint8_t>(p.loco, static_cast<uint8_t>(kke::Locomotion::State::WallRun)));
        b.stateTime = r.netStateTime;
        b.groundSpeed = p.groundSpeed;
        b.fallHeight = p.fallHeight;
        b.jumped = r.netJumped;
        b.landed = r.netLanded;
        for (int s = 0; s < 2; ++s) {
            b.hand[s] = p.hand[s];
            b.foot[s] = p.foot[s];
        }
        return b;
    }
    const kke::Climber& c = *r.climber;
    b.climbing = c.climbing();
    b.mantle = c.state() == kke::Climber::State::Mantle;
    b.mantleProgress = b.mantle ? c.mantleProgress() : 0.0f;
    b.yaw = b.climbing ? yawOf(c.facing()) : r.loco->facingYaw();
    b.in = b.climbing ? c.facing() : r.loco->facing();
    b.loco = r.loco->state();
    b.stateTime = r.loco->stateTime();
    b.groundSpeed = r.loco->groundSpeed();
    b.fallHeight = r.loco->fallHeight();
    b.jumped = r.loco->jumped();
    b.landed = r.loco->landed();
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 right = glm::normalize(glm::cross(b.in, up)); // the climber's right
    for (int s = 0; s < 2; ++s) {
        const float side = s == 0 ? -1.0f : 1.0f;
        if (b.climbing) {
            b.hand[s] = toWorld(r, c.hand(s));
        } else {
            // Locomotion's own ledge hang: shoulder-width on the edge.
            b.hand[s] = r.loco->hangEdge() + right * (0.22f * side) + b.in * 0.08f + up * 0.02f;
        }
        b.foot[s] = toWorld(r, c.foot(s));
    }
    return b;
}

void ClimbRaceModule::animateBody(Racer& r, float dt) {
    if (!r.model || !r.anim) return;
    kke::RigidWorld& w = m_rigid->world();
    kke::Animator& a = *r.anim;
    const BodyInput b = bodyInput(r);
    const bool climbing = b.climbing;
    const glm::vec3 feet = b.feet;
    const float yaw = b.yaw;
    const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(m_modelYaw - yaw), glm::vec3(0, 1, 0));
    m_models->setTransform(r.model, xf);

    // The state machine: the rock, or kke::Locomotion's state.
    using LS = kke::Locomotion::State;
    const int cur = a.current();
    if (climbing) {
        const int want = b.mantle && b.mantleProgress > 0.45f ? m_stTop : m_stHang;
        if (cur != want) a.play(want, 0.15f);
    } else {
        const LS st = b.loco;
        if (b.jumped) a.play(m_stJump, 0.08f, true);
        if (st == LS::Ground) {
            if (b.landed && b.fallHeight > 0.6f) a.play(m_stLand, 0.06f);
            const bool landing = a.current() == m_stLand && !a.finished();
            const bool jumping = a.current() == m_stJump && a.stateTime() < 0.2f;
            if (!landing && !jumping && a.current() != m_stMove) a.play(m_stMove, 0.2f);
        } else if (st == LS::Hang) {
            if (cur != m_stHang) a.play(m_stHang, 0.12f);
        } else if (cur == m_stJump && a.finished()) {
            a.play(m_stFall, 0.15f);
        } else if (cur != m_stJump && cur != m_stFall && b.stateTime > 0.15f) {
            a.play(m_stFall, 0.2f);
        }
    }
    a.setParameter(b.groundSpeed);
    a.update(dt);

    std::vector<glm::mat4>* locals = m_models->boneLocals(r.model);
    if (!locals) return;
    kke::Pose pose = a.pose();
    const glm::mat4 inv = glm::inverse(xf);
    auto model = [&](const glm::vec3& p) { return glm::vec3(inv * glm::vec4(p, 1.0f)); };
    const float k = 1.0f - std::exp(-12.0f * dt);

    // Feet on the ground when walking about.
    const bool ground = !climbing && b.loco == LS::Ground;
    r.footWeight += ((ground ? 1.0f : 0.0f) - r.footWeight) * k;
    auto groundQuery = [&](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
        const glm::vec3 start = glm::vec3(xf * glm::vec4(from, 1.0f));
        const kke::RigidWorld::RayHit h = w.raycast(start, glm::vec3(0, -1, 0), 1.2f);
        if (!h.hit || h.normal.y < 0.5f) return false;
        hit = model(h.point);
        normal = glm::normalize(glm::mat3(inv) * h.normal);
        return true;
    };
    if (m_feet.valid() && r.footWeight > 0.01f) m_feet.apply(m_rigData, pose, kke::FootPlacer::SurfaceQuery(groundQuery), dt, r.footWeight);

    // On the rock: hands on their holds (or on their way), feet on theirs.
    const float mantle = b.mantle ? b.mantleProgress : 0.0f;
    const float armGoal = climbing ? 1.0f - std::clamp((mantle - 0.55f) / 0.3f, 0.0f, 1.0f) : b.loco == LS::Hang ? 1.0f : 0.0f;
    const float legGoal = climbing ? 1.0f - std::clamp((mantle - 0.25f) / 0.3f, 0.0f, 1.0f) : 0.0f;
    r.armWeight += (armGoal - r.armWeight) * k;
    r.legWeight += (legGoal - r.legWeight) * k;
    if (r.armWeight > 0.01f || r.legWeight > 0.01f) {
        const glm::vec3 in = b.in; // into the rock
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(in, up)); // the climber's right
        const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
        for (int s = 0; s < 2; ++s) {
            const float side = s == 0 ? -1.0f : 1.0f;
            if (m_arm[s].valid() && r.armWeight > 0.01f) {
                const glm::vec3 hand = b.hand[s];
                const glm::vec3 shoulder = glm::vec3(xf * bones[static_cast<size_t>(m_arm[s].upper)][3]);
                // Elbows down and out, away from the rock.
                const glm::vec3 pole = shoulder - up * 0.5f + right * (0.45f * side) - in * 0.25f;
                kke::solveTwoBone(m_rigData, pose, m_arm[s], model(hand), model(pole), r.armWeight);
            }
            if (m_leg[s].valid() && r.legWeight > 0.01f) {
                // The ankle sits a little out from the foothold and above it.
                const glm::vec3 foot = b.foot[s] - in * 0.1f + up * 0.07f;
                const glm::vec3 hip = glm::vec3(xf * bones[static_cast<size_t>(m_leg[s].upper)][3]);
                // Knees toward the rock and a little out, like a frog.
                const glm::vec3 pole = hip + in * 0.6f + right * (0.35f * side) - up * 0.2f;
                kke::solveTwoBone(m_rigData, pose, m_leg[s], model(foot), model(pole), r.legWeight);
            }
        }
    }
    kke::poseToLocals(pose, *locals);
}

} // namespace climb_race
