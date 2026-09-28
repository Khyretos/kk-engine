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
        in.addBinding(IM::bind("dive", IM::pad(SDL_GAMEPAD_BUTTON_WEST)));
        in.addBinding(IM::bind("dive", IM::pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)));
        in.addBinding(IM::bind("dive", IM::key(SDL_SCANCODE_E)));
        in.addBinding(IM::bind("dive", IM::mouse(SDL_BUTTON_RIGHT)));
        in.addBinding(IM::bind("menu", IM::pad(SDL_GAMEPAD_BUTTON_BACK)));
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
    if (m_game && m_game->camera() == CameraStyle::Overview) {
        fwd = m_overview.target - m_overview.position;
        fwd.y = 0.0f;
        fwd = glm::length(fwd) > 1e-3f ? glm::normalize(fwd) : glm::vec3(0, 0, -1);
        right = glm::cross(fwd, glm::vec3(0, 1, 0));
    }
    fwd.y = right.y = 0.0f;
    glm::vec3 wish = (glm::length(fwd) > 1e-3f ? glm::normalize(fwd) : glm::vec3(0, 0, -1)) * move.y +
                     (glm::length(right) > 1e-3f ? glm::normalize(right) : glm::vec3(1, 0, 0)) * move.x;
    if (glm::length(wish) > 1.0f) wish = glm::normalize(wish);
    bi.move = glm::vec2(wish.x, wish.z);
    bi.jump = in.pressed("jump");
    bi.jumpHeld = in.held("jump");
    bi.dive = in.pressed("dive");
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
    b.bumped = std::max(0.0f, b.bumped - dt);
    BeanInput in = b.stun > 0.0f ? BeanInput{} : b.input;
    if (!b.active && m_phase == Phase::Play) in = BeanInput{}; // finished: wait at the goal
    glm::vec3 wish = glm::vec3(in.move.x, 0.0f, in.move.y) * kRunSpeed;
    kke::RigidWorld::CharacterInput ci;
    ci.airSteer = 1000.0f; // we send the exact horizontal velocity
    // Dive: a burst forward, then a belly slide that slows down.
    if (in.dive && b.dive <= 0.0f && b.stun <= 0.0f) {
        b.dive = kDiveTime;
        b.velocity = facingOf(b.yaw) * kDiveSpeed;
        if (b.grounded) b.launch = std::max(b.launch, 3.2f);
        sound(b.feet(w), kke::AudioMaterialTable::Rubber, 0.25f);
    }
    if (b.dive > 0.0f) {
        b.dive -= dt;
        wish = facingOf(b.yaw) * (b.grounded ? kDiveSpeed * std::max(0.0f, b.dive / kDiveTime) : kDiveSpeed * 0.9f);
        // Landed from a dive: back up on your feet a moment later.
        if (b.grounded && b.dive < kDiveTime - 0.25f) b.dive = std::min(b.dive, 0.2f);
    }
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
    if (in.jump && (b.grounded || b.airTime < kCoyote) && b.dive <= 0.0f && b.stun <= 0.0f) {
        ci.jump = true;
        ci.jumpSpeed = kJumpSpeed;
        b.airTime = kCoyote; // one jump
        b.squash = -0.25f;   // stretch
        sound(b.feet(w), kke::AudioMaterialTable::Rubber, 0.12f);
    }
    if (b.launch > 0.0f) {
        if (b.grounded) {
            ci.jump = true;
            ci.jumpSpeed = std::max(ci.jumpSpeed * (ci.jump ? 1.0f : 0.0f), b.launch);
        } else {
            const glm::vec3 v = w.characterVelocity(b.id);
            w.setCharacterVelocity(b.id, glm::vec3(v.x, std::max(v.y, b.launch), v.z));
        }
        b.launch = 0.0f;
    }
    b.push *= std::exp(-(b.grounded ? 3.5f : 0.5f) * dt);
    ci.move = b.velocity + b.push;
    w.setCharacterInput(b.id, ci);
    // A hard landing squashes the jelly.
    if (b.grounded && !was) {
        const float fall = std::max(0.0f, -b.lastVelocity.y);
        b.squashVel += std::min(fall, 12.0f) * 0.25f;
        if (fall > 4.0f) sound(b.feet(w), kke::AudioMaterialTable::Rubber, std::min(1.0f, fall / 14.0f));
    }
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
                if (dived) knock(hit, dir * (strength * 1.6f + 2.0f) + glm::vec3(0.0f, 3.5f, 0.0f), 0.9f);
                else if (speed > 2.5f) knock(hit, dir * strength * std::min(speed / kRunSpeed, 1.2f) + glm::vec3(0.0f, 1.2f, 0.0f), 0.0f);
                knock(by, -dir * 1.2f, 0.0f);
                sound((pa + pc) * 0.5f + glm::vec3(0.0f, 0.7f, 0.0f), kke::AudioMaterialTable::Rubber, std::min(1.0f, speed / 8.0f + (dived ? 0.3f : 0.0f)));
                if (m_game && m_phase == Phase::Play) m_game->touched(*this, by, hit);
            };
            if (approach > 1.2f) shove(a, c, n, approach);
            else if (approach < -1.2f) shove(c, a, -n, -approach);
        }
    }
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
}

void PartyModule::drawBean(const Bean& b, const kke::RenderContext* ctx, const kke::ShadowRenderContext* shadow) {
    if (b.hidden || !b.meshBuilt) return;
    const glm::vec3 feet = b.remote ? b.drawFeet : world().characterDrawPosition(b.id, m_app->fixedAlpha());
    glm::mat4 m = glm::translate(glm::mat4(1.0f), feet);
    m = glm::rotate(m, glm::radians(-b.yaw), glm::vec3(0, 1, 0));
    const float mid = kBeanHeight * 0.5f;
    // Diving: flat on the belly; tumbling: rolling over; leaning into moves.
    const float diveTilt = b.dive > 0.0f ? std::min(1.0f, (kDiveTime - b.dive) * 6.0f) : 0.0f;
    m = glm::translate(m, glm::vec3(0.0f, mid * (1.0f - diveTilt * 0.55f), 0.0f));
    m = glm::rotate(m, -diveTilt * glm::radians(80.0f) - b.lean.x - std::sin(b.tumble) * 1.4f, glm::vec3(1, 0, 0));
    m = glm::rotate(m, b.lean.y + std::sin(b.tumble * 0.7f) * 0.6f, glm::vec3(0, 0, 1));
    m = glm::translate(m, glm::vec3(0.0f, -mid, 0.0f));
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
        const glm::vec3 hand(0.47f * fs, (0.55f + (cheer ? 0.45f : 0.0f)) * sq, swing);
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
    const bool shared = !m_game || m_game->camera() == CameraStyle::Overview || m_phase == Phase::Podium || players.empty();
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
            if (b.input.jump || m_input->map(b.player).pressed("dive")) {
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
        b.rig.settings.armLength = 5.5f;
        b.rig.settings.pivotHeight = 1.2f;
        b.rig.settings.shoulderOffset = 0.0f;
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
