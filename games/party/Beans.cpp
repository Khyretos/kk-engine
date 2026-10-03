// The beans: controls, the controller that moves them (run, jump, dive,
// get knocked), bumping into each other, how they wobble when drawn, the
// cameras, and the particles (confetti, glass bits, flames).

#include "PartyModule.h"

#include "kke/Application.h"
#include "kke/ImpactSynth.h"
#include "kke/Log.h"
#include "kke/SphereImpostors.h"
#include "kke/Viewports.h"
#include "kke/modules/InputModule.h"
#include "kke/modules/LobbyModule.h"
#include "kke/modules/RigidBodyModule.h"
#include "kke/Renderer.h"

#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace party {

namespace {

constexpr float kRunSpeed = 5.2f;
constexpr float kJumpSpeed = 6.3f;
constexpr float kDiveTime = 0.7f, kDiveSpeed = 8.0f;
constexpr float kCoyote = 0.12f;
constexpr float kChargeMax = 1.0f;    // s of holding dive for a full-power dive (charged-dive games)
constexpr float kClashTime = 2.0f;    // s two clashing beans mash for
constexpr float kJumpBuffer = 0.15f;  // s a press waits for the ground (pressed just before landing)
constexpr float kDiveCooldown = 1.1f; // s after a dive before the next
constexpr float kPushCooldown = 0.7f, kPushReach = 1.35f;
// Stamina: what each move costs, how fast it comes back.
constexpr float kJumpCost = 0.12f, kDiveCost = 0.28f, kPushCost = 0.15f;
constexpr float kRegenGround = 0.32f, kRegenAir = 0.05f;
// Jumps in a row (a hop the moment you land): each goes less high, so
// bunny hopping is slower than running.
constexpr float kRepeatWindow = 0.15f, kRepeatScale = 0.85f, kRepeatMin = 0.6f;
constexpr float kLookMouse = 0.12f, kLookStick = 180.0f;

float yawOf(const glm::vec3& d) { return glm::degrees(std::atan2(d.x, -d.z)); }
glm::vec3 facingOf(float yaw) { return glm::vec3(std::sin(glm::radians(yaw)), 0.0f, -std::cos(glm::radians(yaw))); }
float wrap180(float a) {
    while (a > 180.0f) a -= 360.0f;
    while (a < -180.0f) a += 360.0f;
    return a;
}

} // namespace

void PartyModule::defineControls() {
    for (int p = 0; p < kke::Lobby::kMaxSeats; ++p) {
        m_input->setPlayers(p + 1);
        kke::InputMap& in = m_input->map(p);
        kke::InputModule::defineCharacterActions(in);
        // Beans always run; the pad's other buttons are the party's.
        for (const char* a : { "fire", "aim", "interact", "crouch", "camera.toggle", "sprint", "walk" }) in.clearBindings(a);
        using IM = kke::InputModule;
        in.defineAction({ "dive", "Dive (and shove whoever you land on)", "Party", "game" });
        in.defineAction({ "menu", "Back to the menu (players, rounds, games)", "Party", "game" });
        in.defineAction({ "panels", "Developer panels", "Game", "game" });
        in.defineAction({ "push", "Push whoever is in front of you", "Party", "game" });
        in.addBinding(IM::bind("push", IM::pad(SDL_GAMEPAD_BUTTON_EAST)));
        in.addBinding(IM::bind("push", IM::key(SDL_SCANCODE_F)));
        in.addBinding(IM::bind("dive", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        in.addBinding(IM::bind("dive", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
        in.addBinding(IM::bind("dive", IM::key(SDL_SCANCODE_E)));
        in.addBinding(IM::bind("dive", IM::mouse(SDL_BUTTON_RIGHT)));
        // A controller's Back opens the pause menu (Pause.cpp), which has "Back to the start menu".
        in.addBinding(IM::bind("menu", IM::key(SDL_SCANCODE_M)));
        in.addBinding(IM::bind("panels", IM::key(SDL_SCANCODE_F1)));
    }
    m_input->setPlayers(1);
    m_input->commitDefaults();
}

kke::Camera& PartyModule::cameraOf(Bean& b) { return b.seat >= 0 && b.player == 0 ? m_app->camera() : b.camera; }

BeanInput PartyModule::readPlayer(Bean& b, float dt) {
    BeanInput bi;
    kke::InputMap& in = m_input->map(b.player);
    const bool mouse = !m_lobby || m_lobby->lobby().seat(std::max(0, b.seat)).device != kke::Lobby::Device::Pad;
    glm::vec2 look(0.0f);
    if (mouse && m_captured) look += in.axis2("look") * kLookMouse;
    const glm::vec2 rate = in.axis2("look.rate");
    look += glm::vec2(rate.x * kLookStick * dt, rate.y * kLookStick * 0.7f * dt);
    b.rig.addLook(look.x, look.y);
    b.idleLook = glm::length(look) > 0.05f ? 0.0f : b.idleLook + dt;
    const glm::vec2 move = in.axis2("move");
    // Relative to what the player sees: their camera, or the shared one.
    glm::vec3 fwd = b.rig.forward(), right = b.rig.right();
    fwd.y = right.y = 0.0f;
    glm::vec3 wish = (glm::length(fwd) > 1e-3f ? glm::normalize(fwd) : glm::vec3(0, 0, -1)) * move.y +
                     (glm::length(right) > 1e-3f ? glm::normalize(right) : glm::vec3(1, 0, 0)) * move.x;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    bi.move = glm::vec2(wish.x, wish.z);
    bi.jump = in.pressed("jump");
    bi.jumpHeld = in.held("jump");
    bi.dive = in.pressed("dive");
    bi.diveHeld = in.held("dive");
    bi.push = in.pressed("push");
    return bi;
}

void PartyModule::park(Bean& b) {
    kke::RigidWorld& w = world();
    b.hidden = true;
    b.velocity = b.push = glm::vec3(0.0f);
    w.teleportCharacter(b.id, glm::vec3(static_cast<float>(b.index) * 3.0f, -200.0f, -300.0f));
    w.setCharacterKinematic(b.id, true);
}

void PartyModule::moveBean(Bean& b, float dt) {
    kke::RigidWorld& w = world();
    if (b.hidden) return;
    const bool was = b.grounded;
    b.grounded = w.characterOnGround(b.id);
    b.airTime = b.grounded ? 0.0f : b.airTime + dt;
    b.groundTime = b.grounded ? (was ? b.groundTime + dt : 0.0f) : 0.0f;
    if (b.grounded && b.groundTime > kRepeatWindow) b.repeatJumps = 0;
    if (b.grounded && !was) b.jumped = false;
    b.bumped = std::max(0.0f, b.bumped - dt);
    b.diveCooldown = std::max(0.0f, b.diveCooldown - dt);
    b.pushCooldown = std::max(0.0f, b.pushCooldown - dt);
    b.pushPose = std::max(0.0f, b.pushPose - dt);
    b.stamina = std::min(1.0f, b.stamina + (b.grounded && b.dive <= 0.0f ? kRegenGround : kRegenAir) * dt);
    BeanInput in = b.stun > 0.0f ? BeanInput{} : b.input;
    if (!b.active && m_phase == Phase::Play) in = BeanInput{}; // finished: wait at the goal
    glm::vec3 wish = glm::vec3(in.move.x, 0.0f, in.move.y) * kRunSpeed;
    kke::RigidWorld::CharacterInput ci;
    ci.airSteer = 1000.0f; // we send the exact horizontal velocity
    // Dive: a burst forward, then a belly slide that slows down. Not
    // again straight away, and not on an empty tank.
    const bool canDive = b.dive <= 0.0f && b.stun <= 0.0f && b.diveCooldown <= 0.0f && b.stamina >= kDiveCost * 0.5f;
    bool diveNow = in.dive;
    if (m_game && m_game->chargedDive()) {
        // Held: winding up (slow on your feet, squashing down); let go: dive.
        diveNow = b.charge > 0.0f && !in.diveHeld;
        if (in.diveHeld && canDive && b.grounded) {
            b.charge = std::min(kChargeMax, b.charge + dt);
            wish *= 0.35f;
            b.squash = std::max(b.squash, 0.2f * b.charge / kChargeMax);
        }
    }
    if (diveNow && canDive) {
        b.divePower = 1.0f + std::min(b.charge, kChargeMax) / kChargeMax;
        b.dive = kDiveTime;
        b.diveCooldown = kDiveTime + kDiveCooldown;
        b.stamina = std::max(0.0f, b.stamina - kDiveCost * b.divePower);
        b.velocity = facingOf(b.yaw) * kDiveSpeed * (0.75f + 0.25f * b.divePower);
        if (b.grounded) b.launch = std::max(b.launch, 3.2f);
        else if (m_game) b.launch = std::max(b.launch, m_game->airDiveLift(*this, b));
        sound(b.feet(w), kke::AudioMaterialTable::Rubber, 0.25f * b.divePower);
    }
    if (diveNow || !in.diveHeld) b.charge = 0.0f;
    if (b.dive > 0.0f) {
        b.dive -= dt;
        wish = facingOf(b.yaw) * (b.grounded ? kDiveSpeed * std::max(0.0f, b.dive / kDiveTime) : kDiveSpeed * 0.9f);
        // Landed from a dive: back up on your feet a moment later.
        if (b.grounded && b.dive < kDiveTime - 0.25f) b.dive = std::min(b.dive, 0.2f);
    }
    if (in.push && b.pushCooldown <= 0.0f && b.dive <= 0.0f && b.stamina >= kPushCost * 0.5f) pushFrom(b);
    if (b.stun > 0.0f) {
        b.stun -= dt;
        wish = glm::vec3(0.0f);
    }
    const float accel = b.dive > 0.0f ? 6.0f : b.grounded ? 32.0f : 11.0f;
    glm::vec3 dv = wish - b.velocity;
    const float dl = glm::length(dv);
    if (dl > accel * dt) dv *= accel * dt / dl;
    b.velocity += dv;
    // Turn to face where you run (quickly: beans are nimble).
    if (glm::length(wish) > 0.3f && b.dive <= 0.0f && b.stun <= 0.0f) {
        const float d = wrap180(yawOf(wish) - b.yaw);
        const float step = 900.0f * dt;
        b.yaw += std::clamp(d, -step, step);
    }
    // Jump: a press counts for a moment (pressed just before landing), and
    // a little after leaving an edge. A jump the moment you land goes less
    // high, and so does one on an empty tank.
    b.jumpBuffer = in.jump ? kJumpBuffer : std::max(0.0f, b.jumpBuffer - dt);
    const bool canJump = (b.grounded || (b.airTime < kCoyote && !b.jumped)) && b.dive <= 0.0f && b.stun <= 0.0f;
    if (b.jumpBuffer > 0.0f && canJump) {
        if (b.grounded && b.groundTime < kRepeatWindow && b.repeatJumps > 0) ++b.repeatJumps;
        else b.repeatJumps = 1;
        const float repeat = std::max(kRepeatMin, std::pow(kRepeatScale, static_cast<float>(b.repeatJumps - 1)));
        const float tired = b.stamina >= kJumpCost ? 1.0f : 0.75f;
        b.stamina = std::max(0.0f, b.stamina - kJumpCost);
        ci.jump = true;
        ci.jumpSpeed = kJumpSpeed * std::sqrt(repeat * tired); // height goes with speed squared
        ci.jumpInAir = true; // coyote time is ours to decide
        b.jumpBuffer = 0.0f;
        b.jumped = true;
        b.airTime = kCoyote;
        b.squash = -0.25f;   // stretch
        sound(b.feet(w), kke::AudioMaterialTable::Rubber, 0.12f);
    }
    if (b.launch > 0.0f) {
        if (b.grounded) {
            ci.jumpSpeed = std::max(ci.jump ? ci.jumpSpeed : 0.0f, b.launch);
            ci.jump = true;
            ci.jumpInAir = true;
        } else {
            const glm::vec3 v = w.characterVelocity(b.id);
            w.setCharacterVelocity(b.id, glm::vec3(v.x, std::max(v.y, b.launch), v.z));
        }
        b.launch = 0.0f;
    }
    b.push *= std::exp(-(b.grounded ? 3.5f : 0.5f) * dt);
    glm::vec3 move = b.velocity + b.push;
    // Before GO: stay in your start area (no head start, and nobody can be
    // pushed off the start). Moving about and shoving inside it is fine.
    if (b.fenced && m_phase != Phase::Play) {
        const glm::vec3 at = w.characterPosition(b.id);
        auto hold = [&](float pos, float lo, float hi, float& v) {
            if ((pos <= lo && v < 0.0f) || (pos >= hi && v > 0.0f)) v = 0.0f;
            if (pos < lo - 0.05f) v = std::max(v, (lo - pos) * 8.0f);
            if (pos > hi + 0.05f) v = std::min(v, (hi - pos) * 8.0f);
        };
        hold(at.x, b.fenceMin.x, b.fenceMax.x, move.x);
        hold(at.z, b.fenceMin.z, b.fenceMax.z, move.z);
        if (move.x == 0.0f) b.velocity.x = b.push.x = 0.0f;
        if (move.z == 0.0f) b.velocity.z = b.push.z = 0.0f;
    }
    ci.move = move;
    w.setCharacterInput(b.id, ci);
    // A hard landing squashes the jelly.
    if (b.grounded && !was) {
        const float fall = std::max(0.0f, -b.lastVelocity.y);
        b.squashVel += std::min(fall, 12.0f) * 0.25f;
        if (fall > 4.0f) sound(b.feet(w), kke::AudioMaterialTable::Rubber, std::min(1.0f, fall / 14.0f));
    }
}

// The push button: a two-handed shove at whoever is closest in front.
void PartyModule::pushFrom(Bean& b) {
    kke::RigidWorld& w = world();
    b.pushCooldown = kPushCooldown;
    b.pushPose = 0.3f;
    b.stamina = std::max(0.0f, b.stamina - kPushCost);
    const glm::vec3 at = b.feet(w), fwd = facingOf(b.yaw);
    Bean* best = nullptr;
    float bestD = kPushReach + kBeanRadius;
    for (Bean& o : m_beans) {
        if (&o == &b || o.hidden || !o.active) continue;
        const glm::vec3 p = o.remote ? o.drawFeet : o.feet(w);
        glm::vec3 d = p - at;
        if (std::abs(d.y) > kBeanHeight * 0.8f) continue;
        d.y = 0.0f;
        const float dist = glm::length(d);
        if (dist > bestD || dist < 1e-3f || glm::dot(d / dist, fwd) < 0.45f) continue;
        bestD = dist;
        best = &o;
    }
    sound(at + glm::vec3(0.0f, 0.8f, 0.0f), kke::AudioMaterialTable::Rubber, best ? 0.45f : 0.1f);
    if (!best) return;
    glm::vec3 dir = (best->remote ? best->drawFeet : best->feet(w)) - at;
    dir.y = 0.0f;
    dir = glm::length(dir) > 1e-3f ? glm::normalize(dir) : fwd;
    const float strength = m_game ? m_game->pushStrength() : 6.0f;
    const glm::vec3 shove = dir * strength + glm::vec3(0.0f, 1.8f, 0.0f);
    if (best->remote) sendKnock(*best, shove, 0.35f); // its own machine moves it
    else knock(*best, shove, 0.35f);
    knock(b, -dir * 0.8f, 0.0f);
    if (m_game && m_phase == Phase::Play) m_game->touched(*this, b, *best);
}

void PartyModule::bumpBeans(float dt) {
    kke::RigidWorld& w = world();
    const float reach = kBeanRadius * 2.0f;
    const float strength = m_game ? m_game->bumpStrength() : 2.5f;
    for (size_t i = 0; i < m_beans.size(); ++i) {
        Bean& a = m_beans[i];
        if (a.hidden) continue;
        for (size_t j = i + 1; j < m_beans.size(); ++j) {
            Bean& c = m_beans[j];
            if (c.hidden || (a.remote && c.remote)) continue;
            const glm::vec3 pa = a.remote ? a.drawFeet : w.characterPosition(a.id), pc = c.remote ? c.drawFeet : w.characterPosition(c.id);
            if (std::abs(pa.y - pc.y) > kBeanHeight * 0.85f) continue;
            glm::vec3 d = pc - pa;
            d.y = 0.0f;
            const float dist = glm::length(d);
            if (dist >= reach) continue;
            const glm::vec3 n = dist > 1e-4f ? d / dist : glm::vec3(1.0f, 0.0f, 0.0f);
            // Pushed apart, softly (the capsules don't collide with each other).
            const float overlap = reach - dist;
            if (!a.remote) a.push -= n * overlap * 70.0f * dt;
            if (!c.remote) c.push += n * overlap * 70.0f * dt;
            // Ran or dived into: a shove, and the minigame hears of it.
            const float approach = glm::dot(a.velocity - c.velocity, n);
            auto shove = [&](Bean& by, Bean& hit, const glm::vec3& dir, float speed) {
                if (by.bumped > 0.0f) return;
                by.bumped = 0.35f;
                const bool dived = by.dive > 0.0f;
                if (dived) knock(hit, dir * (strength * 1.6f + 2.0f) * by.divePower + glm::vec3(0.0f, 3.5f, 0.0f), 0.9f);
                else if (speed > 2.5f) knock(hit, dir * strength * std::min(speed / kRunSpeed, 1.2f) + glm::vec3(0.0f, 1.2f, 0.0f), 0.0f);
                knock(by, -dir * 1.2f, 0.0f);
                sound((pa + pc) * 0.5f + glm::vec3(0.0f, 0.7f, 0.0f), kke::AudioMaterialTable::Rubber, std::min(1.0f, speed / 8.0f + (dived ? 0.3f : 0.0f)));
                if (m_game && m_phase == Phase::Play) m_game->touched(*this, by, hit);
            };
            // Head-on dives at the same power: a clash (both here, a game that has them).
            if (m_game && m_game->chargedDive() && m_phase == Phase::Play && m_clash.a < 0 && !a.remote && !c.remote && a.dive > 0.0f &&
                c.dive > 0.0f && !a.clashing && !c.clashing && glm::dot(facingOf(a.yaw), facingOf(c.yaw)) < -0.5f &&
                std::abs(a.divePower - c.divePower) < 0.2f) {
                startClash(a, c, n);
                continue;
            }
            if (approach > 1.2f) shove(a, c, n, approach);
            else if (approach < -1.2f) shove(c, a, -n, -approach);
        }
    }
}

void PartyModule::startClash(Bean& a, Bean& b, const glm::vec3& dir) {
    m_clash = Clash{};
    m_clash.a = a.index;
    m_clash.b = b.index;
    m_clash.left = kClashTime;
    m_clash.power = std::max(a.divePower, b.divePower);
    m_clash.dir = dir;
    for (Bean* x : { &a, &b }) {
        x->clashing = true;
        x->dive = 0.0f;
        x->charge = 0.0f;
        x->velocity = x->push = glm::vec3(0.0f);
    }
    kke::RigidWorld& w = world();
    const glm::vec3 mid = (a.feet(w) + b.feet(w)) * 0.5f + glm::vec3(0.0f, 0.8f, 0.0f);
    sound(mid, kke::AudioMaterialTable::Wood, 1.0f);
    burst(mid, glm::vec3(1.0f, 0.85f, 0.3f), 30, 5.0f);
    flash("CLASH! Mash jump!", 1.2f);
}

void PartyModule::updateClash(float dt) {
    if (m_clash.a < 0) return;
    Bean* side[2] = { nullptr, nullptr };
    for (Bean& b : m_beans) {
        if (b.index == m_clash.a) side[0] = &b;
        if (b.index == m_clash.b) side[1] = &b;
    }
    const bool broken = !side[0] || !side[1] || m_phase != Phase::Play || side[0]->hidden || side[1]->hidden || !side[0]->active || !side[1]->active;
    kke::RigidWorld& w = world();
    if (!broken) {
        const glm::vec3 mid = (side[0]->feet(w) + side[1]->feet(w)) * 0.5f + glm::vec3(0.0f, 0.8f, 0.0f);
        for (int k = 0; k < 2; ++k) {
            Bean& b = *side[k];
            const bool press = b.bot ? m_botRng.unit() < dt * (5.0f + 2.5f * static_cast<float>(b.difficulty)) : b.input.jump;
            if (press) {
                ++m_clash.presses[k];
                b.squash = 0.25f;
                if (m_botRng.below(2) == 0) burst(mid, k == 0 ? glm::vec3(1.0f, 0.6f, 0.2f) : glm::vec3(0.4f, 0.7f, 1.0f), 3, 2.5f);
            }
            // Locked together, leaning in, nothing else.
            b.input = BeanInput{};
            b.velocity = b.push = glm::vec3(0.0f);
            b.yaw = yawOf(k == 0 ? m_clash.dir : -m_clash.dir);
        }
        status("CLASH  " + std::to_string(m_clash.presses[0]) + " : " + std::to_string(m_clash.presses[1]));
        m_clash.left -= dt;
        if (m_clash.left > 0.0f) return;
    }
    if (!broken) {
        // The loser flies at twice the power they met with.
        const int winner = m_clash.presses[0] == m_clash.presses[1] ? m_botRng.below(2) : m_clash.presses[0] > m_clash.presses[1] ? 0 : 1;
        Bean& win = *side[winner];
        Bean& lose = *side[1 - winner];
        const glm::vec3 dir = winner == 0 ? m_clash.dir : -m_clash.dir;
        const float strength = m_game ? m_game->bumpStrength() : 2.5f;
        knock(lose, dir * (strength * 1.6f + 2.0f) * m_clash.power * 2.0f + glm::vec3(0.0f, 5.0f, 0.0f), 1.2f);
        knock(win, -dir * 1.0f, 0.0f);
        sound(lose.feet(w) + glm::vec3(0.0f, 0.8f, 0.0f), kke::AudioMaterialTable::Rubber, 1.0f);
        if (m_game) m_game->touched(*this, win, lose);
    }
    for (Bean* b : side)
        if (b) b->clashing = false;
    m_clash = Clash{};
}

void PartyModule::ensureMesh(Bean& b) {
    if (b.meshBuilt && b.builtLook == b.look) return;
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    buildBean(b.look, v, idx);
    if (b.body) m_app->renderer().retire(std::shared_ptr<void>(std::move(b.body)));
    b.body = std::make_shared<kke::DynamicMeshRenderer>(*m_app);
    b.body->upload(v, idx);
    v.clear();
    idx.clear();
    buildLimb(b.look, v, idx);
    if (b.limb) m_app->renderer().retire(std::shared_ptr<void>(std::move(b.limb)));
    b.limb = std::make_shared<kke::DynamicMeshRenderer>(*m_app);
    b.limb->upload(v, idx);
    b.builtLook = b.look;
    b.meshBuilt = true;
}

void PartyModule::animateBean(Bean& b, float dt) {
    ensureMesh(b);
    const glm::vec3 vel = b.remote ? b.velocity : world().characterVelocity(b.id);
    // Jelly: squash (positive) and stretch (negative) on a spring.
    b.squashVel += (-220.0f * b.squash - 11.0f * b.squashVel) * dt;
    b.squash = std::clamp(b.squash + b.squashVel * dt, -0.35f, 0.35f);
    // Lean into acceleration, in the bean's own frame.
    const glm::vec3 acc = dt > 0.0f ? (vel - b.lastVelocity) / dt : glm::vec3(0.0f);
    const glm::vec3 f = facingOf(b.yaw), r = glm::cross(f, glm::vec3(0, 1, 0));
    const glm::vec2 want = glm::clamp(glm::vec2(glm::dot(acc, f), glm::dot(acc, r)) * 0.012f, glm::vec2(-0.25f), glm::vec2(0.25f));
    b.leanVel += ((want - b.lean) * 90.0f - b.leanVel * 9.0f) * dt;
    b.lean += b.leanVel * dt;
    b.lastVelocity = vel;
    const float speed = glm::length(glm::vec2(vel.x, vel.z));
    b.walkPhase += dt * (b.grounded ? speed * 2.6f : 0.0f);
    b.tumble = b.stun > 0.0f ? b.tumble + dt * 12.0f : b.tumble * std::exp(-10.0f * dt);
    b.wave += dt;
    // A person instead of a bean: the same body matrix, their own clips
    // (a person's dive is already a roll, so no extra tilt for it).
    if (m_people.sync(b)) {
        const bool cheer = b.finished || m_phase == Phase::Podium;
        m_people.animate(b, bodyMatrix(b), speed, cheer, !b.hidden, dt);
    }
}

glm::mat4 PartyModule::bodyMatrix(const Bean& b) {
    const glm::vec3 feet = b.remote ? b.drawFeet : world().characterDrawPosition(b.id, m_app->fixedAlpha());
    glm::mat4 m = glm::translate(glm::mat4(1.0f), feet);
    m = glm::rotate(m, glm::radians(-b.yaw), glm::vec3(0, 1, 0));
    const float mid = kBeanHeight * 0.5f;
    // Diving: flat on the belly; tumbling: rolling over; leaning into moves.
    const float diveTilt = b.dive > 0.0f ? std::min(1.0f, (kDiveTime - b.dive) * 6.0f) : 0.0f;
    m = glm::translate(m, glm::vec3(0.0f, mid * (1.0f - diveTilt * 0.55f), 0.0f));
    const float haul = b.pulling ? 0.35f : 0.0f; // leaning back on a rope
    m = glm::rotate(m, -diveTilt * glm::radians(80.0f) - b.lean.x - std::sin(b.tumble) * 1.4f + haul, glm::vec3(1, 0, 0));
    m = glm::rotate(m, b.lean.y + std::sin(b.tumble * 0.7f) * 0.6f, glm::vec3(0, 0, 1));
    return glm::translate(m, glm::vec3(0.0f, -mid, 0.0f));
}

void PartyModule::drawBean(const Bean& b, const kke::RenderContext* ctx, const kke::ShadowRenderContext* shadow) {
    if (b.hidden || !b.meshBuilt || b.person) return; // a person is drawn by ModelModule (People)
    const glm::mat4 m = bodyMatrix(b);
    const glm::mat4 body = glm::scale(m, glm::vec3(1.0f + b.squash * 0.5f, 1.0f - b.squash, 1.0f + b.squash * 0.5f));
    if (ctx) b.body->draw(*ctx, body, 0.0f, 0.32f);
    if (shadow) b.body->drawShadow(*shadow, body);
    // Hands and feet: feet step with the run, hands swing (and wave when
    // they've won or finished).
    const float step = std::sin(b.walkPhase);
    const float sq = 1.0f - b.squash;
    for (int s = -1; s <= 1; s += 2) {
        const float fs = static_cast<float>(s);
        const glm::vec3 foot(0.17f * fs, 0.07f, -0.04f + step * fs * 0.16f);
        const glm::mat4 fm = glm::scale(glm::translate(m, foot), glm::vec3(0.12f, 0.08f, 0.17f));
        const bool cheer = b.finished || m_phase == Phase::Podium;
        const float swing = cheer ? 0.35f + 0.12f * std::sin(b.wave * 9.0f + fs) : -step * fs * 0.08f;
        glm::vec3 hand(0.47f * fs, (0.55f + (cheer ? 0.45f : 0.0f)) * sq, swing);
        // A push: both hands shoot out in front.
        const float shove = b.pushPose > 0.0f ? std::sin(b.pushPose / 0.3f * 3.14159f) : 0.0f;
        hand = glm::mix(hand, glm::vec3(0.3f * fs, 0.68f * sq, -0.62f), shove);
        if (b.pulling) hand = glm::vec3(0.14f * fs, 0.62f * sq, -0.5f - 0.18f * (fs + 1.0f)); // one hand behind the other on the rope
        const glm::mat4 hm = glm::scale(glm::translate(m, hand), glm::vec3(0.1f));
        if (ctx) {
            b.limb->draw(*ctx, fm, 0.0f, 0.4f);
            b.limb->draw(*ctx, hm, 0.0f, 0.4f);
        }
        if (shadow) {
            b.limb->drawShadow(*shadow, fm);
            b.limb->drawShadow(*shadow, hm);
        }
    }
}

void PartyModule::updateCameras(float dt) {
    std::vector<kke::Application::View>& views = m_app->views();
    views.clear();
    std::vector<Bean*> players;
    for (Bean& b : m_beans)
        if (b.seat >= 0 && !b.remote) players.push_back(&b);
    std::sort(players.begin(), players.end(), [](const Bean* a, const Bean* b) { return a->player < b->player; });
    kke::Camera& main = m_app->camera();
    const bool shared = !m_game || m_phase == Phase::Podium || players.empty(); // every round: behind your own bean
    if (shared) {
        if (m_game) {
            m_game->overview(m_overview);
        } else {
            // The podium (and the lobby): the stage, a little from above.
            const glm::vec3 at(0.0f, 0.0f, 400.0f);
            m_overview.position = at + glm::vec3(0.0f, 3.4f, 7.5f);
            m_overview.target = at + glm::vec3(0.0f, 1.3f, -4.0f);
        }
        // A tall screen (a phone upright) sees less across: step back part
        // of the way (all of it would leave the beans tiny; the arena's
        // ends may be cut, the middle where they play is not).
        const VkExtent2D e = m_app->renderer().extent();
        const float aspect = e.height ? static_cast<float>(e.width) / static_cast<float>(e.height) : 1.0f;
        const float back = std::clamp(std::pow((16.0f / 9.0f) / std::max(0.1f, aspect), 0.35f), 1.0f, 1.6f);
        main.position = m_overview.target + (m_overview.position - m_overview.target) * back;
        main.target = m_overview.target;
        return;
    }
    kke::RigidWorld& w = world();
    const std::vector<kke::ViewRect> rects = kke::splitScreen(static_cast<int>(players.size()), true);
    for (size_t i = 0; i < players.size(); ++i) {
        Bean& b = *players[i];
        kke::Camera& cam = cameraOf(b);
        if (&cam != &main) {
            cam.fovDegrees = main.fovDegrees;
            cam.nearPlane = main.nearPlane;
            cam.farPlane = main.farPlane;
        }
        // Out (or finished): watch someone still playing.
        const Bean* focus = &b;
        if (!b.active || b.hidden) {
            if (b.watching < 0 || b.watching >= static_cast<int>(m_beans.size()) || !m_beans[static_cast<size_t>(b.watching)].active)
                for (const Bean& o : m_beans)
                    if (o.active && !o.hidden) b.watching = o.index;
            // (an out bean's own controls are off, so ask the buttons)
            const kke::InputMap& in = m_input->map(b.player);
            if (in.pressed("jump") || in.pressed("dive")) {
                // Next one still playing.
                for (size_t k = 1; k <= m_beans.size(); ++k) {
                    const Bean& o = m_beans[(static_cast<size_t>(std::max(0, b.watching)) + k) % m_beans.size()];
                    if (o.active && !o.hidden) {
                        b.watching = o.index;
                        break;
                    }
                }
            }
            if (b.watching >= 0 && !b.hidden && b.finished) focus = &b; // finished: see yourself celebrate unless everyone's done
            if (b.watching >= 0 && (b.hidden || b.out)) focus = &m_beans[static_cast<size_t>(b.watching)];
        }
        const glm::vec3 feet = focus->remote ? focus->drawFeet : w.characterDrawPosition(focus->id, m_app->fixedAlpha());
        b.rig.settings.armLength = m_cameraDistance;
        b.rig.settings.pivotHeight = 1.2f;
        b.rig.settings.shoulderOffset = m_game && m_phase != Phase::Podium ? m_game->cameraSide() : 0.0f;
        b.rig.update(dt, feet, [&w](const glm::vec3& from, const glm::vec3& dir, float maxDist) {
            const auto hit = w.raycast(from, dir, maxDist);
            return hit.hit ? hit.distance : maxDist;
        }, cam);
        if (players.size() > 1) views.push_back({ cam, rects[i] });
    }
    // Three players: the empty quarter shows the whole arena, or in a
    // race, follows whoever is out in front.
    if (players.size() == 3 && m_game) {
        if (m_game->camera() == CameraStyle::Overview) {
            m_game->overview(m_overview);
        } else {
            const float yaw = glm::radians(m_game->followYaw());
            const glm::vec3 ahead(-std::sin(yaw), 0.0f, -std::cos(yaw)); // yaw 0 = -Z
            const Bean* lead = nullptr;
            float best = -1e9f;
            for (const Bean& b : m_beans) {
                if (!b.active || b.hidden) continue;
                const float along = glm::dot(b.remote ? b.drawFeet : b.feet(w), ahead);
                if (along > best) {
                    best = along;
                    lead = &b;
                }
            }
            if (lead) {
                const glm::vec3 at = lead->remote ? lead->drawFeet : lead->feet(w);
                // High behind them, looking down over any wall at the start.
                const glm::vec3 want = at - ahead * 4.0f + glm::vec3(0.0f, 9.0f, 0.0f);
                const bool jump = glm::length(m_overview.position - want) > 20.0f; // a new level: cut, don't fly
                m_overview.target = at + glm::vec3(0.0f, 0.5f, 0.0f) + ahead * 4.0f;
                m_overview.position = jump ? want : glm::mix(m_overview.position, want, std::min(1.0f, dt * 4.0f));
            }
        }
        m_overview.fovDegrees = main.fovDegrees;
        m_overview.nearPlane = main.nearPlane;
        m_overview.farPlane = main.farPlane;
        views.push_back({ m_overview, kke::splitScreen(4, true)[3] });
    }
}

// ---- Particles --------------------------------------------------------

void PartyModule::burst(const glm::vec3& at, const glm::vec3& color, int count, float speed) {
    for (int i = 0; i < count && m_particles.size() < 2000; ++i) {
        Particle p;
        p.pos = at;
        const glm::vec3 dir(m_botRng.range(-1.0f, 1.0f), m_botRng.range(0.2f, 1.2f), m_botRng.range(-1.0f, 1.0f));
        p.vel = glm::normalize(dir) * speed * m_botRng.range(0.4f, 1.0f);
        // Confetti: the colour, and some party colours in between.
        p.color = m_botRng.unit() < 0.5f ? color : beanColour(m_botRng.below(12));
        p.color2 = p.color;
        p.size = m_botRng.range(0.04f, 0.08f);
        p.life = m_botRng.range(1.2f, 2.2f);
        p.gravity = 6.0f;
        p.spin = m_botRng.range(-12.0f, 12.0f);
        m_particles.push_back(p);
    }
}

void PartyModule::rubble(const glm::vec3& at, const glm::vec3& color, int count, float radius) {
    for (int i = 0; i < count && m_particles.size() < 2000; ++i) {
        Particle p;
        const float ang = m_botRng.range(0.0f, 6.2831853f), r = radius * std::sqrt(m_botRng.unit());
        p.pos = at + glm::vec3(std::cos(ang) * r, m_botRng.range(-0.1f, 0.1f), std::sin(ang) * r);
        p.vel = glm::vec3(std::cos(ang) * 0.4f, m_botRng.range(-0.5f, 0.6f), std::sin(ang) * 0.4f);
        p.color = color * m_botRng.range(0.7f, 1.0f);
        p.color2 = color * 0.45f; // darker as it goes, like dust settling
        p.size = m_botRng.range(0.07f, 0.16f);
        p.life = m_botRng.range(1.4f, 2.4f);
        p.gravity = 9.8f;
        p.spin = m_botRng.range(-5.0f, 5.0f);
        m_particles.push_back(p);
    }
}

void PartyModule::flame(const glm::vec3& at, float size) {
    if (m_particles.size() >= 2000) return;
    Particle p;
    p.pos = at + glm::vec3(m_botRng.range(-0.3f, 0.3f), 0.0f, m_botRng.range(-0.3f, 0.3f)) * size;
    p.vel = glm::vec3(m_botRng.range(-0.3f, 0.3f), m_botRng.range(1.2f, 2.4f), m_botRng.range(-0.3f, 0.3f)) * size;
    p.color = glm::vec3(1.0f, 0.85f, 0.25f);  // yellow at the core
    p.color2 = glm::vec3(0.6f, 0.08f, 0.02f); // dark red as it goes
    p.size = size * m_botRng.range(0.1f, 0.18f);
    p.life = m_botRng.range(0.3f, 0.55f);
    p.gravity = -1.5f; // rises
    p.spin = m_botRng.range(-4.0f, 4.0f);
    m_particles.push_back(p);
}

void PartyModule::updateParticles(float dt) {
    std::vector<kke::Vertex> v;
    std::vector<uint32_t> idx;
    MeshBuilder mb{ v, idx };
    for (Particle& p : m_particles) {
        p.age += dt;
        p.vel.y -= p.gravity * dt;
        p.vel *= std::exp(-0.6f * dt);
        p.pos += p.vel * dt;
    }
    std::erase_if(m_particles, [](const Particle& p) { return p.age >= p.life; });
    for (const Particle& p : m_particles) {
        const float t = p.age / p.life;
        const float s = p.size * (p.gravity < 0.0f ? 1.0f - t * 0.6f : 1.0f);
        const glm::mat3 rot = glm::mat3(glm::rotate(glm::mat4(1.0f), p.spin * p.age, glm::normalize(glm::vec3(0.3f, 1.0f, 0.5f))));
        mb.box(p.pos, glm::vec3(s, s * 0.35f, s), glm::mix(p.color, p.color2, t), rot);
    }
    m_particleMesh->upload(v, idx);
}

} // namespace party
