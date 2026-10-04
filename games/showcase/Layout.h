#pragma once

#include <glm/glm.hpp>

// Where kke_demo's stations are (one place, so the level, the stress
// test and the HUD agree). The floor is 60 x 60 m around the origin: the
// yard, in the middle of an open world 1.4 km across (World.cpp) whose
// zones are below.
namespace kke_showcase::layout {

inline constexpr glm::vec3 kYard(14.0f, 0.0f, -6.0f);        // breaking yard (FEMFX glass, plank, stone wall)
inline constexpr glm::vec3 kCratePile(4.0f, 0.0f, -3.0f);    // Jolt crate pyramid
inline constexpr glm::vec3 kPoolCenter(-18.0f, 0.0f, 20.0f); // water: floating props
inline constexpr glm::vec3 kLavaCenter(0.0f, 0.0f, 20.0f);   // lava pouring on a melting block
inline constexpr float kLaneX = 20.0f;                        // parkour lane, run toward -Z from z = 28
inline constexpr float kTrickX = 26.0f;                       // trick course face: wall run, ledge leaps
inline constexpr glm::vec3 kPlatform(-14.0f, 0.0f, 6.0f);     // moving platform
inline constexpr glm::vec3 kLowRoof(10.0f, 0.0f, 6.0f);       // crouch under it
inline constexpr glm::vec3 kSupply(-4.5f, 0.0f, 2.5f);        // supply table: things to pick up and equip
inline constexpr glm::vec3 kSupplyTableHalf(1.3f, 0.42f, 0.45f); // its top is 0.84 m up

// The yard's gates: gaps in its walls, the roads start there.
inline constexpr float kGateHalf = 5.0f;
inline constexpr glm::vec2 kGateNorth(0.0f, -30.0f), kGateSouth(0.0f, 30.0f), kGateWest(-30.0f, 0.0f), kGateEast(30.0f, -18.0f);

// The open world: terrain from -kWorldHalf to +kWorldHalf on X and Z, a
// vertex every kTerrainStep metres (one lands on each yard wall).
inline constexpr float kWorldHalf = 700.0f;
inline constexpr float kTerrainStep = 5.0f;

// The zones round the yard, reached on foot, by road, by car or by plane
// (Kees, 2026-10-04: one open world). The HUD names the one you're in;
// the world map (M, or the pause menu) lists them and takes you there.
struct Zone {
    const char* name;  // on the HUD and the map
    const char* text;  // what to do there (prompt text: {action} shows that button)
    glm::vec3 centre;  // y = the ground there
    float radius;      // m: inside it, you're there
    glm::vec3 color;   // its flag, its dot on the map
    glm::vec3 arrive;  // where travelling there puts you (on the ground there: y is a guess)
    float arriveYaw;   // degrees, the way you face on arrival
};
inline constexpr Zone kZones[] = {
    { "THE YARD", "The course: parkour lane, trick course, crates, lava, pool, breaking yard, supply table.", { 0.0f, 0.0f, 0.0f }, 30.0f,
      { 0.95f, 0.8f, 0.3f }, { 0.0f, 0.05f, 6.0f }, 0.0f },
    { "PARKOUR PARK", "Walls, gaps and rooftops to run, vault and climb.", { 0.0f, 0.0f, -160.0f }, 70.0f, { 0.3f, 0.6f, 0.95f },
      { 0.0f, 0.05f, -95.0f }, 0.0f },
    { "FIRING RANGE", "Targets and things to break with the guns from the supply table.", { 170.0f, 0.0f, -150.0f }, 45.0f, { 0.9f, 0.3f, 0.25f },
      { 140.0f, 0.05f, -120.0f }, 45.0f },
    { "AIRFIELD", "A runway and a hangar.", { 380.0f, 0.0f, 40.0f }, 200.0f, { 0.95f, 0.95f, 0.95f }, { 200.0f, 0.05f, 15.0f }, 90.0f },
    { "RACE TRACK", "An oval and a car park.", { 0.0f, 0.0f, 255.0f }, 125.0f, { 0.2f, 0.2f, 0.2f }, { 0.0f, 0.05f, 140.0f }, 180.0f },
    { "NATURE PARK", "A forest and a meadow.", { -230.0f, 0.0f, 40.0f }, 110.0f, { 0.3f, 0.75f, 0.3f }, { -135.0f, 0.05f, 8.0f }, -90.0f },
    { "SNOW FIELD", "Snow up on the mountain.", { -300.0f, 30.0f, -280.0f }, 80.0f, { 0.85f, 0.92f, 1.0f }, { -255.0f, 30.05f, -230.0f },
      -45.0f },
};
inline constexpr int kZoneCount = static_cast<int>(sizeof(kZones) / sizeof(kZones[0]));

} // namespace kke_showcase::layout
