#pragma once

#include "kke/Locomotion.h"
#include "kke/RigidWorld.h"
#include "kke/net/InputReplay.h"

#include <functional>
#include <optional>
#include <vector>

namespace kke::net {

// kke::Locomotion (vault, climb, hang, leap, wall run) behind Rewindable,
// for input replay (kke/net/InputReplay.h): the same class on the client
// (Prediction) and on the server (one per player, fed by
// NetServer::nextInput). Takes the character out of RigidWorld::step()
// (RigidWorld::setCharacterManual): each input steps it here, once.
//
// A history slot keeps a copy of the Locomotion and the character's whole
// state (RigidWorld::characterState), so going back and replaying gives
// the very same numbers as the first time.
class LocomotionReplay : public Rewindable {
public:
    LocomotionReplay(RigidWorld& world, RigidWorld::CharacterId id, Locomotion& locomotion, size_t slots = 128);
    ~LocomotionReplay() override;
    LocomotionReplay(const LocomotionReplay&) = delete;
    LocomotionReplay& operator=(const LocomotionReplay&) = delete;

    void step(const InputFrame& in, float dt) override;
    void save(size_t slot) override;
    void load(size_t slot) override;
    NetPlayerState state() const override;
    // Position and velocity; a different movement state starts the
    // Locomotion afresh there (a vault or hang can't be rebuilt from a
    // snapshot: it continues as walking or falling until the server's
    // next answer, usually with the traversal over).
    void correct(const NetPlayerState& server) override;

    // Capsule heights for kButtonCrouch (standing up waits for room).
    // Hanging, crouch means "let go" and the capsule stays as it is.
    float standHeight = 1.8f, crouchHeight = 1.0f;
    // First person: the body faces InputFrame::yaw, not where it walks.
    bool faceInputYaw = false;
    // The game's own touches on what others see (flags, aux).
    std::function<void(NetPlayerState&)> describe;

    Locomotion& locomotion() { return m_loco; }

private:
    struct Slot {
        std::optional<Locomotion> loco;
        RigidWorld::CharacterState character;
    };
    RigidWorld& m_world;
    RigidWorld::CharacterId m_id;
    Locomotion& m_loco;
    std::vector<Slot> m_slots;
};

} // namespace kke::net
