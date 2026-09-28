#include "Flight.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace flying {

namespace {
constexpr float kGravity = 9.81f;

float wrap180(float deg) {
    deg = std::fmod(deg + 180.0f, 360.0f);
    if (deg < 0.0f) deg += 360.0f;
    return deg - 180.0f;
}

glm::quat fromAngles(float heading, float pitch, float bank) {
    return glm::angleAxis(glm::radians(-heading), glm::vec3(0, 1, 0)) * glm::angleAxis(glm::radians(pitch), glm::vec3(1, 0, 0)) *
           glm::angleAxis(glm::radians(-bank), glm::vec3(0, 0, 1));
}

// Lift for an angle of attack, as a fraction of what holds the plane up
// at the stall speed: a straight line through the zero-lift angle up to
// the stall, then it falls away (the wing stalls) to half.
float liftFraction(float aoa, const FlightSettings& f, bool& stalled) {
    const float span = f.stallAngle - f.zeroLiftAngle;
    stalled = std::abs(aoa) > f.stallAngle;
    if (aoa > f.stallAngle) return std::max(0.5f, 1.0f - 0.5f * (aoa - f.stallAngle) / 10.0f);
    if (aoa < -f.stallAngle) return -std::max(0.5f, 1.0f - 0.5f * (-aoa - f.stallAngle) / 10.0f) * 0.8f;
    return std::clamp((aoa - f.zeroLiftAngle) / span, -0.8f, 1.0f);
}
} // namespace

float headingOf(const glm::quat& q) {
    const glm::vec3 fwd = q * glm::vec3(0, 0, -1);
    return glm::degrees(std::atan2(fwd.x, -fwd.z));
}

float pitchOf(const glm::quat& q) {
    const glm::vec3 fwd = q * glm::vec3(0, 0, -1);
    return glm::degrees(std::asin(std::clamp(fwd.y, -1.0f, 1.0f)));
}

float bankOf(const glm::quat& q) {
    const glm::vec3 right = q * glm::vec3(1, 0, 0), up = q * glm::vec3(0, 1, 0);
    return glm::degrees(std::atan2(-right.y, up.y));
}

PlaneState airborne(const glm::vec3& position, float yawDegrees, float speed) {
    PlaneState s;
    s.position = position;
    s.rotation = fromAngles(yawDegrees, 0.0f, 0.0f);
    s.velocity = s.forward() * speed;
    s.airspeed = speed;
    return s;
}

PlaneState parked(const glm::vec3& position, float yawDegrees, const FlightSettings& f) {
    PlaneState s;
    s.position = position + glm::vec3(0.0f, f.gearHeight, 0.0f);
    s.rotation = fromAngles(yawDegrees, 0.0f, 0.0f);
    s.onGround = true;
    return s;
}

void step(PlaneState& s, const Controls& c, const FlightSettings& f, const Ground& g, float dt) {
    if (s.crashed || dt <= 0.0f) return;
    const glm::vec3 fwd = s.forward(), up = s.up(), right = s.right();
    const float speed = glm::length(s.velocity);
    const float vf = glm::dot(s.velocity, fwd), vu = glm::dot(s.velocity, up), vr = glm::dot(s.velocity, right);
    // Angle of attack: the airflow meets the wing from below the nose.
    const float aoa = speed > 1.0f ? glm::degrees(std::atan2(-vu, std::max(vf, 0.5f))) : 0.0f;
    const float slip = speed > 1.0f ? glm::degrees(std::atan2(vr, std::max(vf, 0.5f))) : 0.0f;
    bool stalled = false;
    const float lift = liftFraction(aoa, f, stalled) * (vf * std::abs(vf)) / (f.stallSpeed * f.stallSpeed);
    s.airspeed = speed;
    s.angleOfAttack = aoa;
    s.stalled = stalled && !s.onGround && speed > 3.0f;

    // Turning: control surfaces work in moving air, so slow means sluggish.
    const float authority = std::clamp(speed / f.authoritySpeed, 0.08f, 1.0f);
    const float airflow = std::clamp(speed / f.authoritySpeed, 0.0f, 1.0f);
    const float pitch = std::clamp(c.pitch, -1.0f, 1.0f), roll = std::clamp(c.roll, -1.0f, 1.0f), yaw = std::clamp(c.yaw, -1.0f, 1.0f);
    // The nose follows the airflow (a weathervane), relative to the half
    // degree of attack the plane is trimmed for; a stalled wing drops it.
    float pitchRate = pitch * f.pitchRate * authority - f.weathervane * (aoa - 0.5f) * airflow * (s.stalled ? 2.5f : 1.0f);
    float yawRate = yaw * f.yawRate * authority + f.weathervane * slip * airflow;
    float rollRate = roll * f.rollRate * authority;
    if (s.onGround) {
        rollRate = 0.0f;
        yawRate = yaw * f.groundSteer * std::clamp(speed / 6.0f, 0.3f, 1.0f) + yaw * f.yawRate * authority * 0.5f;
        pitchRate = std::max(pitchRate, pitch * f.pitchRate * authority); // the weathervane doesn't pin the tail down
    }
    const glm::vec3 turn(glm::radians(pitchRate), glm::radians(-yawRate), glm::radians(-rollRate));
    const float angle = glm::length(turn) * dt;
    if (angle > 1e-7f) s.rotation = glm::normalize(s.rotation * glm::angleAxis(angle, turn / glm::length(turn)));

    // Forces (as accelerations).
    glm::vec3 acc(0.0f, -kGravity, 0.0f);
    acc += fwd * (std::clamp(c.throttle, 0.0f, 1.0f) * f.maxThrust * (1.0f - 0.5f * std::min(speed / f.maxSpeed, 1.0f)));
    const float liftAcc = lift * kGravity;
    acc += up * liftAcc;
    if (speed > 0.01f) acc -= (s.velocity / speed) * (f.drag * speed * speed + f.inducedDrag * std::abs(liftAcc));
    acc -= right * (vr * f.sideForce);
    s.velocity += acc * dt;
    const float newSpeed = glm::length(s.velocity);
    if (newSpeed > f.maxSpeed) s.velocity *= f.maxSpeed / newSpeed;
    s.position += s.velocity * dt;

    // The ground.
    const float groundY = g.height ? g.height(s.position.x, s.position.z) : -1e9f;
    const float wheels = s.position.y - f.gearHeight;
    if (s.onGround) {
        if (wheels > groundY - 0.05f && s.velocity.y > 0.5f) {
            s.onGround = false; // the wing lifts more than the plane weighs: flying
            return;
        }
        s.position.y = groundY + f.gearHeight;
        s.velocity.y = std::max(s.velocity.y, 0.0f);
        // Rolling: friction, the brakes, and the wheels go where the nose points.
        glm::vec3 flat(s.velocity.x, 0.0f, s.velocity.z);
        const float rolling = glm::length(flat);
        const float slow = (f.rollingFriction + (c.brake ? f.brakeDecel : 0.0f)) * dt;
        glm::vec3 nose(fwd.x, 0.0f, fwd.z);
        if (glm::length(nose) > 1e-4f) nose = glm::normalize(nose);
        const float along = std::max(0.0f, glm::dot(flat, nose) - slow);
        const float keep = rolling > 1e-4f ? along : 0.0f;
        s.velocity.x = nose.x * keep;
        s.velocity.z = nose.z * keep;
        // Wings level on the wheels; the nose from level to a take-off pitch.
        s.rotation = fromAngles(headingOf(s.rotation), std::clamp(pitchOf(s.rotation), 0.0f, 14.0f), 0.0f);
        if (g.landable && !g.landable(s.position.x, s.position.z) && keep > 8.0f) s.crashed = true; // rolled off the runway into rough ground fast
        return;
    }
    if (wheels < groundY) {
        const float sink = -s.velocity.y;
        const float bank = std::abs(bankOf(s.rotation)), nose = pitchOf(s.rotation);
        const bool landable = !g.landable || g.landable(s.position.x, s.position.z);
        if (landable && sink < f.maxTouchdownSink && bank < f.maxTouchdownTilt && nose > -8.0f && nose < f.maxTouchdownTilt) {
            s.onGround = true;
            s.position.y = groundY + f.gearHeight;
            s.velocity.y = 0.0f;
            s.rotation = fromAngles(headingOf(s.rotation), std::clamp(nose, 0.0f, 14.0f), 0.0f);
        } else {
            s.crashed = true;
            s.position.y = std::max(s.position.y, groundY + 0.3f);
        }
    }
}

Controls steerToward(const PlaneState& s, const glm::vec3& target, const Ground& g, float floor, float skill) {
    Controls c;
    skill = std::clamp(skill, 0.0f, 1.0f);
    const float heading = headingOf(s.rotation), bank = bankOf(s.rotation);
    glm::vec3 to = target - s.position;
    // Ground ahead: climb first, whatever the target says.
    const float speed = std::max(glm::length(s.velocity), 1.0f);
    // A steady speed: fast enough to climb, slow enough to turn tightly.
    const float cruise = 50.0f + 12.0f * skill;
    c.throttle = std::clamp(0.55f + (cruise - speed) * 0.08f, 0.0f, 1.0f);
    const glm::vec3 ahead = s.position + s.velocity * 2.5f;
    float clearance = s.position.y;
    if (g.height) clearance = std::min(s.position.y - g.height(s.position.x, s.position.z), ahead.y - g.height(ahead.x, ahead.z));
    // A target low over the ground (a valley ring) lowers the floor, never below 15 m.
    if (g.height) floor = std::min(floor, std::max(15.0f, (target.y - g.height(target.x, target.z)) * 0.6f));
    const bool pullUp = clearance < floor;
    if (pullUp) to = glm::vec3(s.velocity.x, 0.0f, s.velocity.z) + glm::vec3(0.0f, speed * 0.7f, 0.0f);
    const float want = glm::degrees(std::atan2(to.x, -to.z));
    const float headingErr = wrap180(want - heading);
    const float horizontal = std::sqrt(to.x * to.x + to.z * to.z);
    const float climbWant = std::clamp(glm::degrees(std::atan2(to.y, std::max(horizontal, 1.0f))), -30.0f, pullUp ? 35.0f : 30.0f);
    const float path = glm::degrees(std::asin(std::clamp(s.velocity.y / speed, -1.0f, 1.0f)));
    // Bank into the turn (more for a bigger turn), then pull: the lift,
    // tilted, turns the plane. Level the wings as the nose comes round.
    const float maxBank = 55.0f + 20.0f * skill;
    const float bankWant = pullUp ? 0.0f : std::clamp(headingErr * 1.6f, -maxBank, maxBank);
    c.roll = std::clamp(wrap180(bankWant - bank) / 35.0f, -1.0f, 1.0f) * (0.6f + 0.4f * skill);
    const float cosBank = std::cos(glm::radians(bank));
    const float turnPull = std::clamp(std::abs(headingErr) / 50.0f, 0.0f, 1.0f) * std::max(0.0f, cosBank) *
                           (std::abs(wrap180(bankWant - bank)) < 30.0f ? 1.0f : 0.25f);
    const float climbPull = std::clamp((climbWant - path) / 7.0f, -1.0f, 1.0f) * cosBank;
    c.pitch = std::clamp(turnPull * (0.55f + 0.35f * skill) + climbPull * 0.8f, -0.8f, 1.0f);
    // Upside down: roll upright before anything else.
    if (s.up().y < 0.0f) {
        c.pitch = 0.0f;
        c.roll = bank > 0.0f ? -1.0f : 1.0f;
    }
    c.yaw = std::clamp(headingErr / 40.0f, -0.4f, 0.4f);
    return c;
}

} // namespace flying
