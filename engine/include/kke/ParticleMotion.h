#pragma once

#include "kke/ParticleSim.h"

#include <glm/glm.hpp>

namespace kke {

// Where a particle is `t` seconds after its birth, in closed form (ENG-G18
// part 4): the motion kke::ParticleSim steps frame by frame, solved
// exactly, so a GPU particle needs no state after its birth.
// shaders/effect_gpu.vert evaluates the same formulas per vertex; this is
// their CPU copy and the tests' reference (tests/test_particle_motion.cpp).
//
// Exact for drag toward the wind, rise (or gravity), radius growth that
// eases off, spin and tumbling, a ring's growth and a swirl round a still
// particle's spot. Close for the rest: a flake's flutter is its sway's own
// path (the drag that lags it behind is left out), a swirl uses the radius
// the particle would have without it, and sparks and flakes stop at their
// floor instead of bouncing (a GPU particle has no collisions): `landed`.
struct ParticleMotion {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float radius = 0.0f;
    float angle = 0.0f;
    float flipAngle = 0.0f;
    bool landed = false; // a spark or flake that reached its floor (a GPU spark is hidden then: rain hitting the ground)
};

// `birth` is the particle as emitted (its age is ignored); `wind` the wind
// for its whole life (m/s).
ParticleMotion particleMotion(const ParticleSim::Particle& birth, float t, const glm::vec3& wind);

} // namespace kke
