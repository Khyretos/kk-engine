#pragma once

#include "kke/Animator.h"
#include "kke/CharacterIk.h"
#include "kke/Equipment.h"
#include "kke/ModelAsset.h"
#include "kke/Module.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <map>
#include <memory>
#include <optional>
#include <string>

namespace kke {
class Application;
class DynamicMeshRenderer;
} // namespace kke

// Shared by the command demos (games/pet_companion, games/platoon): people
// on screen, the input that turns into orders, and the HUD around them.
namespace command_kit {

// The UAL mannequin (Quaternius' Universal Animation Library, CC0,
// assets/animations/UAL1_Standard.fbx), loaded once per game and shared by
// every person in it. Without the file, people are coloured blocks.
class HumanoidKit {
public:
    explicit HumanoidKit(kke::Application& app);
    ~HumanoidKit();
    bool load(kke::ModelModule& models);
    bool loaded() const { return m_model != 0; }

    kke::ModelModule::ModelId model() const { return m_model; }
    const kke::AnimationSet& set() const { return *m_set; }
    const kke::ModelData& rig() const { return m_rig; }
    float modelYaw() const { return m_modelYaw; } // turns the model's own forward to yaw 0
    kke::DynamicMeshRenderer& block() { return *m_block; }
    // The mannequin with its mesh (fits the IK's body and hands), and its hands.
    const kke::ModelData* skinned() const;
    const kke::HandRig& hand(int side) const { return m_hands[side & 1]; } // 0 left, 1 right

private:
    kke::ModelModule* m_models = nullptr;
    kke::HandRig m_hands[2];
    kke::Application& m_app;
    kke::ModelModule::ModelId m_model = 0;
    kke::ModelData m_rig;
    std::unique_ptr<kke::AnimationSet> m_set;
    float m_modelYaw = 0.0f;
    std::unique_ptr<kke::DynamicMeshRenderer> m_block;
};

// One person: walks with the move blend (idle, walk, jog, sprint by
// ground speed) and plays actions over it ("Pistol_Shoot", "Interact",
// "Death01", ...: any part of a UAL clip name).
class Humanoid {
public:
    Humanoid(HumanoidKit& kit, kke::ModelModule& models, const glm::vec3& tint);
    ~Humanoid();
    Humanoid(const Humanoid&) = delete;
    Humanoid& operator=(const Humanoid&) = delete;

    // "" goes back to moving. The same clip again doesn't restart it
    // unless `restart`.
    void act(const std::string& clip, bool loop = false, float speed = 1.0f, float fade = 0.15f, bool restart = false);
    const std::string& acting() const { return m_action; }
    bool actionFinished() const; // a one-shot action has played through
    void update(const glm::vec3& feet, float yawDegrees, float groundSpeed, float dt);
    void setTint(const glm::vec3& tint);
    void setVisible(bool visible);

    // Hands and feet on the world (kke::CharacterIk on top of the clips):
    // off until enableIk. Then the feet stand on `ground`, and for one
    // frame at a time a hand can go somewhere (a ball on the grass, a
    // dog's head) and close round a ball. Sides: 0 left, 1 right.
    void enableIk(kke::CharacterIk::GroundQuery ground);
    // Before update(), this frame only: the palm goes to `point` (world).
    // Without one the hand fades back to the clip.
    void reach(int side, const glm::vec3& point, const std::optional<glm::vec3>& elbowToward = std::nullopt);
    // Before update(), this frame only: the fingers close round a ball of
    // `radius` lying in that palm.
    void grip(int side, float radius);
    // After update(): the palm (world; origin on the palm's skin, +Z out
    // of it). False without the mannequin.
    bool palm(int side, glm::mat4& world) const;
    // Where a ball of `radius` held in that palm has its centre (world).
    glm::vec3 inPalm(int side, float radius) const;
    float reachWeight(int side) const;

    // The block body, when there is no mannequin.
    void render(const kke::RenderContext& ctx);
    void renderShadow(const kke::ShadowRenderContext& ctx);

private:
    HumanoidKit& m_kit;
    kke::ModelModule& m_models;
    kke::ModelModule::InstanceId m_instance = 0;
    std::unique_ptr<kke::Animator> m_anim;
    int m_move = -1;
    std::map<std::string, int> m_states;
    std::string m_action;
    glm::mat4 m_transform{1.0f};
    glm::vec3 m_tint{1.0f};
    bool m_visible = true;
    // IK (enableIk).
    std::unique_ptr<kke::CharacterIk> m_ik;
    kke::CharacterIk::GroundQuery m_ground;
    float m_grip[2] = { 0.0f, 0.0f };
    std::vector<glm::mat4> m_bones; // model space, after the IK
    glm::vec3 m_lastFeet{0.0f};
    bool m_haveFeet = false;
};

} // namespace command_kit
