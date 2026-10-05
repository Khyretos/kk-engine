// Picking things up in kke_demo (Kees, 2026-09-28: "id like to be able to
// pick up the boxes"). X on a controller or F on the keyboard lifts the
// crate, barrel or ball in front of you; it rides in front of the chest,
// held at its sides by both hands (kke::CharacterIk hand contacts on top
// of the walk clips), and stays a physics body the whole time: it bumps
// into walls and other crates instead of going through them, and if it
// gets stuck you let go of it. X or F again puts it down, RT or a click
// throws it where you look. What weighs more than kMaxLift you push (Y, E).
//
// Online the host's bodies are the real ones: a client carries its own
// copy and tells the host, which carries the real one for that player
// (Online.cpp), so everyone sees it in that player's hands.

#include "ShowcaseModule.h"

#include "kke/Application.h"
#include "kke/modules/RigidBodyModule.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace kke_showcase {

namespace {
constexpr float kMaxLift = 60.0f;     // kg: a full wooden crate, not an iron one
constexpr float kReach = 1.8f;        // m from the chest to the thing's middle
constexpr float kFollow = 14.0f;      // 1/s: how fast the held body closes the gap to the hands
constexpr float kMaxFollow = 9.0f;    // m/s
constexpr float kLetGoDistance = 1.0f; // m off the hands (snagged on something): it drops
constexpr float kOnlineLetGoDistance = 4.0f; // m: a guest's copy lags the host's hands (3.3 m seen while lifting)
constexpr float kLiftTime = 0.6f;      // s: while lifting from where it lay, it may still be far off
} // namespace

glm::vec3 ShowcaseModule::holdPoint() const {
    const kke::RigidWorld& w = m_rigid->world();
    return holdPointFor(w.characterPosition(m_player), m_loco->facing(), w.characterRadius(m_player), m_crouch, m_held.half);
}

glm::vec3 ShowcaseModule::holdPointFor(const glm::vec3& feet, const glm::vec3& facing, float radius, bool crouch, const glm::vec3& half) {
    glm::vec3 f(facing.x, 0.0f, facing.z);
    f = glm::length(f) > 1e-3f ? glm::normalize(f) : glm::vec3(0, 0, -1);
    // Clear of the capsule, its near face a hand's width in front of the
    // chest; bigger things ride a little lower (held against the belly).
    const float depth = std::max(half.x, half.z);
    const float height = (crouch ? 0.65f : 1.05f) - std::min(0.25f, half.y * 0.4f);
    return feet + glm::vec3(0, height, 0) + f * (radius + 0.12f + depth);
}

void ShowcaseModule::togglePickUp() {
    if (m_held.body != kke::RigidWorld::kNoBody) {
        dropHeld();
        return;
    }
    // Only on your feet: not hanging, vaulting or in the air.
    if (m_loco->state() != kke::Locomotion::State::Ground) return;
    // An item in front of you (Items.cpp) goes in the bag; crates are lifted.
    if (const int item = itemInReach(); item >= 0) {
        takeItem(item);
        return;
    }
    kke::RigidWorld& w = m_rigid->world();
    const glm::vec3 chest = w.characterPosition(m_player) + glm::vec3(0, 0.9f, 0);
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::normalize(f);
    // The nearest body in front (a cone, not the camera's aim: on a
    // controller you face a crate more easily than you point at it).
    std::vector<kke::RigidWorld::BodyBox> near;
    w.bodiesInBox(chest - glm::vec3(kReach, 1.1f, kReach), chest + glm::vec3(kReach, 1.0f, kReach), near);
    const kke::RigidWorld::BodyBox* best = nullptr;
    float bestScore = 1e9f;
    bool tooHeavy = false;
    for (const kke::RigidWorld::BodyBox& b : near) {
        if (b.motion != kke::RigidWorld::Motion::Dynamic) continue;
        glm::vec3 to = b.center - chest;
        to.y = 0.0f;
        const float d = glm::length(to);
        if (d > kReach || (d > 0.2f && glm::dot(to / d, f) < 0.45f)) continue;
        if (b.mass > kMaxLift) {
            tooHeavy = true;
            continue;
        }
        if (glm::length(b.halfExtents) > 1.1f) continue; // a big box: push it
        const float score = d - glm::dot(to / std::max(d, 1e-3f), f) * 0.5f;
        if (score < bestScore) {
            bestScore = score;
            best = &b;
        }
    }
    if (!best) {
        if (tooHeavy) m_status = "Too heavy to lift: push it";
        return;
    }
    m_held.body = best->id;
    std::erase_if(m_remoteCarries, [&](const RemoteCarry& c) { return c.body == best->id; }); // hosting: ours now
    m_held.mass = best->mass;
    m_held.half = best->halfExtents;
    // Keep it square to you on whichever face is nearest, so lifting
    // doesn't spin it round.
    const glm::vec3 bodyF = best->rotation * glm::vec3(0, 0, -1);
    const float rel = std::atan2(bodyF.x, -bodyF.z) - std::atan2(f.x, -f.z);
    m_held.yawOffset = std::round(rel / glm::half_pi<float>()) * glm::half_pi<float>();
    tellCarry(true, glm::vec3(0.0f)); // online: the host carries the real one for us
}

void ShowcaseModule::dropHeld() {
    if (m_held.body == kke::RigidWorld::kNoBody) return;
    // Put down, not flung: it keeps the walk's speed and no spin.
    kke::RigidWorld& w = m_rigid->world();
    w.setAngularVelocity(m_held.body, glm::vec3(0.0f));
    w.setVelocity(m_held.body, w.characterVelocity(m_player));
    tellCarry(false, w.characterVelocity(m_player));
    m_held = Held{};
}

void ShowcaseModule::throwHeld(const kke::Camera& cam) {
    if (m_held.body == kke::RigidWorld::kNoBody) return;
    kke::RigidWorld& w = m_rigid->world();
    glm::vec3 dir = glm::normalize(cam.target - cam.position);
    // Where you look, unless that's behind you (the camera in front of
    // the face): then straight ahead.
    const glm::vec3 f = m_loco->facing();
    if (dir.x * f.x + dir.z * f.z < 0.0f) dir = glm::vec3(f.x, dir.y, f.z);
    dir.y = std::max(dir.y, -0.2f) + 0.25f; // a throw goes up a little
    dir = glm::normalize(dir);
    // A light thing flies far, a full crate about a body length or three.
    const float speed = std::clamp(9.0f * std::sqrt(15.0f / std::max(m_held.mass, 1.0f)), 4.0f, 14.0f);
    const kke::RigidWorld::BodyId body = m_held.body;
    const glm::vec3 velocity = w.characterVelocity(m_player) + dir * speed, spin = glm::vec3(-dir.z, 0.0f, dir.x) * 2.0f;
    tellThrow(velocity, spin);
    m_held = Held{};
    w.setVelocity(body, velocity);
    w.setAngularVelocity(body, spin);
}

// At the physics rate: steer the body to the hands with its velocity (it
// stays a dynamic body, so walls stop it), square to the facing.
void ShowcaseModule::carryStep(float dt) {
    if (m_held.body == kke::RigidWorld::kNoBody) return;
    kke::RigidWorld& w = m_rigid->world();
    const kke::Locomotion::State st = m_loco->state();
    // Climbing, vaulting or hanging needs the hands: it drops.
    if (st != kke::Locomotion::State::Ground && st != kke::Locomotion::State::Air) {
        dropHeld();
        return;
    }
    const glm::vec3 at = w.position(m_held.body);
    const glm::vec3 target = holdPoint();
    const glm::vec3 gap = target - at;
    m_held.age += dt;
    // Online the host carries the real body and our copy shows it a little
    // late, so a guest only lets go past the host's own limit (2.5 m): the
    // host decides when it snags, and our copy then falls away from us.
    // That goes for the lift too: the copy is furthest behind while the
    // host's hands first take it (soucouyant: dropped 3 runs in 5 at 0.2 s
    // into the lift, 2.4 to 3.3 m off).
    const float letGo = onlineClient() ? kOnlineLetGoDistance : m_held.age < kLiftTime ? kReach + 0.5f : kLetGoDistance;
    if (glm::length(gap) > letGo) {
        dropHeld();
        return;
    }
    steerHeld(m_held.body, target, w.characterVelocity(m_player), m_loco->facing(), m_held.yawOffset, dt);
}

void ShowcaseModule::steerHeld(kke::RigidWorld::BodyId body, const glm::vec3& target, const glm::vec3& carrierVelocity, const glm::vec3& f,
                               float yawOffset, float dt) {
    kke::RigidWorld& w = m_rigid->world();
    glm::vec3 v = (target - w.position(body)) * kFollow;
    if (glm::length(v) > kMaxFollow) v = glm::normalize(v) * kMaxFollow;
    // The step adds gravity after this; ask for that much more upward.
    w.setVelocity(body, carrierVelocity * glm::vec3(1, 0, 1) + v + glm::vec3(0, 9.81f * dt, 0));
    // Turn toward upright and the facing (yaw) by the shortest way.
    const float yaw = std::atan2(f.x, -f.z) + yawOffset;
    const glm::quat want = glm::angleAxis(-yaw, glm::vec3(0, 1, 0));
    glm::quat q = w.rotation(body);
    glm::quat diff = want * glm::inverse(q);
    if (diff.w < 0.0f) diff = -diff;
    const float angle = 2.0f * std::acos(std::clamp(diff.w, -1.0f, 1.0f));
    glm::vec3 axis(diff.x, diff.y, diff.z);
    const float s = glm::length(axis);
    w.setAngularVelocity(body, s > 1e-5f ? axis / s * std::min(angle * 10.0f, 12.0f) : glm::vec3(0.0f));
}

// The hands on the held body's sides, a little below the middle, the
// elbows out and down: CharacterIk solves the arms on top of the walk.
void ShowcaseModule::holdHands() {
    if (m_held.body == kke::RigidWorld::kNoBody) return;
    const kke::RigidWorld& w = m_rigid->world();
    const glm::mat4 t = w.transform(m_held.body);
    const glm::vec3 c = glm::vec3(t[3]);
    glm::vec3 f = m_loco->facing();
    f.y = 0.0f;
    f = glm::normalize(f);
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
    // How far the body reaches sideways, along its own axes.
    const glm::vec3 ax = glm::vec3(t[0]), az = glm::vec3(t[2]);
    const float side = std::abs(glm::dot(ax, right)) * m_held.half.x + std::abs(glm::dot(az, right)) * m_held.half.z;
    for (int i = 0; i < 2; ++i) {
        const float s = i == 0 ? -1.0f : 1.0f; // left hand on the left side
        const glm::vec3 palm = c + right * (s * (side + 0.03f)) - glm::vec3(0, m_held.half.y * 0.2f, 0);
        m_ik.hand(static_cast<kke::CharacterIk::Side>(i), palm, palm + right * (s * 0.3f) - f * 0.25f - glm::vec3(0, 0.35f, 0));
    }
}

} // namespace kke_showcase
