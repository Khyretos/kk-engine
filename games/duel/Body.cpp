// The fighters' bodies: Quaternius' Universal Animation Library mannequin
// (CC0, assets/animations/UAL1_Standard.fbx), with UAL 2's melee clips
// (hook, uppercut, knee, knockback, getting up) added when UAL2.fbx is
// found. kke::Combatant's state picks the clip; two-bone IK raises the
// guard (hands to the chin, higher when blocking); a knockdown hands the
// skeleton to a Jolt ragdoll and blends back out of it.

#include "DuelModule.h"

#include "kke/Application.h"
#include "kke/KnownPacks.h"
#include "kke/AssetCatalog.h"
#include "kke/Log.h"
#include "kke/modules/RigidBodyModule.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <unordered_map>

namespace duel {

namespace fs = std::filesystem;

namespace {

constexpr float kGetUpTime = 0.9f; // s of the get-up clip (Combatant's knockdown covers it)

std::string firstExisting(std::initializer_list<std::string> paths) {
    std::error_code ec;
    for (const std::string& p : paths)
        if (!p.empty() && fs::exists(p, ec)) return p;
    return {};
}

} // namespace

void DuelModule::loadCharacter() {
    if (!m_models) return;
    const char* base = SDL_GetBasePath();
    const std::string dir = kke::findAssetFolder("assets/animations", { "KKE_ANIMATIONS_DIR" }, base ? base : "");
    const std::string ual1 = dir.empty() ? std::string() : (fs::path(dir) / "UAL1_Standard.fbx").string();
    if (ual1.empty() || !fs::exists(ual1)) {
        kke::log::get(name())->warn("animation library not found (assets/animations/UAL1_Standard.fbx): the fighters are blocks");
        return;
    }
    const kke::ModelModule::ModelId loaded = m_models->load(ual1);
    const kke::ModelData* d = loaded ? m_models->model(loaded) : nullptr;
    if (!d || d->bones.empty()) return;
    // The mannequin is orange; a light grey copy takes the corners' colours
    // (the tint multiplies), with its joints kept dark.
    kke::ModelData grey = *d;
    for (kke::ModelMaterial& m : grey.materials) m.baseColor = m.name.find("Joint") != std::string::npos ? glm::vec3(0.12f) : glm::vec3(0.75f);
    m_charModel = m_models->add(std::move(grey), "duel/fighter");
    d = m_models->model(m_charModel);
    m_rigData = kke::ModelData{};
    m_rigData.bones = d->bones;
    m_rigData.animations = d->animations;

    // UAL 2: next to UAL 1, or the extracted pack in the asset folder.
    const std::string ual2 = firstExisting({ dir.empty() ? std::string() : (fs::path(dir) / "UAL2.fbx").string(),
                                             kke::findPackFile("Universal Animation Library 2", "UAL2.fbx", base ? base : "") });
    if (!ual2.empty()) {
        try {
            kke::ModelLoadOptions o;
            o.allowNoMeshes = true;
            kke::appendClipsByBoneName(m_rigData, kke::loadModel(ual2, o));
            m_meleeClips = true;
        } catch (const std::exception& e) {
            kke::log::get(name())->warn("UAL2.fbx: {}", e.what());
        }
    } else {
        kke::log::get(name())->info("UAL2.fbx not found (assets/animations, or the 'Universal Animation Library 2' pack in the asset folder): "
                                    "UAL 1's punches stand in for the hook, uppercut and knee");
    }
    m_animSet = std::make_unique<kke::AnimationSet>(m_rigData);
    const kke::TwoBoneChain arms[2] = { kke::findChain(m_rigData, "upperarm_l", "lowerarm_l", "hand_l"),
                                        kke::findChain(m_rigData, "upperarm_r", "lowerarm_r", "hand_r") };
    for (int s = 0; s < 2; ++s) m_arm[s] = kke::makeHumanArm(m_rigData, arms[s], arms[1 - s]);
    const glm::vec3 fwd = kke::modelForward(m_rigData);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));
}

void DuelModule::setupBody(Fighter& f) {
    if (!m_charModel || !m_animSet) return;
    f.model = m_models->spawn(m_charModel, glm::mat4(1.0f));
    m_models->setOverlayEnabled(f.model, false);
    m_models->setTint(f.model, f.tint);
    f.anim = std::make_unique<kke::Animator>(*m_animSet);
    kke::Animator& a = *f.anim;
    const kke::AnimationSet& s = *m_animSet;
    auto pick = [&](std::initializer_list<const char*> names) {
        for (const char* n : names)
            if (int c = s.find(n); c >= 0) return c;
        return -1;
    };
    // A clip state that lasts exactly `seconds` (a move's timing is the
    // combat rules', the clip is stretched to it).
    auto timed = [&](const char* state, int clip, float seconds) {
        const float speed = clip >= 0 && seconds > 0.0f ? s.duration(clip) / seconds : 1.0f;
        return a.addClipState(state, clip, false, speed);
    };
    const kke::AttackDesc light = kke::AttackDesc::light(), heavy = kke::AttackDesc::heavy(), kick = kke::AttackDesc::kick();
    const kke::CombatStats stats = kke::CombatStats::fighter();
    // Same order on both animators, so the state numbers match.
    m_st.idle = a.addClipState("idle", pick({ "|Idle_Loop" }), true);
    m_st.fwd = a.addClipState("fwd", pick({ "|Walk_Fwd_Loop", "|Walk_Loop" }), true, 1.4f);
    m_st.back = a.addClipState("back", pick({ "|Walk_Bwd_Loop", "|Walk_Loop" }), true, 1.4f);
    m_st.left = a.addClipState("left", pick({ "|Walk_L_Loop", "|Walk_Loop" }), true, 1.4f);
    m_st.right = a.addClipState("right", pick({ "|Walk_R_Loop", "|Walk_Loop" }), true, 1.4f);
    const float lightTime = light.windup + light.active + light.recovery;
    m_st.jabL = timed("jab_l", pick({ "|Melee_Hook", "|Punch_Jab" }), lightTime);
    m_st.jabR = timed("jab_r", pick({ "|Punch_Cross", "|Punch_Jab" }), lightTime);
    m_st.heavy = timed("uppercut", pick({ "|Melee_Uppercut", "|Punch_Cross" }), heavy.windup + heavy.active + heavy.recovery);
    m_st.kick = timed("knee", pick({ "|Melee_Knee", "|Punch_Cross" }), kick.windup + kick.active + kick.recovery);
    m_st.dodge = timed("dodge", pick({ "|Walk_Bwd_Loop", "|Jump_Start" }), stats.dodgeTime + 0.15f);
    m_st.hitHigh = timed("hit_high", pick({ "|Hit_Head", "|Hit_Chest" }), light.hitStun);
    m_st.hitLow = timed("hit_low", pick({ "|Hit_Chest" }), light.hitStun);
    m_st.hitHard = timed("hit_hard", pick({ "|Hit_Knockback", "|Hit_Chest" }), heavy.hitStun);
    m_st.getUp = timed("get_up", pick({ "|LayToIdle", "|KipUp", "|Crouch_Idle_Loop" }), kGetUpTime);
    m_st.win = a.addClipState("win", pick({ "|Dance_Loop", "|Yes", "|Idle_Loop" }), true);
    a.play(m_st.idle, 0.0f);
}

void DuelModule::animateBody(Fighter& f, const Fighter& other, float dt) {
    if (!f.model || !f.anim) return;
    const kke::ModelData* d = m_models->model(m_charModel);
    if (!d) return;
    kke::RigidWorld& w = m_rigid->world();
    const kke::Combatant& c = m_combat.get(f.id);

    // A ragdoll has the skeleton: skin from its bodies.
    if (f.ragdoll) {
        std::vector<glm::mat4> bodies;
        if (m_ragdolls->ragdollBodyTransforms(f.ragdoll, bodies))
            m_models->setBoneWorldOverride(f.model, kke::poseFromRagdoll(m_rigData, f.binding, bodies, glm::inverse(m_models->transform(f.model))));
        return;
    }

    const glm::vec3 feet = w.characterDrawPosition(f.body, m_app->fixedAlpha()); // between physics steps: no 60 Hz shake
    const float yaw = m_modelYaw - glm::degrees(std::atan2(f.facing.x, -f.facing.z));
    const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(yaw), glm::vec3(0, 1, 0));
    m_models->setTransform(f.model, xf);

    // The state machine: kke::Combatant's state picks the clip.
    using S = kke::Combatant::State;
    kke::Animator& a = *f.anim;
    const int state = static_cast<int>(c.state());
    const bool entered = state != f.lastState;
    f.lastState = state;
    const bool gettingUp = f.getUpAge >= 0.0f;
    switch (c.state()) {
    case S::Windup:
        if (entered) {
            const std::string& n = c.currentAttack().name;
            int st = m_st.heavy;
            if (n == "light") {
                f.leftHand = !f.leftHand;
                st = f.leftHand ? m_st.jabL : m_st.jabR;
            } else if (n == "kick") {
                st = m_st.kick;
            }
            a.play(st, 0.06f, true);
        }
        break;
    case S::Stunned:
        if (entered) {
            const float stun = c.stateLength();
            a.play(stun > 0.5f ? m_st.hitHard : (f.leftHand ? m_st.hitHigh : m_st.hitLow), 0.05f, true);
            f.leftHand = !f.leftHand;
        }
        break;
    case S::Dodging:
        if (entered) a.play(m_st.dodge, 0.05f, true);
        break;
    case S::Knockdown:
        break; // the get-up clip plays (set by getUp())
    case S::Dead:
        break;
    case S::Idle:
    case S::Active:
    case S::Recovery:
        if (c.state() == S::Idle && !gettingUp) {
            if (m_phase == Phase::MatchOver && other.wins < f.wins) {
                if (a.current() != m_st.win) a.play(m_st.win, 0.4f);
                break;
            }
            // Footwork: the walk clip for the way the fighter is moving.
            const glm::vec3 v = w.characterVelocity(f.body);
            const glm::vec3 right = glm::normalize(glm::cross(f.facing, glm::vec3(0, 1, 0)));
            const float fwd = glm::dot(v, f.facing), side = glm::dot(v, right);
            int want = m_st.idle;
            if (std::max(std::abs(fwd), std::abs(side)) > 0.35f)
                want = std::abs(fwd) >= std::abs(side) ? (fwd > 0.0f ? m_st.fwd : m_st.back) : (side > 0.0f ? m_st.right : m_st.left);
            // Let a strike or a flinch finish blending out first.
            const bool busyClip = (a.current() != m_st.idle && a.current() != m_st.fwd && a.current() != m_st.back && a.current() != m_st.left &&
                                   a.current() != m_st.right && a.current() != m_st.win && !a.finished());
            if (!busyClip && a.current() != want) a.play(want, 0.2f);
        }
        break;
    }
    a.update(dt);

    std::vector<glm::mat4>* locals = m_models->boneLocals(f.model);
    if (!locals) return;
    kke::Pose pose = a.pose();

    // The guard: hands to the chin, up in front of the face when blocking.
    // Off while a strike or a flinch moves the arms.
    const bool free = c.state() == S::Idle && !gettingUp && m_phase != Phase::MatchOver;
    const float guardGoal = !free ? 0.0f : c.blocking() ? 1.0f : 0.55f;
    f.guard += (guardGoal - f.guard) * (1.0f - std::exp(-(guardGoal > f.guard ? 22.0f : 8.0f) * dt));
    if (f.guard > 0.01f && m_arm[0].valid() && m_arm[1].valid()) {
        const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
        const int head = m_rigData.findBone("Head");
        const glm::vec3 fwdM = kke::modelForward(m_rigData);
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 rightM = glm::normalize(glm::cross(fwdM, up));
        const glm::vec3 chin = head >= 0 ? glm::vec3(bones[static_cast<size_t>(head)][3]) : glm::vec3(0.0f, 1.6f, 0.0f);
        const float high = c.blocking() ? 1.0f : 0.0f;
        for (int s = 0; s < 2; ++s) {
            const float side = s == 0 ? -1.0f : 1.0f;
            const glm::vec3 hand = chin + fwdM * (0.22f + 0.06f * high) + rightM * (0.09f * side) + up * (-0.08f + 0.1f * high);
            const glm::vec3 shoulder(bones[static_cast<size_t>(m_arm[s].chain.upper)][3]);
            kke::ArmGoal goal;
            goal.hand = hand;
            goal.elbowToward = shoulder - up * 0.6f + rightM * (0.3f * side) + fwdM * 0.1f; // elbows down and a little out
            goal.weight = f.guard;
            kke::solveHumanArm(m_rigData, pose, m_arm[s], goal);
        }
    }

    // Getting up: blend from where the ragdoll left the body into the clip.
    if (gettingUp) {
        f.getUpAge += dt;
        const float t = kke::blendWeight(f.getUpAge, 0.45f);
        std::vector<glm::mat4> animated = kke::poseToModel(m_rigData, pose);
        if (t >= 1.0f || f.getUpFrom.empty()) {
            f.getUpAge = -1.0f;
            f.getUpFrom.clear();
            m_models->setBoneWorldOverride(f.model, {});
        } else {
            m_models->setBoneWorldOverride(f.model, kke::blendPoses(f.getUpFrom, animated, t));
        }
        if (a.finished() && f.getUpAge < 0.0f) a.play(m_st.idle, 0.3f);
    }
    kke::poseToLocals(pose, *locals);
}

} // namespace duel
