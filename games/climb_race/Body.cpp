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

glm::vec3 positionOf(const glm::mat4& m) { return glm::vec3(m[3]); }
// Rotation of a bone's model transform (which may carry uniform scale).
glm::quat rotationOf(const glm::mat4& m) {
    glm::mat3 r(m);
    for (int i = 0; i < 3; ++i) r[i] = glm::normalize(r[i]);
    return glm::normalize(glm::quat_cast(r));
}
// The rotation taking the frame (a, b) onto (a2, b2); b is made square to a.
glm::quat frameRotation(glm::vec3 a, glm::vec3 b, glm::vec3 a2, glm::vec3 b2) {
    auto frame = [](glm::vec3 x, glm::vec3 y) {
        x = glm::normalize(x);
        y = glm::normalize(y - x * glm::dot(x, y));
        return glm::mat3(x, y, glm::cross(x, y));
    };
    return glm::normalize(glm::quat_cast(frame(a2, b2) * glm::transpose(frame(a, b))));
}
// Moves bone `b` (and everything under it) by `delta`, model space.
void shiftBone(const kke::ModelData& rig, kke::Pose& pose, const std::vector<glm::mat4>& bones, int b, const glm::vec3& delta) {
    const int parent = rig.bones[static_cast<size_t>(b)].parent;
    pose[static_cast<size_t>(b)].t += parent >= 0 ? glm::inverse(glm::mat3(bones[static_cast<size_t>(parent)])) * delta : delta;
}
int boneIndex(const kke::ModelData& rig, const std::string& name) {
    for (size_t b = 0; b < rig.bones.size(); ++b)
        if (kke::canonicalBoneName(rig.bones[b].name) == name || rig.bones[b].name == name) return static_cast<int>(b);
    return -1;
}
} // namespace

std::unique_ptr<kke::Climber> ClimbRaceModule::makeClimber(int lane) const {
    return std::make_unique<kke::Climber>(*m_lanes[static_cast<size_t>(lane)]->wall, m_climbSettings);
}

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
    for (int s = 0; s < 2; ++s) m_human[s] = kke::makeHumanArm(m_rigData, m_arm[s], m_arm[1 - s]);
    m_pelvis = -1;
    for (size_t b = 0; b < m_rigData.bones.size(); ++b)
        if (kke::canonicalBoneName(m_rigData.bones[b].name) == "pelvis") m_pelvis = static_cast<int>(b);
    m_feet = kke::FootPlacer(m_rigData, m_leg[0], m_leg[1], m_pelvis);
    const glm::vec3 fwd = kke::modelForward(m_rigData);
    m_modelYaw = 180.0f - glm::degrees(std::atan2(fwd.x, fwd.z));

    // The body's proportions, from the rest pose: the climber's reach is
    // what these arms reach, so the IK can always put the hands on the holds.
    const std::vector<glm::mat4> rest = kke::poseToModel(m_rigData, m_animSet->restPose());
    auto at = [&](int b) { return positionOf(rest[static_cast<size_t>(b)]); };
    const char* fingerNames[4] = { "index", "middle", "ring", "pinky" };
    for (int s = 0; s < 2; ++s) {
        const std::string side = s == 0 ? "_l" : "_r";
        HandRig& hr = m_handRig[s];
        hr = HandRig{};
        for (int f = 0; f < 4; ++f)
            for (int k = 0; k < 3; ++k) hr.segment[f][k] = boneIndex(m_rigData, std::string(fingerNames[f]) + "_0" + std::to_string(k + 1) + side);
        for (int k = 0; k < 3; ++k) hr.thumb[k] = boneIndex(m_rigData, "thumb_0" + std::to_string(k + 1) + side);
        if (!m_arm[s].valid()) continue;
        const glm::vec3 wrist = at(m_arm[s].end);
        hr.restModel = rotationOf(rest[static_cast<size_t>(m_arm[s].end)]);
        if (hr.segment[1][0] >= 0 && hr.segment[0][0] >= 0 && hr.segment[3][0] >= 0) {
            hr.fingers = glm::normalize(at(hr.segment[1][0]) - wrist);
            hr.thumbSide = glm::normalize(at(hr.segment[0][0]) - at(hr.segment[3][0]));
            hr.knuckles = glm::length(at(hr.segment[1][0]) - wrist);
        }
    }
    kke::Climber::Settings& cs = m_climbSettings;
    if (m_arm[0].valid() && m_leg[0].valid() && m_pelvis >= 0) {
        const glm::vec3 pelvis = at(m_pelvis), shoulder = at(m_arm[0].upper), hip = at(m_leg[0].upper);
        const float arm = glm::length(at(m_arm[0].lower) - shoulder) + glm::length(at(m_arm[0].end) - at(m_arm[0].lower));
        const float leg = glm::length(at(m_leg[0].lower) - hip) + glm::length(at(m_leg[0].end) - at(m_leg[0].lower));
        // A straight arm reaches the knuckles over a hold; keep a little
        // bend in it so the elbow never locks.
        cs.armReach = arm * 0.98f;
        cs.handLength = m_handRig[0].knuckles;
        cs.shoulderUp = shoulder.y - pelvis.y;
        cs.shoulderHalf = std::abs(shoulder.x - pelvis.x);
        cs.hipHalf = std::abs(hip.x - pelvis.x);
        cs.legReach = leg * 0.95f;
        kke::log::get(name())->info("climber proportions from the skeleton: arm reach {:.2f} m, shoulders {:.2f} m up and {:.2f} m out, leg {:.2f} m",
                                    cs.armReach, cs.shoulderUp, cs.shoulderHalf, cs.legReach);
    }
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
    // On the rock: the idle's upright torso (its breathing slowed); IK puts
    // the limbs on the holds.
    m_stHang = a.addClipState("hang", pick({ "Hang_Idle", "|Idle_Loop" }), true, 0.5f);
    // Over the top: a crouch while the legs come up under the body.
    m_stTop = a.addClipState("top", pick({ "Crouch_Idle_Loop", "Idle_Loop" }), true);
    a.play(m_stMove, 0.0f);
}

ClimbRaceModule::BodyInput ClimbRaceModule::bodyInput(const Racer& r) const {
    BodyInput b;
    const kke::RigidWorld& w = m_rigid->world();
    // Drawn between the last two physics steps (smooth on a screen faster
    // than the physics).
    b.feet = w.characterDrawPosition(r.id, m_app->fixedAlpha());
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
            b.grip[s] = p.grip[s];
            b.normal[s] = p.normal[s];
            b.closed[s] = p.closed[s];
            b.onRock[s] = p.onRock[s];
            b.held[s] = p.held[s];
            b.foot[s] = p.foot[s];
        }
        b.hips = p.hips;
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
    const auto& holds = c.wall().holds();
    // Where each hand closes (the knuckles), the rock's normal there, and
    // how closed the fingers are.
    for (int s = 0; s < 2; ++s) {
        b.normal[s] = -b.in;
        if (b.climbing) {
            const int hold = c.handHold(s);
            if (hold >= 0) {
                const kke::ClimbHold& h = holds[static_cast<size_t>(hold)];
                b.grip[s] = toWorld(r, h.position);
                b.normal[s] = h.normal;
                b.onRock[s] = b.held[s] = true;
                b.closed[s] = 1.0f;
            } else if (c.handMoving(s)) {
                const int t = c.handTarget(s);
                b.grip[s] = toWorld(r, c.hand(s));
                b.normal[s] = t >= 0 ? holds[static_cast<size_t>(t)].normal : -b.in;
                b.onRock[s] = true;
                b.closed[s] = std::clamp((c.handProgress(s) - 0.8f) / 0.2f, 0.0f, 1.0f); // open, closing as it arrives
            } else {
                b.grip[s] = toWorld(r, c.hand(s)); // hanging free by the body
                b.closed[s] = 0.3f;
            }
        } else {
            // Locomotion's own ledge hang: shoulder-width on the edge.
            const float side = s == 0 ? -1.0f : 1.0f;
            b.grip[s] = r.loco->hangEdge() + right * (0.22f * side) + b.in * 0.04f;
            b.onRock[s] = true;
            b.closed[s] = 1.0f;
        }
        b.foot[s] = toWorld(r, c.foot(s));
    }
    b.hips = toWorld(r, c.hips());
    return b;
}

void ClimbRaceModule::animateBody(Racer& r, float dt) {
    if (!r.model || !r.anim) return;
    kke::RigidWorld& w = m_rigid->world();
    kke::Animator& a = *r.anim;
    const kke::Climber& c = *r.climber; // for its hand geometry only: the state is in b
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
    glm::vec3 fingerDir[2], thumbDir[2], wrist[2];
    if (r.armWeight > 0.01f || r.legWeight > 0.01f) {
        const glm::vec3 in = b.in; // into the rock
        const glm::vec3 up(0.0f, 1.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(in, up)); // the climber's right
        const glm::vec3* grip = b.grip;
        const glm::vec3* normal = b.normal;
        for (int s = 0; s < 2; ++s) {
            r.grip[s] += (b.closed[s] - r.grip[s]) * k;
            r.handAim[s] += ((b.onRock[s] ? 1.0f : 0.0f) - r.handAim[s]) * k;
        }
        // The fingers point up and hook over the hold, palm to the rock, the
        // thumb toward the body's middle; the wrist sits below and out from
        // the knuckles by the hand's length.
        for (int s = 0; s < 2; ++s) {
            fingerDir[s] = c.fingerDirection(normal[s]);
            thumbDir[s] = s == 0 ? right : -right;
            wrist[s] = b.onRock[s] ? c.wristAt(grip[s], normal[s]) : grip[s];
        }

        // The pelvis where the climber's hips are (the logic already keeps
        // every hold within the arms' reach of them).
        if (climbing && m_pelvis >= 0) {
            const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
            const glm::vec3 want = model(b.hips);
            shiftBone(m_rigData, pose, bones, m_pelvis, (want - positionOf(bones[static_cast<size_t>(m_pelvis)])) * r.armWeight);
        }
        // Arms: two passes. If the animated torso leaves a hand short of its
        // hold, the whole body moves the rest of the way and they solve again.
        // Each arm keeps to a person's ranges (kke::solveHumanArm): the elbow
        // bends only forward, the shoulder doesn't reach through the back,
        // and the hand turns onto its hold as far as the forearm and wrist go.
        const glm::mat3 toModel = glm::mat3(inv);
        for (int pass = 0; pass < 2; ++pass) {
            const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
            for (int s = 0; s < 2; ++s) {
                if (!m_arm[s].valid() || r.armWeight <= 0.01f) continue;
                const float side = s == 0 ? -1.0f : 1.0f;
                const glm::vec3 shoulder = glm::vec3(xf * bones[static_cast<size_t>(m_arm[s].upper)][3]);
                // Elbows down and out, away from the rock. A hand reaching
                // over to the other side (Climber lets it go a little past
                // the middle) passes in front of the chest: the elbow comes
                // forward instead of out, so the arm never folds behind.
                const glm::vec3 chest = m_arm[1 - s].valid() ? (glm::vec3(xf * bones[static_cast<size_t>(m_arm[1 - s].upper)][3]) + shoulder) * 0.5f
                                                             : shoulder - right * (m_climbSettings.shoulderHalf * side);
                const float across = std::clamp(-glm::dot(wrist[s] - chest, right) * side / 0.3f, 0.0f, 1.0f);
                const glm::vec3 pole = shoulder - up * 0.5f + right * (0.45f * side * (1.0f - across)) - in * (0.25f + 0.35f * across);
                kke::ArmGoal goal;
                goal.hand = model(wrist[s]);
                goal.elbowToward = model(pole);
                goal.weight = r.armWeight;
                if (r.handAim[s] > 0.01f) {
                    const HandRig& hr = m_handRig[s];
                    const glm::quat want = frameRotation(hr.fingers, hr.thumbSide, toModel * fingerDir[s], toModel * thumbDir[s]) * hr.restModel;
                    goal.handRotation = glm::slerp(rotationOf(bones[static_cast<size_t>(m_arm[s].end)]), want, r.handAim[s]);
                }
                kke::solveHumanArm(m_rigData, pose, m_human[s], goal);
            }
            if (pass == 1 || !climbing || m_pelvis < 0) break;
            const std::vector<glm::mat4> solved = kke::poseToModel(m_rigData, pose);
            glm::vec3 shortBy(0.0f);
            int n = 0;
            for (int s = 0; s < 2; ++s) {
                if (!m_arm[s].valid() || !b.held[s]) continue;
                const glm::vec3 miss = model(wrist[s]) - positionOf(solved[static_cast<size_t>(m_arm[s].end)]);
                if (glm::length(miss) > 0.005f) {
                    shortBy += miss;
                    ++n;
                }
            }
            if (n == 0) break;
            shiftBone(m_rigData, pose, solved, m_pelvis, shortBy / static_cast<float>(n) * r.armWeight);
        }
        // Legs, after the body has settled.
        if (r.legWeight > 0.01f) {
            const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
            for (int s = 0; s < 2; ++s) {
                if (!m_leg[s].valid()) continue;
                const float side = s == 0 ? -1.0f : 1.0f;
                // The ankle sits a little out from the foothold and above it.
                const glm::vec3 foot = b.foot[s] - in * 0.1f + up * 0.07f;
                const glm::vec3 hip = glm::vec3(xf * bones[static_cast<size_t>(m_leg[s].upper)][3]);
                // Knees toward the rock and a little out, like a frog.
                const glm::vec3 pole = hip + in * 0.6f + right * (0.35f * side) - up * 0.2f;
                kke::solveTwoBone(m_rigData, pose, m_leg[s], model(foot), model(pole), r.legWeight);
            }
        }
        // Hands: turned onto the hold with the arms (above), fingers closed around it.
        const std::vector<glm::mat4> bones = kke::poseToModel(m_rigData, pose);
        for (int s = 0; s < 2; ++s) {
            const HandRig& hr = m_handRig[s];
            const int hand = m_arm[s].end;
            if (!m_arm[s].valid()) continue;
            const glm::quat handModel = rotationOf(bones[static_cast<size_t>(hand)]);
            const float curl = r.grip[s] * r.armWeight;
            if (curl < 0.01f) continue;
            // Curl toward the palm: about the axis across the knuckles.
            const glm::vec3 palm = glm::normalize(-(toModel * normal[s]));
            const glm::vec3 f = handModel * glm::inverse(hr.restModel) * hr.fingers;
            glm::vec3 axis = glm::cross(f, palm);
            if (glm::length(axis) < 1e-4f) continue;
            axis = glm::normalize(axis);
            const float bend[3] = { 0.9f, 1.1f, 0.7f }; // radians per joint, fully closed
            auto curlChain = [&](const int* seg, float amount) {
                glm::quat parentModel = handModel;
                for (int j = 0; j < 3; ++j) {
                    if (seg[j] < 0) break;
                    const glm::vec3 localAxis = glm::inverse(parentModel) * axis;
                    kke::BoneTRS& bone = pose[static_cast<size_t>(seg[j])];
                    bone.r = glm::normalize(glm::angleAxis(bend[j] * amount, localAxis) * bone.r);
                    parentModel = parentModel * bone.r;
                }
            };
            for (const auto& seg : hr.segment) curlChain(seg, curl);
            curlChain(hr.thumb, curl * 0.4f);
        }
    }
    // How well the hands sit on their holds: the knuckles against the
    // point the climber holds (logged by the KKE_CLIMB_QUIT report).
    if (climbing && !r.remote && r.armWeight > 0.98f) {
        const std::vector<glm::mat4> final = kke::poseToModel(m_rigData, pose);
        for (int s = 0; s < 2; ++s) {
            const int hold = c.handHold(s), knuckle = m_handRig[s].segment[1][0];
            if (hold < 0 || knuckle < 0 || r.grip[s] < 0.95f || r.handAim[s] < 0.95f) continue;
            const glm::vec3 at = glm::vec3(xf * final[static_cast<size_t>(knuckle)][3]);
            const glm::vec3 want = toWorld(r, c.wall().holds()[static_cast<size_t>(hold)].position);
            const float d = glm::length(at - want);
            r.gripError.worst = std::max(r.gripError.worst, d);
            r.gripError.sum += d;
            ++r.gripError.samples;
        }
    }
    kke::poseToLocals(pose, *locals);
}

} // namespace climb_race
