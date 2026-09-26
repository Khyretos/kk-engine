#pragma once

#include <glm/glm.hpp>

// Where kke_demo's stations are (one place, so the level, the stress
// test and the HUD agree). The floor is 60 x 60 m around the origin.
namespace kke_showcase::layout {

inline constexpr glm::vec3 kYard(14.0f, 0.0f, -6.0f);        // breaking yard (FEMFX glass, plank, stone wall)
inline constexpr glm::vec3 kCratePile(4.0f, 0.0f, -3.0f);    // Jolt crate pyramid
inline constexpr glm::vec3 kPoolCenter(-18.0f, 0.0f, 20.0f); // water: floating props
inline constexpr glm::vec3 kLavaCenter(0.0f, 0.0f, 20.0f);   // lava pouring on a melting block
inline constexpr float kLaneX = 20.0f;                        // parkour lane, run toward -Z from z = 28
inline constexpr float kTrickX = 26.0f;                       // trick course face: wall run, ledge leaps
inline constexpr glm::vec3 kPlatform(-14.0f, 0.0f, 6.0f);     // moving platform
inline constexpr glm::vec3 kLowRoof(10.0f, 0.0f, 6.0f);       // crouch under it

} // namespace kke_showcase::layout
