#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// Wheeled vehicles on Jolt's vehicle physics (JPH::VehicleConstraint with
// a WheeledVehicleController: ray-less cylinder casts per wheel, spring
// and damper suspension, tyre friction curves over slip, an engine with a
// torque curve, a gearbox, differentials with limited slip and anti-roll
// bars). RigidWorld::addVehicle makes one; see docs/VEHICLES.md.
//
// Body space: +Z is forward, +Y up, +X is the car's left (right-handed,
// so a car seen from behind has +X on its left). Wheels are listed in
// axle pairs, left wheel first: front-left, front-right, rear-left,
// rear-right for a car.

struct VehicleWheelDesc {
    // Where the suspension is attached (body space). The wheel's centre
    // hangs below it, suspensionMin..suspensionMax further down.
    glm::vec3 position{0.0f};
    float radius = 0.33f;
    float width = 0.25f;
    float suspensionMin = 0.05f;      // m below `position`, fully compressed
    float suspensionMax = 0.3f;       // m, fully extended
    float suspensionFrequency = 1.5f; // Hz: how stiff (1-2 road car, 3+ race car)
    float suspensionDamping = 0.5f;   // 0 = bouncy .. 1 = critically damped
    float maxSteerDegrees = 0.0f;     // 0 = doesn't steer
    float maxBrakeTorque = 1500.0f;   // Nm
    float maxHandBrakeTorque = 0.0f;  // Nm (rear wheels: 4000 locks them for a slide)
    // Tyre friction over slip. longitudinal: peak at ~6% slip, then it
    // falls off (wheelspin, locked brakes); lateral: peak at ~3 degrees
    // of slip angle, then it falls off (a drift). 1 = a road tyre on
    // asphalt; less slides earlier.
    float longitudinalGrip = 1.2f;
    float lateralGrip = 1.0f;
    // Of the peak grip, what's left once the tyre slides fully (a low
    // value keeps a drift going once it starts; 1 = never lets go).
    float slideGrip = 0.8f;
    // The friction circle: a tyre spinning or locked (longitudinal slip)
    // has less to give sideways. The share of sideways grip a fully
    // spinning or locked tyre loses: a handbrake or a burnout steps the
    // rear out. 0 = sideways grip ignores wheelspin.
    float combinedSlipLoss = 0.6f;
};

struct VehicleDesc {
    // The chassis as a box (centre at the body origin + boxOffset) or,
    // when `hull` has points, as their convex hull (body space). Wheels
    // are not part of it: they are the suspension casts.
    glm::vec3 halfExtents{0.9f, 0.4f, 2.1f};
    glm::vec3 boxOffset{0.0f};
    std::vector<glm::vec3> hull;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    float mass = 1300.0f;                 // kg
    // Added to the shape's centre of mass: lower it and the car doesn't
    // roll over in every corner (real cars have the engine low).
    glm::vec3 centerOfMassOffset{0.0f, -0.35f, 0.0f};
    float friction = 0.4f, restitution = 0.1f;
    uint32_t material = 0;                // reported in contacts (RigidWorld::Contact)

    std::vector<VehicleWheelDesc> wheels; // axle pairs, left first (see above)
    // Which axle pairs the engine drives (index = wheel pair: 0 = the
    // first two wheels). Empty: the last pair (rear-wheel drive).
    std::vector<int> drivenAxles;
    float maxTorque = 500.0f;             // Nm, engine
    float minRpm = 1000.0f, maxRpm = 7000.0f;
    std::vector<float> gearRatios{ 2.66f, 1.78f, 1.3f, 1.0f, 0.74f };
    float reverseRatio = -2.9f;
    float differentialRatio = 3.42f;
    // Max / min wheel speed across a driven axle before the slower wheel
    // gets all the torque (1.4 = a limited-slip diff; very large = open).
    float limitedSlipRatio = 1.4f;
    bool manualGearbox = false;           // true: VehicleInput::gear picks the gear
    float shiftUpRpm = 5500.0f, shiftDownRpm = 2500.0f; // automatic gearbox
    float gearSwitchSeconds = 0.3f;
    float antiRollStiffness = 6000.0f;    // N/m per axle pair (0 = none)
};

struct VehicleInput {
    float throttle = 0.0f;  // 0..1 (automatic gearbox: -1..1, below 0 reverses once stopped)
    float brake = 0.0f;     // 0..1
    float steer = 0.0f;     // -1 (left) .. 1 (right)
    float handBrake = 0.0f; // 0..1
    int gear = 1;           // manual gearbox only: -1 reverse, 0 neutral, 1.. forward
};

struct VehicleWheelState {
    // World transform of the wheel's mesh, modelled in the car's own axes
    // and centred on the wheel (as it sits on the car in the art), with
    // its spin, steer and suspension travel. Axle = local X.
    glm::mat4 transform{1.0f};
    bool contact = false;        // touching the ground
    glm::vec3 contactPoint{0.0f};
    uint32_t groundBody = 0xffffffffu; // RigidWorld::BodyId under it (kNoBody in the air)
    float longitudinalSlip = 0.0f; // (wheel speed - ground speed) / ground speed: 0 rolls, -1 locked, >0 wheelspin
    float lateralSlip = 0.0f;      // slip angle, degrees (a drift is 10-40)
    float angularVelocity = 0.0f;  // rad/s
    float suspension = 0.0f;       // 0 = fully extended .. 1 = fully compressed
    float steerDegrees = 0.0f;     // positive = left
};

struct VehicleState {
    float speed = 0.0f;     // m/s along the car's forward axis (negative in reverse)
    float rpm = 0.0f;
    int gear = 0;           // -1 reverse, 0 neutral, 1.. forward
    bool switchingGear = false;
    std::vector<VehicleWheelState> wheels;
};

} // namespace kke
