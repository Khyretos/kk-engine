#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/ModelAsset.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>

namespace kke {
class Application;
class DynamicMeshRenderer;
struct RenderContext;
struct ShadowRenderContext;
} // namespace kke

namespace tennis {

// The people: Quaternius' Universal Animation Library mannequin (CC0,
// assets/animations/UAL1_Standard.fbx), with UAL 2's side steps and
// cheers when that pack is there. Loaded once (Rig), shared by every
// player and spectator. Without the file everyone is a coloured block.
class Rig {
public:
    explicit Rig(kke::Application& app);
    ~Rig();
    bool load(kke::ModelModule& models);
    bool loaded() const { return m_model != 0; }

    kke::ModelModule::ModelId model() const { return m_model; }
    const kke::ModelData& data() const { return m_rig; }
    const kke::AnimationSet& set() const { return *m_set; }
    float modelYaw() const { return m_modelYaw; }
    const kke::TwoBoneChain& arm(int side) const { return m_arm[side]; } // 0 left, 1 right
    int bone(const char* name) const { return m_rig.findBone(name); }
    kke::DynamicMeshRenderer& block() { return *m_block; }
    kke::DynamicMeshRenderer& racketMesh() { return *m_racket; }
    bool sideSteps() const { return m_sideSteps; }

private:
    kke::Application& m_app;
    kke::ModelModule::ModelId m_model = 0;
    kke::ModelData m_rig;
    std::unique_ptr<kke::AnimationSet> m_set;
    float m_modelYaw = 0.0f;
    kke::TwoBoneChain m_arm[2];
    bool m_sideSteps = false;
    std::unique_ptr<kke::DynamicMeshRenderer> m_block, m_racket;
};

// What the arms do this frame. A swing is procedural (two-bone IK on the
// racket arm along a path through the contact point), so the racket meets
// the ball wherever it really is.
struct SwingPose {
    enum class Kind { Ready, Forehand, Backhand, Serve, Toss } kind = Kind::Ready;
    float t = 0.0f;              // -1 wound up .. 0 contact .. 1 follow-through done
    glm::vec3 contact{0.0f};     // the ball at contact, in the body's frame (x right, y up, z forward)
};

// One person on screen. Tennis players hold a racket; spectators sit,
// stand and cheer.
class Body {
public:
    Body(Rig& rig, kke::ModelModule& models, const glm::vec3& tint, bool racket);
    ~Body();
    Body(const Body&) = delete;
    Body& operator=(const Body&) = delete;

    enum class Mood { Play, Stand, Sit, Cheer, Groan };
    // feet: world; yaw: the way the body faces (sin, 0, cos); velocity:
    // world, for the legs; facingNet: while playing, the feet shuffle
    // sideways instead of turning.
    void update(const glm::vec3& feet, float yawDegrees, const glm::vec3& velocity, const SwingPose& swing, Mood mood, float dt);
    void setTint(const glm::vec3& tint);
    void setVisible(bool visible);
    bool visible() const { return m_visible; }
    // The racket's grip-to-head transform in the world (for drawing it and
    // for the hit's sound); identity without a racket.
    const glm::mat4& racket() const { return m_racketWorld; }
    // The hand holding the ball on a serve (world).
    glm::vec3 tossHand() const { return m_tossHand; }

    void render(const kke::RenderContext& ctx);
    void renderShadow(const kke::ShadowRenderContext& ctx);

private:
    Rig& m_rig;
    kke::ModelModule& m_models;
    kke::ModelModule::InstanceId m_instance = 0;
    std::unique_ptr<kke::Animator> m_anim;
    struct States { int idle = -1, run = -1, sprint = -1, left = -1, right = -1, back = -1, sit = -1, cheer = -1, groan = -1, clap = -1; } m_st;
    bool m_hasRacket = false;
    bool m_visible = true;
    glm::vec3 m_tint{1.0f};
    glm::mat4 m_xf{1.0f};
    glm::mat4 m_racketWorld{1.0f};
    glm::vec3 m_tossHand{0.0f};
};

} // namespace tennis
