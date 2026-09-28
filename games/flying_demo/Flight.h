#pragma once

// The flight model of the Flying demo (README.md "How a plane flies"):
// pure maths, no engine, so tests/test_flight.cpp checks it on its own.
//
// A plane is a point with an orientation. Each step:
//   - the controls turn it: pitch, roll and yaw rates, weaker when slow
//     (the control surfaces need air flowing over them);
//   - the air pushes it: lift along its up axis (grows with the speed
//     squared and with the angle of attack, until the wing stalls), drag
//     against the velocity, a side force that stops it sliding sideways;
//   - the propeller pulls it along its nose (the throttle), gravity down;
//     pointed steeply up the pull fades below the plane's weight, so a
//     plane can't hang on its propeller: it slows and tips over;
//   - the nose weathervanes into the airflow a little, so it flies where
//     it points instead of skating.
// That is the classic arcade-sim middle: loops, rolls, stalls and spins
// feel right, but a beginner with a pad can still fly it.
//
// Axes: the plane's forward is -Z, up +Y, right +X (like the engine's
// camera and characters), world Y up, metres and seconds.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>

namespace flying {

struct Controls {
    float pitch = 0.0f;     // -1..1, + = nose up (stick back)
    float roll = 0.0f;      // -1..1, + = right wing down
    float yaw = 0.0f;       // -1..1, + = nose right (rudder)
    float throttle = 0.6f;  // 0..1
    bool brake = false;     // on the ground: wheel brakes
};

struct FlightSettings {
    float maxThrust = 15.0f;       // m/s^2 at full throttle (a light stunt plane)
    float steepThrust = 0.4f;      // of that left pointing straight up (from 55 degrees up): it slows and tips over
    float stallSpeed = 22.0f;      // m/s: below this the wing can't hold the plane up
    float maxSpeed = 88.0f;        // m/s: hard cap (dives)
    float pitchRate = 120.0f;      // deg/s at full stick and full authority
    float rollRate = 240.0f;
    float yawRate = 45.0f;
    float authoritySpeed = 32.0f;  // m/s: the controls bite fully from here
    float stallAngle = 16.0f;      // degrees of attack where lift peaks
    float zeroLiftAngle = -3.0f;   // degrees: a cambered wing lifts a little at 0
    float drag = 0.0019f;          // parasitic, per (m/s)^2
    float inducedDrag = 0.06f;     // per unit of lift acceleration
    float sideForce = 2.5f;        // 1/s: how fast sideways sliding dies
    float weathervane = 2.2f;      // 1/s: how hard the nose turns into the airflow
    float gearHeight = 1.1f;       // m from the plane's origin to the wheels
    float rollingFriction = 0.6f;  // m/s^2 on the ground
    float brakeDecel = 6.0f;       // m/s^2 with the brakes
    float rotateSpeed = 27.0f;     // m/s on the runway: the nose lifts by itself and the plane flies
    float groundSteer = 40.0f;     // deg/s of tail-wheel steering (rudder) on the ground
    float maxTouchdownSink = 6.0f; // m/s down: more is a crash
    float maxTouchdownTilt = 25.0f;// degrees of bank or pitch: more is a crash
};

struct PlaneState {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 velocity{0.0f};
    bool onGround = false;
    bool crashed = false;
    // Last step, for the HUD and the camera:
    float airspeed = 0.0f;         // m/s
    float angleOfAttack = 0.0f;    // degrees
    bool stalled = false;

    glm::vec3 forward() const { return rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
    glm::vec3 up() const { return rotation * glm::vec3(0.0f, 1.0f, 0.0f); }
    glm::vec3 right() const { return rotation * glm::vec3(1.0f, 0.0f, 0.0f); }
};

// Where the ground is: its height under (x, z), and whether a plane may
// land there (a runway, a flat field; not the sea or a mountainside).
struct Ground {
    std::function<float(float x, float z)> height;
    std::function<bool(float x, float z)> landable;
};

// One step (dt up to ~1/30 s; FlightSettings are per second).
void step(PlaneState& s, const Controls& c, const FlightSettings& f, const Ground& g, float dt);

// A plane heading `yawDegrees` (0 = -Z, + = to the right, i.e. clockwise
// seen from above) at `speed` m/s, wings level, at `position`.
PlaneState airborne(const glm::vec3& position, float yawDegrees, float speed);
// A plane standing on the ground at `position` (its wheels there), nose
// at `yawDegrees`.
PlaneState parked(const glm::vec3& position, float yawDegrees, const FlightSettings& f);

// Heading (degrees, 0 = -Z, clockwise from above), pitch (nose above the
// horizon, degrees) and bank (right wing down, degrees) of a rotation.
float headingOf(const glm::quat& q);
float pitchOf(const glm::quat& q);
float bankOf(const glm::quat& q);

// An autopilot's stick (the CPU pilots, README.md "CPU pilots"): the
// controls that turn `s` toward `target`, banking into turns like a
// pilot does, and pulling up when the ground ahead is closer than
// `floor` metres. `skill` 0..1: how hard it flies (rates and throttle).
Controls steerToward(const PlaneState& s, const glm::vec3& target, const Ground& g, float floor, float skill);

} // namespace flying
