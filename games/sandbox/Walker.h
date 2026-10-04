#pragma once

#include "kke/Animator.h"
#include "kke/CameraRig.h"
#include "kke/CharacterIk.h"
#include "kke/Locomotion.h"
#include "kke/ModelAsset.h"
#include "kke/RigidWorld.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <memory>
#include <optional>
#include <string>

namespace kke {
class Application;
struct AssetCatalog;
}

namespace kke_sandbox {

// Walk mode's player (Kees, 2026-09-28: "a 3rd person one where you are a
// character in a world"): a person you walk, run, jump and climb with on
// Jolt's character controller (kke::Locomotion), seen over the shoulder
// (kke::CameraRig, V for first person), animated from the Universal
// Animation Library clips with kke::CharacterIk on top: feet on the
// ground, and hands on whatever the sandbox puts in them (the bat's
// handle, the gun's grip).
//
// The same pieces as kke_demo's player (games/showcase), cut down to what a
// sandbox needs: no traversal clips, no split screen, no network.
// Without assets/animations/UAL1_Standard.fbx the body is a capsule drawn
// by the sandbox (body() is 0).
class Walker {
public:
    Walker() = default;
    ~Walker();
    Walker(const Walker&) = delete;
    Walker& operator=(const Walker&) = delete;

    // Adds the character at `feet`, facing `yaw` (degrees, the CameraRig's
    // convention: 0 looks along -Z). Loads the mannequin on first use.
    void begin(kke::Application& app, kke::RigidWorld& world, const glm::vec3& feet, float yaw);
    // Takes the character out of the world (Fly mode, shutdown).
    void end();
    bool active() const { return m_id != 0; }

    // A Synty person (an SK_ asset in `catalog`) wearing the UAL clips, or
    // "" for the mannequin. False if it can't be used (no skeleton).
    bool useCharacter(const kke::AssetCatalog* catalog, const std::string& asset);
    const std::string& character() const { return m_character; }

    struct Controls {
        glm::vec2 move{0.0f};     // stick / WASD, y forward
        glm::vec2 look{0.0f};     // degrees this frame (yaw, pitch)
        bool jump = false;        // pressed this frame
        bool sprint = false, walk = false, crouch = false;
        bool toggleView = false;  // first <-> third person
        float zoom = 0.0f;        // + = closer
    };
    // Moves the character, animates it and points the camera.
    void update(float dt, float alpha, const Controls& c, kke::Camera& camera);

    // Hands for this frame's pose (world space), applied in update(); a
    // hand without one follows the clip. Call before update().
    void hand(kke::CharacterIk::Side side, const glm::vec3& point, const std::optional<glm::vec3>& elbow = std::nullopt);
    // A one-off clip over the moves ("PickUp_Table", "Pistol_Shoot",
    // "Sword_Attack"): plays once, then back to walking. False if the
    // clip isn't in the set.
    bool playOnce(const std::string& clip, float speed = 1.0f);
    // Hold a pose while a tool is in hand ("Pistol_Idle_Loop"), "" = none.
    void holdPose(const std::string& clip);

    glm::vec3 feet() const;               // drawn position (smooth)
    glm::vec3 velocity() const;
    float facingYaw() const;              // degrees, the body's
    glm::vec3 facing() const;             // unit, horizontal
    void face(const glm::vec3& direction);
    void teleport(const glm::vec3& feet);
    // Where the shoulders are now (world), from the last pose; a fallback
    // above the feet before the first one.
    glm::vec3 shoulder(kke::CharacterIk::Side side) const;
    // The palm's transform (world) after the last pose, for holding things.
    bool palm(kke::CharacterIk::Side side, glm::mat4& out) const;
    kke::CameraRig& rig() { return m_rig; }
    bool firstPerson() const { return m_rig.mode == kke::CameraRig::Mode::FirstPerson; }
    kke::ModelModule::InstanceId body() const { return m_instance; }
    kke::RigidWorld::CharacterId id() const { return m_id; }

private:
    void loadMannequin();
    void buildAnimator();
    void animate(float dt);
    void applyIk(float dt);

    kke::Application* m_app = nullptr;
    kke::RigidWorld* m_world = nullptr;
    kke::ModelModule* m_models = nullptr;
    kke::RigidWorld::CharacterId m_id = 0;
    std::unique_ptr<kke::Locomotion> m_loco;
    kke::CameraRig m_rig;
    bool m_crouch = false;
    bool m_jump = false;

    kke::ModelModule::ModelId m_ual = 0;    // the UAL1 mannequin (and its clips)
    bool m_ualTried = false;
    kke::ModelModule::ModelId m_model = 0;  // what is drawn: the mannequin or a Synty person
    kke::ModelModule::InstanceId m_instance = 0;
    std::string m_character;
    kke::ModelData m_rigData;                 // the drawn model's bones + the UAL clips for them
    std::unique_ptr<kke::AnimationSet> m_set;
    std::unique_ptr<kke::Animator> m_anim;
    kke::CharacterIk m_ik;
    float m_modelYaw = 0.0f;                // turns the model to face -Z
    int m_stMove = -1, m_stCrouch = -1, m_stJump = -1, m_stFall = -1, m_stLand = -1;
    int m_stOnce = -1, m_stHold = -1;
    std::string m_onceClip, m_holdClip;
    std::vector<glm::mat4> m_lastBones;     // model space, after IK
    glm::mat4 m_lastWorld{1.0f};
    struct HandGoal { glm::vec3 point{0.0f}; std::optional<glm::vec3> elbow; bool set = false; };
    HandGoal m_hands[2];
    int m_handBone[2] = { -1, -1 };
};

} // namespace kke_sandbox
