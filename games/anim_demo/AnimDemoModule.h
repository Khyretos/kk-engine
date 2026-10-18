#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/Module.h"
#include "kke/SphereImpostors.h"
#include "kke/modules/ModelModule.h"

#include <memory>
#include <string>
#include <vector>

namespace kke_anim {

// The animation demo (ENG-G19): the engine's animation tools side by side.
//  - A mannequin steered with the move stick while it keeps facing the way
//    the camera looks: one 2D blend space (kke::BlendSpace2D) over UAL2's
//    walk in eight directions, idle and a forward jog, its weights shown live.
//  - Two copies of it modelled in an A-pose (arms 45 degrees down) that
//    copy its pose every frame (kke::PoseRetarget): one without rest
//    matching (the arms stay low), one with it (they match).
//  - Two tentacles reaching for the mannequin's head when it comes near: FABRIK (no limits)
//    and CCD with a bend limit per joint and damping (kke::ccdPoints).
class AnimDemoModule : public kke::Module {
public:
    const char* name() const override { return "AnimDemo"; }
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderTranslucent(const kke::RenderContext& ctx) override;

private:
    struct Tentacle {
        glm::vec3 base{0.0f};
        std::vector<glm::vec3> points; // joints, base first
        bool ccd = false;
        float worstBend = 0.0f;        // degrees, the sharpest joint last frame
    };

    void loadCharacters();
    void buildBlendSpace();
    void steer(float dt);
    void copyPose();
    const kke::Pose& shownPose() const; // the clip's pose, or the rest pose when asked
    void reach(Tentacle& t, const glm::vec3& target);
    void buildScenery();
    void buildTentacles();
    void buildPanel();
    std::string weightRow(int row) const; // -1: the parameters; 0..: the clips weighted, heaviest first

    kke::Application* m_app = nullptr;
    kke::ModelModule* m_models = nullptr;
    std::unique_ptr<kke::DynamicMeshRenderer> m_ground, m_arms, m_glass;

    // The mannequin and its clips (UAL1, plus UAL2's directions if found).
    kke::ModelData m_rig;
    std::unique_ptr<kke::AnimationSet> m_set;
    std::unique_ptr<kke::Animator> m_anim;
    kke::BlendSpace2D m_space;
    int m_state = -1;
    kke::ModelModule::InstanceId m_hero = 0;
    bool m_directions = false; // UAL2 found: eight directions, else forward only
    std::string m_status;

    // The A-pose copies: without and with rest matching.
    kke::ModelModule::InstanceId m_copy[2] = { 0, 0 };
    kke::PoseRetarget m_retarget[2];
    kke::ModelData m_aPose;

    glm::vec3 m_position{0.0f};
    glm::vec3 m_velocity{0.0f};
    float m_yaw = 0.0f;        // degrees, where the mannequin faces (from the camera)
    glm::vec2 m_param{0.0f};   // the blend space's: sideways, forward (m/s)
    glm::vec2 m_forcedMove{0.0f};
    bool m_forced = false;     // KKE_ANIM_MOVE: a fixed stick, for checks
    float m_circle = 0.0f;     // KKE_ANIM_CIRCLE: that stick turns round once in this many seconds
    bool m_showRest = false;   // the rest pose instead of the clips (KKE_ANIM_REST=1)
    bool m_mirrorFloor = false; // a polished floor, to see reflections (KKE_ANIM_MIRROR=1)
    bool m_glassBlock = false;  // a glass block before the tentacles, to see refraction (KKE_ANIM_GLASS=1)
    float m_runSpeed = 3.5f;
    float m_walkSpeed = 1.5f;

    Tentacle m_tentacles[2];
    float m_bendLimit = 25.0f; // degrees per joint (CCD)
    float m_damping = 6.0f;    // degrees a joint may turn per pass (CCD)
    float m_clock = 0.0f;
};

} // namespace kke_anim
