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

    // More clips, by bone name onto the same skeleton: UAL 2 (the hook,
    // uppercut, knee, knockback, getting up) next to UAL 1 or in the asset
    // folder, and the whole UAL 1 (a kick, dodges, body hits) when it's
    // there. Clips the rig already has are skipped.
    auto append = [&](const std::string& file) {
        if (file.empty()) return false;
        try {
            kke::ModelLoadOptions o;
            o.allowNoMeshes = true;
            kke::ModelData more = kke::loadModel(file, o);
            std::erase_if(more.animations, [&](const kke::ModelAnimation& a) {
                return std::any_of(m_rigData.animations.begin(), m_rigData.animations.end(),
                                   [&](const kke::ModelAnimation& have) { return have.name == a.name; });
            });
            kke::appendClipsByBoneName(m_rigData, more);
            return true;
        } catch (const std::exception& e) {
            kke::log::get(name())->warn("{}: {}", file, e.what());
            return false;
        }
    };
    m_meleeClips = append(firstExisting({ dir.empty() ? std::string() : (fs::path(dir) / "UAL2.fbx").string(),
                                          kke::findPackFile("Universal Animation Library 2", "UAL2.fbx", base ? base : "") }));
    if (!m_meleeClips)
        kke::log::get(name())->info("UAL2.fbx not found (assets/animations, or the 'Universal Animation Library 2' pack in the asset folder): "
                                    "UAL 1's punches stand in for the hook, uppercut and knee");
    m_fullUal1 = append(firstExisting({ dir.empty() ? std::string() : (fs::path(dir) / "UAL1.fbx").string(),
                                        kke::findPackFile("Universal Animation Library", "UAL1.fbx", base ? base : "") }));
    m_animSet = std::make_unique<kke::AnimationSet>(m_rigData);
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
    auto strike = [&](const char* state, std::initializer_list<const char*> clips, const char* attack) {
        const kke::AttackDesc d = attackNamed(attack);
        return timed(state, pick(clips), d.windup + d.active + d.recovery);
    };
    const kke::CombatStats stats = kke::CombatStats::fighter();
    // Same order on both animators, so the state numbers match.
    m_st.idle = a.addClipState("idle", pick({ "|Idle_Loop" }), true);
    m_st.fwd = a.addClipState("fwd", pick({ "|Walk_Fwd_Loop", "|Walk_Loop" }), true, 1.4f);
    m_st.back = a.addClipState("back", pick({ "|Walk_Bwd_Loop", "|Walk_Loop" }), true, 1.4f);
    m_st.left = a.addClipState("left", pick({ "|Walk_L_Loop", "|Walk_Loop" }), true, 1.4f);
    m_st.right = a.addClipState("right", pick({ "|Walk_R_Loop", "|Walk_Loop" }), true, 1.4f);
    // Each strike its own clip, the best one there is (UAL 2, then UAL 1).
    m_st.jab = strike("jab", { "|Punch_Jab" }, "jab");
    m_st.cross = strike("cross", { "|Punch_Cross", "|Punch_Jab" }, "cross");
    m_st.hook = strike("hook", { "|Melee_Hook", "|Punch_Cross" }, "hook");
    m_st.heavy = strike("uppercut", { "|Melee_Uppercut", "|Punch_Cross" }, "uppercut");
    m_st.knee = strike("knee", { "|Melee_Knee", "|Kick", "|Punch_Cross" }, "knee");
    m_st.kick = s.find("|Kick") >= 0 ? strike("kick", { "|Kick" }, "kick") : -1; // only with the whole UAL 1
    m_st.dodge = timed("dodge", pick({ "|Walk_Bwd_Loop", "|Jump_Start" }), stats.dodgeTime + 0.15f);
    m_st.dodgeL = timed("dodge_l", pick({ "|Dodge_Left", "|Walk_L_Loop", "|Walk_Bwd_Loop" }), stats.dodgeTime + 0.25f);
    m_st.dodgeR = timed("dodge_r", pick({ "|Dodge_Right", "|Walk_R_Loop", "|Walk_Bwd_Loop" }), stats.dodgeTime + 0.25f);
    const float stun = kke::AttackDesc::light().hitStun;
    m_st.hitHigh = timed("hit_high", pick({ "|Hit_Head", "|Hit_Chest" }), stun);
    m_st.hitLow = timed("hit_low", pick({ "|Hit_Chest" }), stun);
    m_st.hitStomach = timed("hit_stomach", pick({ "|Hit_Stomach", "|Hit_Chest" }), kke::AttackDesc::kick().hitStun);
    m_st.hitHard = timed("hit_hard", pick({ "|Hit_Knockback", "|Hit_Chest" }), kke::AttackDesc::heavy().hitStun);
    m_st.getUp = timed("get_up", pick({ "|LayToIdle", "|KipUp", "|Crouch_Idle_Loop" }), kGetUpTime);
    m_st.win = a.addClipState("win", pick({ "|Celebration", "|Dance_Loop", "|Yes", "|Idle_Loop" }), true);
    // Feet on the canvas, the lean into footwork, the guard's hands (a
    // human arm that stays out of its own body: kke::CharacterIk).
    const kke::ModelData* skinned = m_models->model(m_charModel);
    f.ik = std::make_unique<kke::CharacterIk>(m_rigData, skinned);
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
        if (f.ik) f.ik->reset();
        return;
    }

    const glm::vec3 feet = w.characterDrawPosition(f.body, m_app->fixedAlpha()); // between physics steps: no 60 Hz shake
    const float yaw = m_modelYaw - glm::degrees(std::atan2(f.facing.x, -f.facing.z));
    const glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1.0f), feet), glm::radians(yaw), glm::vec3(0, 1, 0));
    m_models->setTransform(f.model, xf);
    const glm::vec3 velocity = dt > 0.0f ? (feet - f.lastFeet) / dt : glm::vec3(0.0f);
    f.lastFeet = feet;

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
            int st = m_st.jab;
            if (n == "cross") st = m_st.cross;
            else if (n == "hook") st = m_st.hook;
            else if (n == "uppercut") st = m_st.heavy;
            else if (n == "knee") st = m_st.knee;
            else if (n == "kick") st = m_st.kick >= 0 ? m_st.kick : m_st.knee;
            a.play(st, 0.06f, true);
        }
        break;
    case S::Stunned:
        if (entered) {
            // The reaction fits the blow: the head snaps back from a punch,
            // a knee folds the body, an uppercut throws it.
            int st = f.leftHand ? m_st.hitHigh : m_st.hitLow;
            if (c.stateLength() > 0.5f || f.lastHit == "uppercut") st = m_st.hitHard;
            else if (f.lastHit == "knee" || f.lastHit == "kick") st = m_st.hitStomach;
            else if (f.lastHit == "jab" || f.lastHit == "cross" || f.lastHit == "hook") st = m_st.hitHigh;
            a.play(st, 0.05f, true);
            f.leftHand = !f.leftHand;
        }
        break;
    case S::Dodging:
        if (entered) a.play(f.dodgeSide < 0.0f ? m_st.dodgeL : f.dodgeSide > 0.0f ? m_st.dodgeR : m_st.dodge, 0.05f, true);
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

    // A boxer's stance on top of the clip (the mannequin's idle and walks
    // stand up straight): knees bent, the body bladed (lead shoulder
    // forward), leaning in, chin down, hands up. Off while a strike, a
    // flinch or a dodge moves the body.
    const bool free = c.state() == S::Idle && !gettingUp && m_phase != Phase::MatchOver;
    const float guardGoal = !free ? 0.0f : c.blocking() ? 1.0f : 0.7f;
    f.guard += (guardGoal - f.guard) * (1.0f - std::exp(-(guardGoal > f.guard ? 22.0f : 8.0f) * dt));
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const glm::vec3 fwdM = kke::modelForward(m_rigData);
    const glm::vec3 rightM = glm::normalize(glm::cross(fwdM, up));
    const float stance = std::min(1.0f, f.guard / 0.7f);
    if (stance > 0.01f) {
        auto turn = [&](const char* bone, const glm::vec3& axis, float degrees) {
            const int b = m_rigData.findBone(bone);
            if (b < 0) return;
            const std::vector<glm::mat4> world = kke::poseToModel(m_rigData, pose);
            glm::mat3 m(world[static_cast<size_t>(b)]);
            for (int i = 0; i < 3; ++i) m[i] = glm::normalize(m[i]);
            const glm::quat boneWorld = glm::quat_cast(m);
            const glm::quat delta = glm::angleAxis(glm::radians(degrees * stance), axis);
            pose[static_cast<size_t>(b)].r = glm::normalize(pose[static_cast<size_t>(b)].r * (glm::inverse(boneWorld) * delta * boneWorld));
        };
        // Hips down (the feet stay on the canvas below: the knees bend).
        if (const int pelvis = m_rigData.findBone("pelvis"); pelvis >= 0) {
            const std::vector<glm::mat4> world = kke::poseToModel(m_rigData, pose);
            const int parent = m_rigData.bones[static_cast<size_t>(pelvis)].parent;
            const glm::mat4 parentWorld = parent >= 0 ? world[static_cast<size_t>(parent)] : glm::mat4(1.0f);
            const glm::vec3 drop = glm::vec3(glm::inverse(parentWorld) * glm::vec4(-up * (0.08f * stance) - fwdM * (0.02f * stance), 0.0f));
            pose[static_cast<size_t>(pelvis)].t += drop;
        }
        turn("spine_01", up, -14.0f);     // bladed: the left (lead) shoulder forward
        turn("spine_02", -rightM, 7.0f);  // leaning in
        turn("spine_03", -rightM, 5.0f);
        turn("neck_01", -rightM, 6.0f);   // chin down
    }
    if (f.ik) {
        // Hands to the face, the lead (left) one a little out in front;
        // blocking, both come up and in to cover it.
        if (f.guard > 0.01f) {
            const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
            const int head = m_rigData.findBone("Head");
            const glm::vec3 chin = head >= 0 ? glm::vec3(bones[static_cast<size_t>(head)][3]) : glm::vec3(0.0f, 1.55f, 0.0f);
            const float high = c.blocking() ? 1.0f : 0.0f;
            for (int s = 0; s < 2; ++s) {
                const float side = s == 0 ? -1.0f : 1.0f;
                const float lead = s == 0 ? 1.0f : 0.0f;
                const glm::vec3 handM = chin + fwdM * (0.22f + 0.1f * lead + 0.03f * high) + rightM * (side * (0.11f - 0.04f * high)) +
                                        up * (-0.05f + 0.08f * high - 0.03f * lead * (1.0f - high));
                const glm::vec3 shoulder = handM - fwdM * 0.3f + rightM * (0.18f * side) - up * 0.15f;
                const glm::vec3 elbowM = shoulder - up * 0.45f + rightM * (0.12f * side) + fwdM * 0.05f; // elbows down, tucked
                // Blend from where the clip has the hand (a fading guard never pops).
                const int handBone = m_rigData.findBone(s == 0 ? "hand_l" : "hand_r");
                const glm::vec3 clipHand = handBone >= 0 ? glm::vec3(bones[static_cast<size_t>(handBone)][3]) : handM;
                const glm::vec3 goal = glm::mix(clipHand, handM, std::min(1.0f, f.guard / 0.7f));
                f.ik->hand(s == 0 ? kke::CharacterIk::Left : kke::CharacterIk::Right, glm::vec3(xf * glm::vec4(goal, 1.0f)),
                           glm::vec3(xf * glm::vec4(elbowM, 1.0f)));
            }
        }
        // The canvas is flat at y = 0 inside the ropes.
        const auto ground = [](const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) {
            if (from.y < 0.0f) return false;
            hit = glm::vec3(from.x, 0.0f, from.z);
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
            return true;
        };
        f.ik->feetOnGround(c.state() != S::Dodging || f.dodgeSide == 0.0f);
        f.ik->apply(m_rigData, pose, xf, ground, velocity, dt);
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
