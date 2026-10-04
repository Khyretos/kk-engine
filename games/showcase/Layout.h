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
// The firing range's bench (Range.cpp): you shoot from behind it toward +X.
inline constexpr glm::vec3 kRangeBench(140.0f, 0.0f, -150.0f);
inline constexpr glm::vec3 kRangeBenchHalf(0.45f, 0.45f, 6.0f);

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
    { "PARKOUR PARK", "Seven sections, one move each: vault field, wall climb, hang vault, shimmy wall, ledge leaps, wall run and rooftops.", { 0.0f, 0.0f, -160.0f }, 70.0f, { 0.3f, 0.6f, 0.95f },
      { 0.0f, 0.05f, -95.0f }, 0.0f },
    { "FIRING RANGE", "Guns on the bench: steel plates, glass and stone to break, red barrels to blow up.", { 170.0f, 0.0f, -150.0f }, 45.0f, { 0.9f, 0.3f, 0.25f },
      { 137.5f, 0.05f, -150.0f }, 90.0f },
    { "AIRFIELD", "A stunt plane on the runway: walk up and get in {pickup}. Take off, loop, land.", { 380.0f, 0.0f, 40.0f }, 200.0f, { 0.95f, 0.95f, 0.95f }, { 200.0f, 0.05f, 15.0f }, 90.0f },
    { "RACE TRACK", "Three cars in the car park: get in {pickup} and drive a lap of the oval. Cones on the far straight, a jump by the car park.", { 0.0f, 0.0f, 255.0f }, 125.0f, { 0.2f, 0.2f, 0.2f }, { 0.0f, 0.05f, 140.0f }, 180.0f },
    { "NATURE PARK", "Trees and grass swaying in the wind. Take the axe from the chopping block and fell a tree; pick flowers in the meadow.", { -230.0f, 0.0f, 40.0f }, 110.0f, { 0.3f, 0.75f, 0.3f }, { -135.0f, 0.05f, 8.0f }, -90.0f },
    { "SNOW FIELD", "Fresh snow that keeps every footprint and tyre track. Reset the world {reset} for fresh snow.", { -300.0f, 30.0f, -280.0f }, 80.0f, { 0.85f, 0.92f, 1.0f }, { -255.0f, 30.05f, -230.0f },
      -45.0f },
};
inline constexpr int kZoneCount = static_cast<int>(sizeof(kZones) / sizeof(kZones[0]));

} // namespace kke_showcase::layout
