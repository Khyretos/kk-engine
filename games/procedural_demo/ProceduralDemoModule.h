#pragma once

#include "kke/Module.h"
#include "kke/ProceduralAnim.h"
#include "kke/Ragdoll.h"

#include <glm/glm.hpp>

#include <memory>
#include <random>
#include <string>
#include <vector>

namespace kke {
class DynamicMeshRenderer;
class OrbitCameraModule;
class RigidBodyModule;
struct Vertex;
} // namespace kke

namespace procedural_demo {

// Procedural animation demo (docs/PROCEDURAL_ANIMATION.md): a spider, a
// beetle, a dog and a person walk over hills and a flight of steps with
// no animation clips at all. Every creature is a generated skeleton
// driven by kke::ProceduralGait + kke::applyGait, looks at the camera
// with kke::LookAt, and is drawn from its bones with simple shapes. The
// dog wags its tail with FABRIK and runs through walk, trot and gallop
// by itself; the person swings its arms against its legs.
//
// Click the ground: everyone comes. Click the dog or the person: a hit
// (kke::ActiveRagdoll on Jolt joint motors): a light one staggers them,
// Shift+click knocks them down and they get up again. Click a bug and it
// runs away. 1 / 2 / 3: the dog walks, trots, gallops; 0: it picks.
//
// Headless / screenshot switches: KKE_PROC_VIEW=yaw,pitch,distance
// (degrees, degrees, metres), KKE_PROC_FOCUS=spider|beetle|dog|person
// (the camera follows it), KKE_PROC_HIT=<seconds> (the focused dog or
// person, else the person, is hit from the side then;
// KKE_PROC_HIT_SPEED=<m/s>, default 3), KKE_PROC_GAIT=walk|trot|gallop
// (the dog), KKE_PROC_QUIT=<seconds> (quit after that long, logging each
// creature's gait and state every two seconds), KKE_PROC_TRACE=1 (log a
// hit body's balance and lean every frame).
class ProceduralDemoModule : public kke::Module {
public:
    ProceduralDemoModule();
    ~ProceduralDemoModule() override;
    const char* name() const override { return "ProceduralDemo"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;
    void update(const kke::UpdateContext& ctx) override;
    void render(const kke::RenderContext& ctx) override;
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void renderUi() override;
    void onEvent(const SDL_Event& event) override;

private:
    enum class Kind { Spider, Beetle, Dog, Person };
    // A shape drawn between (or at) bones.
    struct Part {
        int a = -1, b = -1;      // capsule from a to b; b < 0 = ellipsoid at a
        float radius = 0.05f;
        glm::vec3 scale{1.0f};   // ellipsoid radii (bone space)
        glm::vec3 offset{0.0f};  // ellipsoid centre (bone space)
        glm::vec3 color{0.5f};
    };
    struct Creature {
        Kind kind = Kind::Dog;
        std::string name;
        kke::ModelData rig;
        kke::Pose rest;
        std::vector<kke::TwoBoneChain> legs;
        int body = -1;           // the bone the gait sways (pelvis, hips, thorax)
        std::vector<Part> parts;
        kke::ProceduralGait gait;
        kke::LookAt look;
        kke::IkChain tail;
        int armL = -1, armR = -1; // person: upper arms, swung against the legs
        // The controller: a point on the ground with a heading.
        glm::vec3 position{0.0f};
        float yaw = 0.0f;         // degrees; forward = (sin, 0, cos)
        float speed = 0.0f, turnRate = 0.0f;
        float cruise = 1.0f;      // m/s it likes to move at
        float maxTurn = 180.0f;   // degrees/s
        glm::vec3 goal{0.0f};
        float wander = 0.0f;      // seconds until it picks a new spot
        float flee = 0.0f;        // bugs: seconds of running away
        float time = 0.0f;
        // Physical reactions (dog and person).
        bool canRagdoll = false;
        float mass = 70.0f;
        kke::ActiveRagdoll active;
        kke::RagdollDesc ragdoll;
        kke::RagdollSkinBinding binding;
        uint32_t handle = 0;
        // What gets drawn: bone transforms in world space.
        std::vector<glm::mat4> world;
    };

    void buildGround();
    float groundHeight(float x, float z) const;
    bool ground(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal) const;

    Creature makeSpider(int legs, const std::string& name, float size) const;
    Creature makeDog() const;
    Creature makePerson() const;
    void place(Creature& c, const glm::vec3& at, float yaw);

    void think(Creature& c, float dt);
    void move(Creature& c, float dt);
    void animate(Creature& c, float dt);
    kke::Pose animatedPose(Creature& c, float dt, const glm::mat4& modelWorld);
    void hit(Creature& c, const glm::vec3& point, const glm::vec3& push);
    void updatePhysical(Creature& c, float dt);

    void click(float mouseX, float mouseY, bool hard);
    void appendCreature(const Creature& c, std::vector<kke::Vertex>& v, std::vector<uint32_t>& idx) const;

    kke::Application* m_app = nullptr;
    kke::RigidBodyModule* m_rigid = nullptr;
    kke::OrbitCameraModule* m_orbit = nullptr;
    std::unique_ptr<kke::DynamicMeshRenderer> m_groundMesh, m_creatureMesh;
    std::vector<Creature> m_creatures;
    std::mt19937 m_rng{7};
    glm::mat4 m_view{1.0f}, m_proj{1.0f};
    glm::vec3 m_flag{0.0f};
    float m_flagTime = -1.0f;
    kke::Gait m_dogGait = kke::Gait::Auto;
    int m_focus = -1;
    float m_time = 0.0f, m_quitAfter = -1.0f, m_hitAt = -1.0f, m_hitSpeed = 3.0f, m_logTimer = 0.0f;
    bool m_hitDone = false, m_trace = false;
};

} // namespace procedural_demo
