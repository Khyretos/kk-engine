#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace kke {

// Rigid bodies, world collision and the character controller, on Jolt
// Physics (MIT; Horizon Forbidden West, Godot 4). Plain C++, no GPU, so
// unit tests and tools/physics_lab can drive it directly;
// RigidBodyModule is the engine module around it.
//
// Why a second physics engine: FEMFX (PhysicsModule) simulates deformable,
// breakable objects and costs ~0.15-0.2 ms per awake body per step on one
// core; a level's static collision, hundreds of rocks and debris pieces,
// and the players themselves need a rigid-body engine that costs a few
// microseconds per body (SCALING.md). FEMFX stays for the hero objects.
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
        float friction = 0.6f, restitution = 0.1f;
        uint32_t material = 0;               // game-defined id, reported in contacts (sounds, effects)
    };

    struct Settings {
        int threads = -1;                    // worker threads; -1 = cores - 1 (at least 1), 0 = none
        uint32_t maxBodies = 65536;
        glm::vec3 gravity{0.0f, -9.81f, 0.0f};
        float contactReportSpeed = 0.5f;     // m/s: slower impacts aren't reported
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

    CharacterId addCharacter(const CharacterDesc& desc);
    void removeCharacter(CharacterId id);
    void setCharacterInput(CharacterId id, const CharacterInput& input);
    glm::vec3 characterPosition(CharacterId id) const; // feet
    glm::vec3 characterVelocity(CharacterId id) const;
    bool characterOnGround(CharacterId id) const;
    void teleportCharacter(CharacterId id, const glm::vec3& feet);
    // Crouch/stand: swaps the capsule for one `height` tall (feet stay put).
    // Returns false, changing nothing, when there's no room (standing up
    // under a low ceiling).
    bool setCharacterHeight(CharacterId id, float height);
    float characterHeight(CharacterId id) const;
    // Scripted moves (vaults, climbs): a kinematic character is not
    // simulated by step(); the caller places it with moveCharacter() along
    // a path it has already checked for room (capsuleFits). Turning it back
    // off hands it to the controller again with `setCharacterVelocity`.
    void setCharacterKinematic(CharacterId id, bool kinematic);
    bool characterKinematic(CharacterId id) const;
    void moveCharacter(CharacterId id, const glm::vec3& feet); // keeps velocity
    void setCharacterVelocity(CharacterId id, const glm::vec3& velocity);
    // Would an upright capsule (feet at `feet`) fit without touching
    // anything? For checking a vault's landing spot or a ledge's top.
    bool capsuleFits(const glm::vec3& feet, float height, float radius) const;

    // Advances characters then bodies by dt (fixed step recommended).
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
