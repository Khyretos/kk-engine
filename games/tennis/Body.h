#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/BodyShape.h"
#include "kke/Equipment.h"
#include "kke/ModelAsset.h"
#include "kke/Outfit.h"
#include "kke/modules/ModelModule.h"

#include "Strings.h"
#include "Swing.h"

#include <glm/glm.hpp>
#include <map>

#include <memory>
#include <string>
#include <vector>

namespace kke {
class Application;
class DynamicMeshRenderer;
class PhysicsModule;
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
    // POLYGON Shops' tennis racket (SM_Prop_Sport_Tennis_Racket_01) when the
    // pack is under KKE_ASSETS_DIR or assets/synty; else the racket is drawn
    // from boxes.
    bool loadRacket(kke::ModelModule& models);
    // FEMFX strings (Strings.h): each racket's bed is made on its first hit.
    // Without physics the strings stay straight.
    void setPhysics(kke::PhysicsModule* physics) { m_physics = physics; }
    // A new string bed (null without physics, or past the most there can be).
    std::unique_ptr<StringBed> makeStringBed();
    void freeStringBed(const StringBed& bed) { m_freeBeds.push_back(bed.slot()); }
    const StringLayout& strings() const { return m_strings; }
    kke::DynamicMeshRenderer& stringsMesh() { return *m_stringsMesh; } // straight, racket frame
    kke::ModelModule::ModelId racketModel() const { return m_racketModel; }
    const std::string& racketTexture() const { return m_racketTexture; }
    const glm::mat4& racketAlign() const { return m_racketAlign; } // our racket frame (grip at the hand, shaft +Y, strings +Z) -> the model's
    bool loaded() const { return m_model != 0; }

    kke::ModelModule::ModelId model() const { return m_model; }
    // The mannequin in these clothes: one copy per outfit, shared.
    kke::ModelModule::ModelId outfitModel(kke::ModelModule& models, const kke::Outfit& outfit);
    const kke::ModelData& data() const { return m_rig; }
    const kke::AnimationSet& set() const { return *m_set; }
    float modelYaw() const { return m_modelYaw; }
    const kke::TwoBoneChain& arm(int side) const { return m_arm[side]; } // 0 left, 1 right
    const kke::HumanArm& humanArm(int side) const { return m_human[side]; } // the same arm with a person's ranges
    // The body as capsules fitted to the mannequin's mesh (the arms and the
    // racket keep out of it), and the hands' palms and the body's sockets.
    const kke::BodyShape& bodyShape() const { return m_body; }
    const kke::Equipment& equipment() const { return m_equip; }
    // The racket as equipment, in our racket frame: held in the right palm
    // by its handle ("main"); the left hand above it for a two-handed
    // backhand ("support") or cradling the throat while waiting ("throat").
    const kke::Equippable& racketItem() const { return m_racketItem; }
    int supportGrip() const { return m_supportGrip; }
    int throatGrip() const { return m_throatGrip; }
    const kke::TwoBoneChain& leg(int side) const { return m_leg[side]; }
    int pelvis() const { return m_pelvis; }
    int spine(int i) const { return m_spine[i]; } // 0..2, hips up
    int bone(const char* name) const { return m_rig.findBone(name); }
    kke::DynamicMeshRenderer& block() { return *m_block; }
    kke::DynamicMeshRenderer& racketMesh() { return *m_racket; }
    bool sideSteps() const { return m_sideSteps; }
    kke::Application& app() { return m_app; }

private:
    kke::Application& m_app;
    kke::ModelModule::ModelId m_model = 0;
    kke::ModelData m_base;                                      // the mannequin, to dress
    std::map<std::string, kke::ModelModule::ModelId> m_outfits; // dressed copies, by outfitKey
    kke::ModelData m_rig;
    std::unique_ptr<kke::AnimationSet> m_set;
    float m_modelYaw = 0.0f;
    kke::TwoBoneChain m_arm[2], m_leg[2];
    kke::HumanArm m_human[2];
    kke::BodyShape m_body;
    kke::Equipment m_equip;
    kke::Equippable m_racketItem;
    int m_supportGrip = -1, m_throatGrip = -1;
    void makeRacketItem();
    int m_pelvis = -1, m_spine[3] = { -1, -1, -1 };
    bool m_sideSteps = false;
    kke::ModelModule::ModelId m_racketModel = 0;
    std::string m_racketTexture;
    glm::mat4 m_racketAlign{1.0f};
    std::unique_ptr<kke::DynamicMeshRenderer> m_block, m_racket;
    kke::PhysicsModule* m_physics = nullptr;
    StringLayout m_strings;
    std::unique_ptr<kke::DynamicMeshRenderer> m_stringsMesh;
    int m_stringBeds = 0;          // parking places handed out so far
    std::vector<int> m_freeBeds;   // ... and given back
};

// What the body does this frame for a stroke (Swing.h). A swing is
// procedural: the shoulders turn, the knees bend and two-bone IK takes the
// racket arm along the stroke's path through the contact point, so the
// racket meets the ball wherever it really is.
struct SwingPose {
    Stroke stroke = Stroke::Ready;
    bool backhand = false;
    float t = kSwingIdle;        // Swing.h's clock
    glm::vec3 contact{0.0f};     // the ball at contact, in the body's frame (x right, y up, z forward)
    bool tossing = false;        // serving: the other hand throws the ball up
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
    void setOutfit(const kke::Outfit& outfit); // dressed (docs/OUTFITS.md), not tinted
    void setVisible(bool visible);
    bool visible() const { return m_visible; }
    // The racket's grip-to-head transform in the world (for drawing it and
    // for the hit's sound); identity without a racket.
    const glm::mat4& racket() const { return m_racketWorld; }
    // The hand holding the ball on a serve (world).
    glm::vec3 tossHand() const { return m_tossHand; }
    // The ball met the strings at `ball` (world) moving at `velocity`
    // (world, m/s): the FEMFX strings take the blow.
    void hitStrings(const glm::vec3& ball, const glm::vec3& velocity);
    float stringDepth() const { return m_bed ? m_bed->depth() : 0.0f; } // m (KKE_TENNIS_STRINGTEST)

    void render(const kke::RenderContext& ctx);
    void renderShadow(const kke::ShadowRenderContext& ctx);

private:
    Rig& m_rig;
    kke::ModelModule& m_models;
    kke::ModelModule::InstanceId m_instance = 0;
    kke::ModelModule::InstanceId m_racketInstance = 0;
    std::unique_ptr<kke::Animator> m_anim;
    kke::Equipment m_equip;                   // this person's: the racket in the right hand
    kke::BodyAvoidState m_avoid[2];           // each arm's way round the body, frame to frame
    struct States { int idle = -1, run = -1, sprint = -1, left = -1, right = -1, back = -1, sit = -1, cheer = -1, groan = -1, clap = -1; } m_st;
    bool m_hasRacket = false;
    bool m_visible = true;
    glm::vec3 m_tint{1.0f};
    std::string m_dressedAs;                  // the outfit m_instance wears ("": the tinted mannequin)
    glm::mat4 m_xf{1.0f};
    glm::mat4 m_racketWorld{1.0f};
    glm::vec3 m_tossHand{0.0f};
    glm::vec3 m_racketVel{0.0f};              // world, m/s: the head's middle
    glm::vec3 m_racketHead{0.0f};             // world, last frame
    std::unique_ptr<StringBed> m_bed;         // made on the first hit
    std::unique_ptr<kke::DynamicMeshRenderer> m_bentStrings;
    std::vector<glm::vec3> m_bentPoints;
    bool m_bent = false;                      // draw m_bentStrings, not the rig's straight ones
    void updateStrings(float dt);
};

} // namespace tennis
