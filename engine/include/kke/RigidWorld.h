#pragma once

#include "kke/Cloth.h"
#include "kke/Hair.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace kke {

struct RagdollDesc;
struct RagdollDrive;

// Rigid bodies, world collision and the character controller, on Jolt
// Physics (MIT; Horizon Forbidden West, Godot 4). Plain C++, no GPU, so
// unit tests and tools/physics_lab can drive it directly;
// RigidBodyModule is the engine module around it.
//
// Why a second physics engine: FEMFX (PhysicsModule) simulates deformable,
// breakable objects and costs ~0.15-0.2 ms per awake body per step on one
// core; a level's static collision, hundreds of rocks and debris pieces,
// and the players themselves need a rigid-body engine that costs a few
// microseconds per body (docs/SCALING.md). FEMFX stays for the hero objects.
class RigidWorld {
public:
    using BodyId = uint32_t;
    using CharacterId = uint32_t;
    static constexpr BodyId kNoBody = 0xffffffffu;

    enum class Motion : uint8_t { Static, Kinematic, Dynamic };
    enum class Shape : uint8_t { Box, Sphere, Capsule, ConvexHull, Mesh };

    struct BodyDesc {
        Shape shape = Shape::Box;
        glm::vec3 halfExtents{0.5f};         // Box
        float radius = 0.5f;                 // Sphere, Capsule
        float halfHeight = 0.5f;             // Capsule: half the cylinder part (along Y)
        std::vector<glm::vec3> points;       // ConvexHull points / Mesh vertices (body space)
        std::vector<uint32_t> indices;       // Mesh triangles (Static/Kinematic only)
        Motion motion = Motion::Dynamic;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
        float density = 1000.0f;             // kg/m^3 (mass from the shape's volume)
        float mass = 0.0f;                   // kg; > 0 overrides density (inertia still from the shape)
        float friction = 0.6f, restitution = 0.1f;
        uint32_t material = 0;               // game-defined id, reported in contacts (sounds, effects)
        bool clothOnly = false;              // seen only by cloth: a moving mannequin's arms, a cape's body proxy
    };

    struct Settings {
        int threads = -1;                    // worker threads; -1 = cores - 1 (at least 1), 0 = none
        uint32_t maxBodies = 65536;
        glm::vec3 gravity{0.0f, -9.81f, 0.0f};
        float contactReportSpeed = 0.5f;     // m/s: slower impacts aren't reported
        // While Full cloth is being kept from going through cloth, each
        // physics step is cut in this many so the protection runs between
        // the cloth solver's sub-steps (docs/CLOTH.md). 1 = never: cheaper,
        // but layers pressed together by a solid work through each other.
        int clothSubsteps = 6;
    };

    struct RayHit {
        bool hit = false;
        BodyId body = kNoBody;
        glm::vec3 point{0.0f}, normal{0.0f, 1.0f, 0.0f};
        float distance = 0.0f;
        uint32_t material = 0;               // BodyDesc::material of the body hit (audio occlusion)
    };

    // A new contact between two bodies (or a body and a character's
    // push): for impact sounds, particles, damage.
    struct Contact {
        BodyId a = kNoBody, b = kNoBody;
        glm::vec3 point{0.0f}, normal{0.0f};
        float speed = 0.0f;                  // approach speed along the normal, m/s
        uint32_t materialA = 0, materialB = 0;
    };

    struct CharacterDesc {
        float radius = 0.3f;
        float height = 1.8f;                 // total, feet to head
        float maxSlopeDegrees = 50.0f;       // steeper = a wall
        float stepUp = 0.35f;                // stairs
        float mass = 70.0f;
        float pushStrength = 400.0f;         // N: how hard it can push dynamic bodies
        glm::vec3 position{0.0f};            // feet
    };
    struct CharacterInput {
        glm::vec3 move{0.0f};                // desired horizontal velocity, m/s (y ignored)
        bool jump = false;
        float jumpSpeed = 5.0f;
        // In the air, how fast the horizontal velocity blends toward `move`
        // (per second). The default steers a little; a very large value
        // hands air control to the caller, who then sends the exact
        // horizontal velocity it wants (kke::Locomotion does).
        float airSteer = 10.0f;
    };

    RigidWorld();
    explicit RigidWorld(const Settings& settings);
    ~RigidWorld();
    RigidWorld(const RigidWorld&) = delete;
    RigidWorld& operator=(const RigidWorld&) = delete;

    BodyId add(const BodyDesc& desc);
    void remove(BodyId body);
    size_t bodyCount() const;
    size_t activeBodyCount() const;
    bool isActive(BodyId body) const;

    glm::vec3 position(BodyId body) const;
    glm::quat rotation(BodyId body) const;
    glm::mat4 transform(BodyId body) const;
    glm::vec3 velocity(BodyId body) const;
    void setVelocity(BodyId body, const glm::vec3& v);
    glm::vec3 angularVelocity(BodyId body) const; // rad/s, world axes
    void setAngularVelocity(BodyId body, const glm::vec3& w);
    void addImpulse(BodyId body, const glm::vec3& impulse, const glm::vec3& worldPoint);
    // A velocity change whatever the mass (blasts, punches); wakes it.
    void addVelocity(BodyId body, const glm::vec3& deltaVelocity);
    // Kinematic bodies: move there over the next step (pushes things).
    void moveKinematic(BodyId body, const glm::vec3& position, const glm::quat& rotation, float dt);
    // Dynamic <-> kinematic (a network client shows the server's bodies
    // as kinematic: they push the local player but follow the server).
    // Static bodies can't change.
    void setMotion(BodyId body, Motion motion);
    // Straight there, no sweep (teleport; kinematic bodies after a jump).
    void setTransform(BodyId body, const glm::vec3& position, const glm::quat& rotation);

    // Closest hit, front or back face; the normal faces the ray's origin.
    RayHit raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const;
    // The same, through every body `accept` says no to (e.g. only the
    // static level: a network host checking a player's path,
    // kke/net/WorldMoveCheck.h). Called with the body and its motion.
    RayHit raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
                   const std::function<bool(BodyId, Motion)>& accept) const;

    // A body as an oriented box (exact for boxes, the shape's bounds for
    // spheres, capsules and hulls): what the FEMFX bridge mirrors.
    struct BodyBox {
        BodyId id = 0;
        Motion motion = Motion::Static;
        glm::vec3 center{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 halfExtents{0.5f};
        glm::vec3 velocity{0.0f}, angularVelocity{0.0f};
        float mass = 0.0f; // 0 = static or kinematic (immovable)
    };
    // Bodies whose bounds overlap the world box min..max (triangle-mesh
    // bodies left out: a level mesh isn't a box). Appends to `out`.
    void bodiesInBox(const glm::vec3& min, const glm::vec3& max, std::vector<BodyBox>& out) const;

    // Ragdolls (kke/Ragdoll.h): one box body per RagdollBody, joined by
    // swing-twist joints (a cone and a twist range) and limited hinges.
    // Limbs collide with each other, the world and everything else,
    // except the two bodies of each joint (and pairs that already overlap
    // at creation, which would otherwise fly apart).
    using RagdollId = uint32_t; // 0 = none
    // 0 if the desc is empty or invalid (a joint naming a missing body).
    RagdollId addRagdoll(const RagdollDesc& desc, const glm::vec3& initialVelocity);
    void removeRagdoll(RagdollId id);
    // World transform of each body, in RagdollDesc::bodies order.
    bool ragdollTransforms(RagdollId id, std::vector<glm::mat4>& out) const;
    // Its bodies, in RagdollDesc::bodies order (empty if unknown).
    std::vector<BodyId> ragdollBodies(RagdollId id) const;
    size_t ragdollCount() const;
    // Current hinge angle (degrees, as RagdollJoint measures it) of joint
    // `joint`; 0 for ball joints and unknown ids.
    float ragdollHingeAngle(RagdollId id, int joint) const;
    // Active ragdoll (kke/ProceduralAnim.h): joint motors (position mode,
    // torque scaled by strength; 0 = motor off) toward drive.targets, and
    // the assist body's velocity pulled toward its target. False if the
    // id is unknown or the drive doesn't match the ragdoll.
    bool driveRagdoll(RagdollId id, const RagdollDrive& drive);

    CharacterId addCharacter(const CharacterDesc& desc);
    void removeCharacter(CharacterId id);
    void setCharacterInput(CharacterId id, const CharacterInput& input);
    glm::vec3 characterPosition(CharacterId id) const; // feet
    // Where to draw it: the feet between the last two physics steps, by
    // `alpha` (UpdateContext::alpha). Characters step at the physics rate;
    // drawn at their raw position on a faster screen they stand still one
    // frame and jump the next (a double image while running).
    glm::vec3 characterDrawPosition(CharacterId id, float alpha) const;
    glm::vec3 characterVelocity(CharacterId id) const;
    bool characterOnGround(CharacterId id) const;
    void teleportCharacter(CharacterId id, const glm::vec3& feet);
    // Crouch/stand: swaps the capsule for one `height` tall (feet stay put).
    // Returns false, changing nothing, when there's no room (standing up
    // under a low ceiling).
    bool setCharacterHeight(CharacterId id, float height);
    float characterHeight(CharacterId id) const;
    float characterRadius(CharacterId id) const;
    std::vector<CharacterId> characterIds() const;
    // Scripted moves (vaults, climbs): a kinematic character is not
    // simulated by step(); the caller places it with moveCharacter() along
    // a path it has already checked for room (capsuleFits). Turning it back
    // off hands it to the controller again with `setCharacterVelocity`.
    void setCharacterKinematic(CharacterId id, bool kinematic);
    bool characterKinematic(CharacterId id) const;
    void moveCharacter(CharacterId id, const glm::vec3& feet); // keeps velocity
    void setCharacterVelocity(CharacterId id, const glm::vec3& velocity);
    // Input replay (kke/net/InputReplay.h): a character stepped by its
    // owner, one input at a time, with stepCharacter() instead of by
    // step(), so it can be rewound (characterState) and replayed.
    void setCharacterManual(CharacterId id, bool manual);
    bool characterManual(CharacterId id) const;
    // Advances one character by dt (as step() would), nothing else: no
    // bodies, no simulatedTime(). Kinematic ones only gain the time.
    void stepCharacter(CharacterId id, float dt);
    // Seconds this character has been simulated: step() and
    // stepCharacter() both count. Movement code measures motion against
    // it (a manual character's clock isn't the world's).
    double characterTime(CharacterId id) const;
    // Everything a character is (position, velocity, contacts, input,
    // size, clock), to put it back exactly with setCharacterState().
    struct CharacterState {
        std::string jolt;              // CharacterVirtual::SaveState
        CharacterInput input;
        float height = 0.0f;
        bool kinematic = false;
        double time = 0.0;
    };
    CharacterState characterState(CharacterId id) const;
    void setCharacterState(CharacterId id, const CharacterState& state);
    // Would an upright capsule (feet at `feet`) fit without touching
    // anything? For checking a vault's landing spot or a ledge's top.
    bool capsuleFits(const glm::vec3& feet, float height, float radius) const;

    // Advances characters then bodies by dt (fixed step recommended).
    // Cloth (kke/Cloth.h, docs/CLOTH.md): Jolt soft bodies stepped with
    // everything else, plus the engine's clipping protection. Positions
    // are world space, one per ClothDesc::mesh vertex.
    using ClothId = uint32_t; // 0 = none
    ClothId addCloth(const ClothDesc& desc);
    void removeCloth(ClothId id);
    size_t clothCount() const;             // cloth only (not hair)
    bool clothPositions(ClothId id, std::vector<glm::vec3>& out) const;
    // Skinned or pinned cloth: the joints' world matrices, same order as
    // ClothDesc::bindPose (or one matrix moving every pin).
    void setClothJoints(ClothId id, const std::vector<glm::mat4>& joints);
    void setClothProtection(ClothId id, ClothProtection level);
    ClothProtection clothProtection(ClothId id) const;
    void resetCloth(ClothId id);           // back to the rest pose, at rest
    ClothStats clothStats(ClothId id) const;
    void setWind(const glm::vec3& velocity); // m/s, pushes on every cloth by its fabric's airDrag
    glm::vec3 wind() const;
    double lastClothMs() const;            // the engine's protection pass, last step (Jolt's own cloth solve is in lastStepMs)

    // Hair (kke/Hair.h, docs/HAIR.md): guide strands as Jolt soft bodies,
    // stepped with the cloth and blown by setWind. Positions are world
    // space, hairStrandVertices(style) per guide, guide after guide.
    using HairId = uint32_t; // 0 = none
    HairId addHair(const HairDesc& desc);
    void removeHair(HairId id);
    size_t hairCount() const;
    bool hairPositions(HairId id, std::vector<glm::vec3>& out) const;
    void setHairJoint(HairId id, const glm::mat4& head); // the head's world matrix now (HairDesc::bindPose at rest)
    void resetHair(HairId id);             // back to the rest pose, at rest
    HairStats hairStats(HairId id) const;

    void step(float dt);
    double lastStepMs() const;
    // Seconds simulated by step() so far. Code that runs per rendered frame
    // measures motion against this: between steps nothing has moved.
    double simulatedTime() const;

    // Contacts since the last call.
    std::vector<Contact> takeContacts();

private:
    struct Impl;
    std::unique_ptr<Impl> m;
};

} // namespace kke
