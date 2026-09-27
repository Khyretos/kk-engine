#pragma once

// Small procedural-motion helpers the cookbook uses (docs/cookbook/animation.md).
// Header-only and pure, so tests/test_cookbook.cpp runs the same code the
// docs quote.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace cookbook {

// --8<-- [start:spring]
// A critically damped spring: `x` chases `goal` as fast as it can without
// overshooting, and `halfLife` is the time it takes to get halfway there.
// Frame-rate independent (the exact solution, not a step of it), so the
// same motion at 30 and 240 fps. After Daniel Holden, "Spring-It-On".
template <typename T>
void springTowards(T& x, T& velocity, const T& goal, float halfLife, float dt) {
    const float d = 2.0f * 0.69314718f / std::max(halfLife, 1e-5f); // ln 2 / (halfLife / 2)
    const T j0 = x - goal;
    const T j1 = velocity + j0 * d;
    const float e = std::exp(-d * dt);
    x = e * (j0 + j1 * dt) + goal;
    velocity = e * (velocity - j1 * d * dt);
}
// --8<-- [end:spring]

// --8<-- [start:shake]
// Camera shake from "trauma" (0..1, after Squirrel Eiserloh's GDC talk):
// the offset grows with trauma squared, so small bumps stay small, and it
// is smooth noise (a few sines at unrelated rates), not random jumps.
// Returns yaw, pitch and roll in degrees.
inline glm::vec3 shakeOffset(float trauma, float time, float maxDegrees = 6.0f) {
    const float t = std::clamp(trauma, 0.0f, 1.0f);
    const float amount = t * t * maxDegrees;
    auto noise = [time](float a, float b, float c) {
        return (std::sin(time * a) + std::sin(time * b + 1.3f) * 0.6f + std::sin(time * c + 4.1f) * 0.3f) / 1.9f;
    };
    return glm::vec3(noise(23.0f, 37.0f, 61.0f), noise(29.0f, 43.0f, 53.0f), noise(19.0f, 31.0f, 47.0f)) * amount;
}
// --8<-- [end:shake]

// --8<-- [start:turn]
// The rotation that turns direction `from` toward `to`, by at most
// `maxDegrees`. The core of every "look at" (a head following you, a
// turret tracking a target): apply it on top of the animated pose.
inline glm::quat turnTowards(const glm::vec3& from, const glm::vec3& to, float maxDegrees) {
    const glm::vec3 a = glm::normalize(from), b = glm::normalize(to);
    const float angle = std::acos(std::clamp(glm::dot(a, b), -1.0f, 1.0f));
    if (angle < 1e-4f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 axis = glm::cross(a, b);
    if (glm::length(axis) < 1e-6f) // opposite: any axis at right angles will do
        axis = glm::cross(a, std::abs(a.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0));
    return glm::angleAxis(std::min(angle, glm::radians(maxDegrees)), glm::normalize(axis));
}
// --8<-- [end:turn]

} // namespace cookbook
