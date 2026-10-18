#pragma once

#include "kke/ParticleSim.h"

#include <glm/glm.hpp>

namespace kke {

// GPU particles that need a step each frame (ENG-F11). Most GPU particles
// move in closed form from their birth (kke/ParticleMotion.h); one that is
// pulled to its emitter (attract), carried by turbulence or bounces off the
// world (collide) has state instead, moved by shaders/effect_step.comp each
// frame. This is that shader's step, on the CPU, so the tests can check it
// against kke::ParticleSim's own (the CPU fallback). Collisions on the GPU
// are against the last frame's depth: whatever is on screen, with no
// colliders to set up and no ray budget.
struct StepParticle {
    glm::vec3 position{0.0f}, velocity{0.0f};
    float age = 0.0f;
    bool resting = false; // a flake that landed on something on screen (GPU depth collisions)
};
struct StepParams {
    ParticleSim::Kind kind = ParticleSim::Kind::Spark;
    float drag = 1.0f, rise = 0.0f, flutter = 0.0f, seed = 0.0f, floor = 0.0f;
    glm::vec3 origin{0.0f};
    float vortex = 0.0f, attract = 0.0f;
    float turbulence = 0.0f;     // m/s of swirling air
    float turbulenceSize = 2.0f; // m: the size of its eddies
};

// One step of `dt` seconds with `wind` (m/s) at sim time `time`: the same
// per-kind motion as ParticleSim::update, plus turbulence.
void particleStep(StepParticle& p, const StepParams& params, float dt, const glm::vec3& wind, float time);

// Turbulent air at `position` and `time`, at most 1 m/s: four travelling
// waves of curl (each cos(k.x + phase) (k x a)), so it is divergence free
// exactly, has no noise texture and costs four cosines.
glm::vec3 particleTurbulence(const glm::vec3& position, float size, float time);

// Off a surface at `surface` with unit `normal` (toward where the particle
// came from), as ParticleSim's collide(): sparks, glows and flakes bounce,
// a flake on ground facing up comes to rest, smoke slides along.
void particleBounce(StepParticle& p, ParticleSim::Kind kind, const glm::vec3& surface, const glm::vec3& normal);

} // namespace kke
