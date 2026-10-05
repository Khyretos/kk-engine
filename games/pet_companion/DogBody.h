#pragma once

#include "kke/Animator.h"
#include "kke/AnimRig.h"
#include "kke/ProceduralAnim.h"
#include "kke/modules/ModelModule.h"

#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kke {
class Application;
class DynamicMeshRenderer;
} // namespace kke

namespace command_kit {
class Scenery;
}

namespace pet_companion {

// A pet you can pick (README.md, "Pets"). The POLYGON Dogs pack holds
// every breed on one skeleton with one set of clips; `material` picks the
// breed's meshes out of it. The pug is Quaternius' (Farm Animals, CC0):
// two clips only, so its legs are procedural (kke::ProceduralGait).
struct Breed {
    std::string id;       // "labrador"
    std::string label;    // "Labrador"
    std::string material; // POLYGON Dogs material prefix ("Labrador"); empty = the pug
    std::string texture;  // POLYGON Dogs texture name part ("Labrador" in PolygonDog_Labrador_02.png)
    float scale = 1.0f;   // on top of the pack's own size
};
const std::vector<Breed>& breeds();
int breedIndex(const std::string& id); // -1 when unknown

// What the dog's body is doing; the brain (AiWorld) and the game pick it.
enum class DogAct {
    Move,    // stand, walk, run by speed
    Sit,
    Sleep,
    Eat,
    Drink,
    Sniff,
    Bark,
    Wag,     // tail wag, standing (or sitting while it sits)
    Beg,
    Shake,   // shakes itself dry, or a toy in its mouth
    Dig,
    Poop,
    Pee,
    Yawn,
    Cower,   // sad, sick
    Joy,     // jumps for joy
    Leap,    // an agility jump: `phase` says where in the arc
    Fall,
};
const char* dogActName(DogAct act);

// The pet's body: model, clips and the layers on top (feet on the
// ground, head on what it looks at). Moving it is the game's job (a Jolt
// character); this only shows it.
class DogBody {
public:
    // Ground under a world point: hit point and normal. False = none.
    using Ground = std::function<bool(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal)>;

    DogBody(kke::Application& app, kke::ModelModule& models, command_kit::Scenery& scenery);
    ~DogBody();
    DogBody(const DogBody&) = delete;
    DogBody& operator=(const DogBody&) = delete;

    // Which breeds this machine has the art for (POLYGON Dogs, the pug).
    bool available(int breed) const;
    // Shows this breed with coat 0..2 (the pack's _01.._03 textures).
    // False: no art for it; the dog is a block.
    bool load(int breed, int coat);
    int breed() const { return m_breed; }
    int coat() const { return m_coat; }
    bool isBlock() const { return m_instance == 0; }
    std::string label() const;

    // Sizes and natural speeds (the clips' own pace), metres and m/s.
    float height() const { return m_height; }   // to the top of the head
    float length() const { return m_length; }   // nose to tail root
    float walkSpeed() const { return m_walk; }
    float runSpeed() const { return m_run; }

    struct Frame {
        glm::vec3 feet{0.0f};
        float yaw = 0.0f;          // AiWorld's: 0 = +Z, 90 = +X
        glm::vec3 velocity{0.0f};  // world, m/s
        float turnRate = 0.0f;     // degrees per second
        DogAct act = DogAct::Move;
        float phase = 0.0f;        // Leap: 0..1 through the jump
        bool hasLook = false;
        glm::vec3 look{0.0f};      // world
        float pitch = 0.0f;        // degrees, nose up (walking up a ramp)
    };
    void update(const Frame& f, const Ground& ground, float dt);
    DogAct act() const { return m_act; }
    // A one-shot act (bark, shake, joy, poop) has played through.
    bool actDone() const;
    // Seconds the current act has been playing.
    float actTime() const { return m_actTime; }

    // World points, after update(): where a toy sits in its mouth, and the
    // top of its head (where a hand pats).
    glm::vec3 mouth() const;
    glm::mat4 mouthFrame() const; // +Z along the jaw, +Y up
    glm::vec3 headTop() const;

    void render(const kke::RenderContext& ctx);
    void renderShadow(const kke::ShadowRenderContext& ctx);

private:
    struct Look {
        kke::ModelModule::ModelId model = 0;
        float walk = 1.3f, run = 5.5f;        // the clips' pace at scale 1
    };
    bool loadSynty(const Breed& b);
    bool loadPug();
    void setUpRig(const kke::ModelData& d);
    void play(DogAct act, float fade);
    void animateSynty(const Frame& f, const Ground& ground, float dt, kke::Pose& pose);
    void animatePug(const Frame& f, float dt, kke::Pose& pose);
    glm::mat4 modelMatrix(const Frame& f) const;

    kke::Application& m_app;
    kke::ModelModule& m_models;
    command_kit::Scenery& m_scenery;
    std::vector<Look> m_looks; // by breed, loaded once each
    int m_breed = -1, m_coat = 0;
    kke::ModelModule::InstanceId m_instance = 0;
    kke::ModelData m_rig; // bones and clips of the model shown
    std::unique_ptr<kke::AnimationSet> m_set;
    std::unique_ptr<kke::Animator> m_anim;
    bool m_synty = false;
    float m_scale = 1.0f, m_yawOffset = 0.0f, m_height = 0.6f, m_length = 0.8f;
    float m_walk = 1.3f, m_run = 5.5f;

    // Animator states.
    int m_move = -1;
    std::vector<int> m_state; // by DogAct (and the sitting / sleeping variants below)
    int m_toSit = -1, m_fromSit = -1, m_toSleep = -1, m_fromSleep = -1, m_sitIdle = -1, m_sleepIdle = -1;
    int m_sitBark = -1, m_sitWag = -1, m_sitBeg = -1, m_sitYawn = -1;
    bool m_sitClipOnce = false; // the sitting clip playing is a one-off
    int m_lean[2] = { -1, -1 }; // running round a bend: left, right
    DogAct m_act = DogAct::Move;
    float m_actTime = 0.0f;
    enum class Posture { Stand, Sitting, Sit, Rising, Lying, Lie, Waking } m_posture = Posture::Stand;

    // Layers.
    kke::LegPlacer m_legs;
    kke::LookAt m_look;
    int m_head = -1, m_jaw = -1, m_snout = -1, m_hips = -1;
    // The pug: procedural legs and sit.
    std::vector<kke::TwoBoneChain> m_legChains;
    kke::ProceduralGait m_gait;
    bool m_gaitReady = false;
    std::vector<int> m_upLegs, m_lowLegs;
    float m_sit = 0.0f;

    glm::mat4 m_world{1.0f};          // the model's transform this frame
    std::vector<glm::mat4> m_bones;   // model space, this frame
    std::unique_ptr<kke::DynamicMeshRenderer> m_block;
    glm::mat4 m_blockXf{1.0f};
};

} // namespace pet_companion
