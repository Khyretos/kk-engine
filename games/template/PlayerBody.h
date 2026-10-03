#pragma once

#include "kke/Animator.h"
#include "kke/CharacterIk.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include <memory>

namespace kke {
class Locomotion;
} // namespace kke

namespace starter {

// The player's body: Quaternius' CC0 mannequin (assets/animations/
// UAL1_Standard.fbx, it ships with the engine) playing the clip that
// matches what kke::Locomotion is doing, with kke::CharacterIk on top:
// feet flat on steps and slopes, hands on the edge of a fence or block
// while vaulting and climbing, and a lean into speeding up and turning.
// docs/PROCEDURAL_ANIMATION.md ("Clip, contacts, lean") explains the layers.
//
// Your own character: put its file in assets/ and load it here instead;
// a humanoid with the usual bone names takes these clips as they are.
class PlayerBody {
public:
    PlayerBody();
    ~PlayerBody();
    // Loads the mannequin. False: it isn't there (draw something else).
    bool load(kke::ModelModule& models);
    bool loaded() const { return m_instance != 0; }
    // Every frame, after Locomotion::update: `feet` where the body is drawn
    // (RigidWorld::characterDrawPosition), `visible` false in first person.
    void update(const kke::Locomotion& loco, kke::RigidWorld& world, kke::RigidWorld::CharacterId id, const glm::vec3& feet, bool visible,
                float dt);

private:
    void animate(const kke::Locomotion& loco);
    void pose(const kke::Locomotion& loco, kke::RigidWorld& world, kke::RigidWorld::CharacterId id, float dt);

    kke::ModelModule* m_models = nullptr;
    kke::ModelModule::ModelId m_model = 0;
    kke::ModelModule::InstanceId m_instance = 0;
    const kke::ModelData* m_data = nullptr;
    std::unique_ptr<kke::AnimationSet> m_set;
    std::unique_ptr<kke::Animator> m_anim;
    kke::CharacterIk m_ik;
    float m_modelYaw = 0.0f; // turns the model to face -Z, like Locomotion's yaw 0
    int m_move = -1, m_jump = -1, m_fall = -1, m_land = -1, m_vault = -1, m_climb = -1;
};

} // namespace starter
