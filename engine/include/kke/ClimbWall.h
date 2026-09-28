#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {

// A generated mountain face to climb: the rock (a height field standing
// up, so slabs, vertical bands and overhangs), the holds on it, and rest
// ledges to stand on. Same seed, same mountain, on every machine, so a
// race is fair and a time means something.
//
// Space: the face runs along X (width) and Y (height, feet at y = 0),
// and faces +Z: climbers come from +Z and reach toward -Z. The rock's
// surface at (x, y) is at z = surfaceZ(x, y); a positive lean (overhang)
// makes z grow with height, a slab (leaning back) makes it shrink.
//
// Pure CPU, no physics: the game turns buildMesh() into a Jolt mesh body
// and something to draw. Unit-tested in tests/test_climb_wall.cpp.
struct ClimbHold {
    enum class Kind : uint8_t {
        Jug,    // big, deep: rest on it
        Crimp,  // a thin edge: tiring
        Sloper, // rounded, no lip: the most tiring
        Edge,   // the front edge of a ledge or the summit: a jug you can mantle over
    };
    glm::vec3 position{0.0f};         // where the hand closes on it
    glm::vec3 normal{0.0f, 0.0f, 1.0f}; // the rock's normal there
    Kind kind = Kind::Jug;
    float size = 0.12f;               // m, half the hold's width
    bool loose = false;               // breaks off when you lunge at it
    bool route = false;               // on the generated route (always climbable)
    int ledge = -1;                   // Edge: which ledge (-1 = the summit)
};

// How good a hold is to hang from (1 = a jug, lower = more tiring).
float holdGrip(ClimbHold::Kind kind);
// How far a hold sticks out of the rock (its apex); its `position` is
// 80% of the way out.
float holdDepth(ClimbHold::Kind kind, float size);
const char* holdKindName(ClimbHold::Kind kind);

// A shelf sticking out of the rock: a box, world space. Standing on one
// is a rest (stamina comes back).
struct ClimbLedge {
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{1.0f, 0.2f, 0.45f};
    float top() const { return center.y + halfExtents.y; }
};

struct ClimbWallDesc {
    uint32_t seed = 1;
    float width = 16.0f;       // m along X, centred on x = 0
    float height = 36.0f;      // m, the summit's height
    float cell = 0.4f;         // m, the rock grid
    float bumpiness = 0.28f;   // m, small-scale rock relief
    float buttress = 0.55f;    // m, large ribs and bays across the face
    float sectionMin = 6.0f, sectionMax = 10.0f; // m, bands of one lean
    float maxOverhang = 22.0f; // degrees past vertical
    float maxSlab = 24.0f;     // degrees leaning back
    // Holds.
    float spacing = 0.55f;     // m, closest two holds may be
    float density = 1.1f;      // holds per m^2 (before spacing rejects some)
    float routeStep = 1.2f;    // m, longest step on the guaranteed route
    float margin = 1.6f;       // m from the sides kept free of holds
    float looseChance = 0.06f; // of the holds off the route
    // The hold mix: extra weight for jugs or crimps (0 = the usual mix,
    // which follows the lean: more jugs on overhangs, more crimps on
    // slabs). 0.5 makes jugs a lot more common; negative, rarer.
    float jugBias = 0.0f;
    float crimpBias = 0.0f;
    int ledges = 3;            // rest ledges between the base and the summit
};

// Triangles to draw and collide with (flat shaded: three vertices per
// triangle, each with its face's normal and a rock or hold colour).
struct ClimbMesh {
    std::vector<glm::vec3> positions, normals, colors;
    std::vector<uint32_t> indices;
};

class ClimbWall {
public:
    static ClimbWall generate(const ClimbWallDesc& desc);

    const ClimbWallDesc& desc() const { return m_desc; }
    // The rock's surface (bilinear on the grid; clamped outside it).
    float surfaceZ(float x, float y) const;
    glm::vec3 surfaceNormal(float x, float y) const;
    // A point on the rock at (x, y), pushed `out` metres off it.
    glm::vec3 surfacePoint(float x, float y, float out = 0.0f) const;
    // The band's lean at height y: degrees past vertical (+ overhang,
    // - slab). The steeper, the harder on the arms.
    float leanAt(float y) const;

    const std::vector<ClimbHold>& holds() const { return m_holds; }
    const std::vector<ClimbLedge>& ledges() const { return m_ledges; }
    float summitY() const { return m_desc.height; }
    // The summit plateau's front edge (z of the rock at the top).
    float summitZ() const { return surfaceZ(0.0f, m_desc.height); }

    // The nearest hold to `p` within maxDist (-1 = none). Skips `skip`.
    int nearestHold(const glm::vec3& p, float maxDist, int skip = -1) const;
    // Holds within `radius` of p.
    void holdsNear(const glm::vec3& p, float radius, std::vector<int>& out) const;

    // How far apart two holds are for a reach: along the rock counts in
    // full, out from it (a ledge's lip, a bulge) at half, as the body
    // leans out for it.
    static float reachDistance(const glm::vec3& a, const glm::vec3& b);
    // Can someone who spans `span` metres between their hands get from a
    // hold near the ground to the summit edge? (Hands leapfrog: a hand
    // can go to any hold within `span` of the other hand's hold.)
    bool routeExists(float span) const;
    // The shortest such chain of holds, bottom to top (empty if none).
    std::vector<int> route(float span) const;
    // The line the generator drew (bottom to top, steps of at most
    // routeStep): past every ledge's edge, up to the summit's edge.
    // What the bot climbs, and a hint line for the player.
    const std::vector<int>& line() const { return m_line; }

    // The rock (with side skirts and the summit's lip) plus every hold
    // that isn't loose. Loose holds are the game's to draw (they fall).
    ClimbMesh buildMesh() const;
    // One hold's rock, in the same format (for loose holds, at the origin
    // of its own body: positions relative to hold.position).
    static void appendHold(const ClimbHold& hold, uint32_t seed, const glm::vec3& origin, ClimbMesh& mesh);

private:
    void buildSurface();
    void placeLedges();
    void placeHolds();
    ClimbHold makeHold(float x, float y, ClimbHold::Kind kind) const;
    bool blockedByLedge(float x, float y) const;

    ClimbWallDesc m_desc;
    int m_cols = 0, m_rows = 0;
    std::vector<float> m_z;    // m_rows x m_cols, row-major, row 0 at y = 0
    std::vector<float> m_lean; // per row, degrees
    std::vector<ClimbHold> m_holds;
    std::vector<int> m_line;
    std::vector<ClimbLedge> m_ledges;
};

} // namespace kke
