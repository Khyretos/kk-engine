#pragma once

#include "kke/RigidWorld.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace kke {

// The movement layer on top of RigidWorld's character controller: what a
// player's inputs *mean* this frame, given where the character is.
//
// Built on the principles from PointDown's controller and parkour series
// (see MOVEMENT.md for the notes and which video each rule comes from):
//
//  - Input actions are not moves. The game sends abstract actions (move,
//    fast, go up, crouch); each state translates them into a move, and the
//    translation depends on the world around the character: "go up" is a
//    vault in front of a hip-high fence, a climb in front of a ledge, and a
//    jump anywhere else.
//  - Area awareness runs once per frame, after input and before any state
//    logic, and is the only place that asks the world questions (ray casts
//    and shape tests). It is dynamic: level designers mark nothing, any
//    box/mesh with a top is vaultable or climbable. Each state asks with its
//    own sensor (a sprint looks further ahead than a walk).
//  - Floor has three tiers, not two: on the floor, a little above it (still
//    walking: snap down, no fall animation on a kerb), and in the air.
//  - Speed and direction are separate. Turning rotates the direction at a
//    limited rate and slows down in sharp turns instead of flipping the
//    velocity in one frame; a 180 turn keeps the side it started on.
//  - In the air you control a small acceleration, not the velocity, and
//    the body faces where you meant to go, not where a wall deflected you.
//  - Scripted traversal (vault, climb) moves the capsule along a path that
//    was checked for room before it started, turns the body square to the
//    obstacle during a short correction window, and hands the momentum
//    back at the end. Animations are pose providers only: no root motion
//    decides where the capsule goes.
//
// Pure logic on RigidWorld (no GPU), unit-tested in tests/test_locomotion.cpp.
class Locomotion {
public:
    enum class State : uint8_t { Ground, Air, Vault, Climb };

    // What the game asks for this frame (from InputMap actions).
    struct Input {
        glm::vec3 move{0.0f};   // world-space wish direction, length 0..1 (y ignored)
        bool fast = false;      // sprint
        bool slow = false;      // walk
        bool crouch = false;    // crouched (the caller resizes the capsule)
        bool goUp = false;      // pressed this frame: jump / vault / climb
    };

    // A state's "ray slice": how far ahead it looks and how high a top it
    // can reach (from the feet).
    struct Sensor {
        float reach = 0.7f;
        float maxHeight = 1.9f;
    };

    struct Settings {
        // Ground speeds (m/s), matched to the animations' foot speeds.
        float walkSpeed = 1.6f, runSpeed = 3.6f, sprintSpeed = 6.2f, crouchSpeed = 1.4f;
        float acceleration = 16.0f, deceleration = 20.0f;   // m/s^2
        // Turning (degrees per second). Sharper than turnSlowAngle drops
        // to turnSpeedFactor of the speed while the turn lasts.
        float turnRate = 600.0f, sprintTurnRate = 320.0f;
        float turnSlowAngle = 50.0f, turnSpeedFactor = 0.6f;
        float reverseHysteresis = 12.0f;                     // degrees around 180
        // Jumping and the air.
        float jumpSpeed = 5.2f;
        float airAcceleration = 5.0f;                        // m/s^2 of steering
        float airSpeedMin = 1.6f;                            // steering cap for slow jumps
        float airTurnRate = 240.0f;
        float coyoteTime = 0.12f, jumpBuffer = 0.15f;
        float groundSnap = 0.25f;                            // the middle floor tier
        // Traversal.
        float radius = 0.3f, height = 1.8f;                  // the capsule
        Sensor walkSensor{ 0.7f, 1.9f }, sprintSensor{ 1.6f, 2.2f }, airSensor{ 0.5f, 2.1f };
        float stepHeight = 0.4f;                             // lower = the controller steps it
        float vaultMaxHeight = 1.3f, vaultMaxDepth = 1.1f, vaultClearance = 0.15f;
        float vaultMinTime = 0.35f, vaultMaxTime = 0.8f;
        float climbTime = 0.85f;
        float correctionTime = 0.2f;                         // squaring up to the obstacle
    };

    // What area awareness found in one direction.
    struct Obstacle {
        enum class Kind : uint8_t { None, Vault, Climb };
        Kind kind = Kind::None;
        glm::vec3 face{0.0f};    // hit point on the front face
        glm::vec3 normal{0.0f};  // the face's normal (points back at the character, horizontal)
        float height = 0.0f;     // top above the feet
        float depth = 0.0f;      // front to back across the top (large = a platform)
        glm::vec3 target{0.0f};  // feet at the end of the move
        const char* why = "";    // why it's None (debug panel, tests)
    };

    Locomotion(RigidWorld& world, RigidWorld::CharacterId id);
    Locomotion(RigidWorld& world, RigidWorld::CharacterId id, const Settings& settings);

    // Call once per frame before RigidWorld::step().
    void update(const Input& input, float dt);
    // Respawn: cancels any vault/climb and puts the feet there, standing still.
    void teleport(const glm::vec3& feet);

    State state() const { return m_state; }
    float stateTime() const { return m_stateTime; }
    // 0..1 through a vault or climb.
    float traversalProgress() const;
    // Measured horizontal speed: drive the blend space with this, so the
    // legs match the ground, including in turns and against walls.
    float groundSpeed() const { return m_measuredSpeed; }
    glm::vec3 facing() const { return m_facing; }
    float facingYaw() const;                   // degrees, 0 = -Z (the camera rig's convention)
    void setFacing(const glm::vec3& dir);      // first person: face the camera
    // One-frame events for animation and sound.
    bool jumped() const { return m_jumped; }
    bool landed() const { return m_landed; }
    float fallHeight() const { return m_fallHeight; }
    const Obstacle& lastObstacle() const { return m_obstacle; }

    // Area awareness, one direction, with a given sensor (public for tests
    // and debug drawing).
    Obstacle probe(const glm::vec3& direction, const Sensor& sensor) const;

    Settings& settings() { return m_settings; }

    // y = a x^2 + b x through (0,0) and (xl, yl), peaking `overshoot`
    // above yl (PointDown's ledge-leap parabola). Returns (a, b); the peak
    // lies between the two points.
    static glm::vec2 leapParabola(float xl, float yl, float overshoot);
    // Signed angle from one yaw to another in degrees (-180..180].
    static float angleBetween(float fromYaw, float toYaw);

private:
    void updateGround(const Input& in, float dt, bool grounded);
    void updateAir(const Input& in, float dt, bool grounded);
    void updateTraversal(float dt);
    bool tryTraversal(const Input& in, const Sensor& sensor, bool inAir);
    void startTraversal(State s, const Obstacle& o);
    void enter(State s);
    void jump(const Input& in);
    float targetSpeed(const Input& in) const;

    RigidWorld& m_world;
    RigidWorld::CharacterId m_id;
    Settings m_settings;
    State m_state = State::Ground;
    float m_stateTime = 0.0f;

    glm::vec3 m_moveDir{0.0f, 0.0f, -1.0f}; // direction of travel (unit, horizontal)
    float m_speed = 0.0f;                   // commanded ground speed
    float m_measuredSpeed = 0.0f;
    glm::vec3 m_lastFeet{0.0f};
    bool m_haveLastFeet = false;
    float m_lastTurnSign = 1.0f;
    glm::vec3 m_facing{0.0f, 0.0f, -1.0f};

    float m_sinceGrounded = 0.0f, m_buffer = 0.0f;
    bool m_jumpedFromGround = false;
    float m_airEntrySpeed = 0.0f, m_airPeak = 0.0f;
    bool m_jumped = false, m_landed = false;
    float m_fallHeight = 0.0f;

    // Traversal in progress.
    Obstacle m_obstacle;
    glm::vec3 m_start{0.0f}, m_wallPoint{0.0f};
    glm::vec3 m_startFacing{0.0f, 0.0f, -1.0f};
    glm::vec2 m_arc{0.0f};                  // vault parabola (a, b)
    float m_duration = 0.0f, m_exitSpeed = 0.0f;
};

} // namespace kke
