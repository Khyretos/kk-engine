#pragma once

// A racket's strings: FEMFX elastic, like the ball.
//
// The string bed is a thin FEMFX slab whose rim is pinned in the frame
// (PhysicsModule::TetSpawnOptions::pinnedVerts), a drum skin in its hoop.
// A hit pushes the slab's vertices around the impact point along the
// ball's path; FEMFX bends it into a pocket and lets it ring back. The
// strings drawn on the racket ride on the slab (kke::TetEmbedding), so they
// bend with it.
//
// The slab never moves with the racket: FEMFX would have to drag it
// through the air at 40 m/s every swing, and a racket swings in the same
// air as the ball. Each bed lives still, out of sight, far above the sport
// center (drawOnlyCracks: FEMFX never draws it), and only its pocket (how
// far the strings moved along the face's normal) is copied onto the
// racket, in the racket's own frame. It stands upright, so gravity pulls
// along the strings, not through them. Each racket gets its bed when the
// player comes on court; it settles into its frame, then sleeps until a
// hit wakes it, and sleeps again once it has rung out (asleep it costs
// nothing).
//
// How long the pocket lasts is not real: a real string bed holds the ball
// for about 5 ms, less than one frame at 60 fps. These strings are softer,
// so the dent and the ringing last a few frames and can be seen.

#include "kke/TetMeshAsset.h"
#include "kke/VoxelTets.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace kke {
class PhysicsModule;
struct Vertex;
} // namespace kke

namespace tennis {

// Where the strings are (pure maths, unit-tested): the racket frame has
// the grip at the origin, the shaft along +Y and the strings facing +Z.
struct StringLayout {
    glm::vec2 centre{0.0f, 0.46f}; // the head's middle
    glm::vec2 radii{0.125f, 0.16f}; // half the head's width and length, inside the frame
    int mains = 16;                 // strings along the shaft
    int crosses = 19;               // strings across it

    // Every string as a run of points (racket frame, z = 0), each run
    // from rim to rim; `first[i]` is where string i starts in `points`
    // (first.back() == points.size()). Mains come first.
    std::vector<glm::vec3> points;
    std::vector<uint32_t> first;
    void build(float spacing = 0.018f);

    // Flat ribbons along every string, `width` wide, facing both ways (+Z
    // and -Z): `bent[i]` is points[i] with the bed's pocket added.
    void mesh(const std::vector<glm::vec3>& bent, float width, const glm::vec3& color, std::vector<kke::Vertex>& v,
              std::vector<uint32_t>& idx) const;
};

class StringBed {
public:
    // `slot`: which parking place (each bed has its own, out of sight).
    StringBed(kke::PhysicsModule& physics, const StringLayout& layout, int slot);
    ~StringBed();
    StringBed(const StringBed&) = delete;
    StringBed& operator=(const StringBed&) = delete;
    bool valid() const { return m_handle != 0; }
    int slot() const { return m_slot; }

    // The ball met the strings at `at` (racket frame) moving at `velocity`
    // (racket frame, m/s, relative to the racket).
    void strike(const glm::vec3& at, const glm::vec3& velocity);
    // Reads the bed back: true while the strings are bent (then `bent`
    // holds every layout point, racket frame); false once they are
    // straight again (the rest shape can be drawn).
    bool update(float dt, std::vector<glm::vec3>& bent);
    // The deepest point of the pocket, m (tests, KKE_TENNIS_STRINGTEST).
    float depth() const { return m_depth; }

private:
    kke::PhysicsModule& m_physics;
    const StringLayout& m_layout;
    int m_slot = 0;
    uint32_t m_handle = 0;      // PhysicsModule::ObjectHandle
    glm::vec3 m_park{0.0f};     // where the bed's middle is, world
    kke::TetEmbedding m_embed;  // the layout's points in the slab
    std::vector<glm::vec3> m_normals, m_out, m_outNormals;
    std::vector<float> m_rest;       // each point's offset along the normal once settled: "straight"
    std::vector<glm::vec3> m_settle; // settling: last frame's points
    float m_still = 0.0f;            // s the strings have been straight (or, settling, still)
    bool m_bent = false;
    float m_depth = 0.0f;
};

} // namespace tennis
