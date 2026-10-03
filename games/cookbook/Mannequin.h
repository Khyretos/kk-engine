#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/BodyShape.h"
#include "kke/Module.h"
#include "kke/modules/ModelModule.h"

#include <memory>

namespace kke {
class DynamicMeshRenderer;
class RigidBodyModule;
} // namespace kke

namespace cookbook {

// Animation recipes on a real skeleton (docs/cookbook/animation.md), using
// the mannequin from Quaternius' Universal Animation Library (CC0,
// assets/animations/UAL1_Standard.fbx):
//
//   - a walker going round a circle, its speed rising and falling, blended
//     idle -> walk -> jog by a 1D blend space (kke::Animator);
//   - a stander whose head turns to look at the camera (procedural look-at
//     on top of the idle clip), whose right hand reaches for a floating
//     orb (two-bone IK), and whose feet stay planted on a step (FootPlacer).
//
// Without the file (a trimmed checkout) the module logs why and does
// nothing.
class Mannequin : public kke::Module {
public:
    Mannequin();
    ~Mannequin() override;
    const char* name() const override { return "Mannequin"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;

    glm::vec3 standAt{ 3.0f, 0.0f, 1.0f };   // the stander's feet
    glm::vec3 circleAt{ -11.0f, 0.0f, 9.0f }; // centre of the walker's circle
    float circleRadius = 3.0f;

private:
    void updateWalker(float dt);
    void updateStander(float dt);
    glm::vec3 orbPosition() const;

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::ModelModule::ModelId m_model = 0;
    const kke::ModelData* m_data = nullptr;
    std::unique_ptr<kke::AnimationSet> m_set;
    float m_turnToPlusZ = 0.0f; // degrees that turn the model to face +Z

    // The walker.
    kke::ModelModule::InstanceId m_walker = 0;
    std::unique_ptr<kke::Animator> m_walkAnim;
    float m_angle = 0.0f, m_speed = 0.0f, m_speedVelocity = 0.0f;

    // The stander.
    kke::ModelModule::InstanceId m_stander = 0;
    std::unique_ptr<kke::Animator> m_standAnim;
    kke::HumanArm m_armR;               // a person's arm: elbow, shoulder and wrist ranges
    kke::BodyShape m_body;              // the body's capsules: the arm never goes through it
    kke::BodyAvoidState m_armAvoid;
    kke::FootPlacer m_feet;
    int m_head = -1;
    glm::vec3 m_headForward{ 0.0f, 0.0f, 1.0f }; // the head's forward, in the head bone's own space
    glm::vec3 m_lookAt{ 0.0f }, m_lookVelocity{ 0.0f };
    float m_time = 0.0f;
    std::unique_ptr<kke::DynamicMeshRenderer> m_orb;
};

} // namespace cookbook
