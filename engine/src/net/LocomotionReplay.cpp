#include "kke/net/LocomotionReplay.h"

#include <algorithm>
#include <cmath>

namespace kke::net {

LocomotionReplay::LocomotionReplay(RigidWorld& world, RigidWorld::CharacterId id, Locomotion& locomotion, size_t slots)
    : m_world(world), m_id(id), m_loco(locomotion), m_slots(std::max<size_t>(slots, 1)) {
    m_world.setCharacterManual(m_id, true);
}

LocomotionReplay::~LocomotionReplay() { m_world.setCharacterManual(m_id, false); }

void LocomotionReplay::step(const InputFrame& in, float dt) {
    // Crouch resizes the capsule (Locomotion leaves that to its owner);
    // hanging, it's "let go" instead.
    const bool crouchButton = (in.buttons & kButtonCrouch) != 0;
    bool crouch = crouchButton;
    if (m_loco.state() != Locomotion::State::Hang) {
        const float want = crouchButton ? crouchHeight : standHeight;
        if (std::abs(m_world.characterHeight(m_id) - want) > 1e-4f) m_world.setCharacterHeight(m_id, want);
        crouch = m_world.characterHeight(m_id) < standHeight - 1e-3f;
    }

    if (faceInputYaw) {
        const float r = glm::radians(in.yaw); // 0 = -Z, as Locomotion::facingYaw
        m_loco.setFacing(glm::vec3(std::sin(r), 0.0f, -std::cos(r)));
    }
    Locomotion::Input li;
    li.move = glm::vec3(in.move.x, 0.0f, in.move.y);
    li.fast = (in.buttons & kButtonFast) != 0;
    li.slow = (in.buttons & kButtonSlow) != 0;
    li.crouch = crouch;
    li.goUp = (in.buttons & kButtonUp) != 0;
    m_loco.update(li, dt);
    m_world.stepCharacter(m_id, dt);
}

void LocomotionReplay::save(size_t slot) {
    Slot& s = m_slots[slot % m_slots.size()];
    s.loco.reset();
    s.loco.emplace(m_loco);
    s.character = m_world.characterState(m_id);
}

void LocomotionReplay::load(size_t slot) {
    const Slot& s = m_slots[slot % m_slots.size()];
    if (!s.loco) return;
    m_loco.restore(*s.loco);
    m_world.setCharacterState(m_id, s.character);
}

NetPlayerState LocomotionReplay::state() const {
    NetPlayerState s;
    s.position = m_world.characterPosition(m_id);
    s.velocity = m_world.characterVelocity(m_id);
    float yaw = std::fmod(m_loco.facingYaw(), 360.0f);
    if (yaw < 0.0f) yaw += 360.0f;
    s.yaw = yaw;
    s.state = static_cast<uint8_t>(m_loco.state());
    s.speed = std::clamp(m_loco.groundSpeed(), 0.0f, 20.0f);
    s.progress = m_loco.traversalProgress();
    // Vault / climb: the obstacle's height (which clip); wall run: the
    // wall's side; else the fall (as the showcase sends its own player).
    const bool traversing = m_loco.state() == Locomotion::State::Vault || m_loco.state() == Locomotion::State::Climb;
    const float aux = traversing ? m_loco.lastObstacle().height
                    : m_loco.state() == Locomotion::State::WallRun ? m_loco.wallRunSide() : m_loco.fallHeight();
    s.aux = std::clamp(aux, 0.0f, 32.0f);
    if (describe) describe(s);
    return s;
}

void LocomotionReplay::correct(const NetPlayerState& server) {
    if (server.state != static_cast<uint8_t>(m_loco.state())) {
        const glm::vec3 facing = m_loco.facing();
        m_loco.teleport(server.position); // Ground (Air after its next update if nothing's below)
        m_loco.setFacing(facing);
    } else {
        m_world.moveCharacter(m_id, server.position);
    }
    m_world.setCharacterVelocity(m_id, server.velocity);
}

} // namespace kke::net
