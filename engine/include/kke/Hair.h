#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Hair: guide strands simulated by Jolt Physics' soft bodies (the same
// XPBD solver as the cloth: stretch along each strand, bend across two
// segments, and across three in a curl), in a kke::RigidWorld next to the
// cloth; many drawn hairs follow each guide (kke::HairRenderer, on the
// GPU). docs/HAIR.md.
//
// What else was tried:
//  - Jolt's own GPU hair (Jolt/Physics/Hair) is marked "in development"
//    by its author, has no wind and collides only with convex hulls.
//  - Jolt's Cosserat rods (bend and twist): nothing turns the first rod's
//    twist with the head (Jolt can't set a rod's orientation), so a quick
//    head turn flipped strands' rest curve from down to up.
//  - FEMFX solves volumes (tetrahedra), not strands.
//
// No clipping: every guide vertex is a ball of `thickness` that collides
// with the world (give the head, neck and shoulders colliders, usually
// RigidWorld::BodyDesc::clothOnly), tethers stop a strand being stretched
// through them, and `hold` keeps a hairstyle near its styled shape. The
// drawn hairs sit around their guide, so they stay out of the head too.
// Strands don't collide with each other or with cloth (the usual game
// trade: at a few hundred guides it would cost more than all the rest).

// How hair behaves and looks. Softness works as in kke::Fabric: 0 = as
// stiff as the solver can make it, 1 = each sub-step fixes about two
// thirds of the error, 100 = about 2%. hairStyle("wavy") etc. are presets.
struct HairStyle {
    std::string name = "straight";
    // --- shape (the rest pose)
    float length = 0.3f;             // m, root to tip
    int segments = 12;               // segments per guide strand
    float curl = 0.0f;               // turns per metre (0 = straight)
    float curlRadius = 0.012f;       // m
    bool helix = true;               // curls spiral (true) or wave in one plane (false)
    float droop = 0.06f;             // m: how soon a strand turns from the scalp towards `down`
    // --- coils (types 3 and 4): drawn around every drawn hair on the GPU,
    // far finer than the guides could hold. A coil unwinds as its guide is
    // pulled longer (the hair's own length stays the same), so a stretched
    // coil is thinner and longer and springs back.
    float coil = 0.0f;               // turns per metre of hair as it rests (0 = none)
    float coilRadius = 0.0f;         // m
    float zigzag = 0.0f;             // 0 = round spirals .. 1 = sharp Z-shaped bends (type 4B)
    float definition = 0.5f;         // 0 = every hair coils on its own (a soft halo) .. 1 = a clump's hairs coil together (ringlets)
    float shrinkage = 0.0f;          // 0..0.9: how much shorter the hair rests than pulled straight (4C: 0.75);
                                     // strands can be pulled out to 1 / (1 - shrinkage) x their length and spring back
    // --- plaits (box braids, cornrows, twists, locs): each guide is one
    // braid; its drawn hairs are laid in `plait` strands that cross over
    // each other around it all the way to the tip (on the GPU, as coils).
    int plait = 0;                   // 0 = loose hair, 1 = one rope (locs), 2 = a two-strand twist, 3 = a three-strand braid
    float plaitRadius = 0.004f;      // m: half the braid's width
    float plaitTurns = 24.0f;        // per metre: a three-strand braid's pattern repeats (six crossings) this often
    // --- behaviour
    float density = 0.004f;          // kg per metre of the clump one guide stands for
    float stretch = 0.0f;            // softness along the strand
    float bend = 4.0f;               // softness of bending and twisting (curls spring back when low)
    float stiffRoot = 0.25f;         // the bend softness at the root, as a fraction of `bend` (roots are stiffer)
    float damping = 0.2f;
    float airDrag = 1.0f;            // multiplier on air resistance (0 = none)
    float width = 0.003f;            // m: how wide the clump is to the air (its hairs shelter each other)
    float friction = 0.3f;
    float thickness = 0.004f;        // m: collision radius of every guide vertex
    float hold = 0.0f;               // 0 = hangs free, 1 = keeps its styled shape exactly (gel)
    float maxStretch = 1.05f;        // tethers: never longer than this x its rest length
    float gravity = 1.0f;
    int iterations = 6;              // solver sub-steps per physics step
    // --- look (HairStrands and DynamicMeshRenderer::drawHair)
    glm::vec3 rootColor{0.10f, 0.06f, 0.04f}; // sRGB
    glm::vec3 tipColor{0.22f, 0.14f, 0.08f};  // sRGB
    int hairsPerGuide = 32;          // drawn hairs around each guide
    float spread = 1.0f;             // how far they spread around it, in guide spacings
    float clump = 0.4f;              // 0..1: tips gather towards their guide
    float frizz = 0.0f;              // m: random offsets along each hair
    float hairWidth = 0.0008f;       // m drawn (a clump of real hairs; kept at least a pixel wide)
    float shine = 0.6f;              // 0..1: the primary highlight
    float shift = 0.08f;             // highlight shift along the hair (cuticle tilt)
};

// Presets: every hair type of Andre Walker's chart, "1a" (fine, straight)
// to "4c" (tight coils, 75% shrinkage) (docs/HAIR.md has the table), and
// "straight", "long", "wavy", "curly", "short", "fur", "gel". Hairstyles
// that need a shape as well (an afro, a puff, bantu knots) are
// hairstyleOnHead(). Unknown names give straight.
HairStyle hairStyle(const std::string& name);
std::vector<std::string> hairStyleNames();

struct HairDesc {
    // Guide roots and the directions the hair grows (world space, at
    // `bindPose`). hairScalp() fills them for a head.
    std::vector<glm::vec3> roots, directions;
    HairStyle style;
    // The head's world matrix at the rest pose; setHairJoint() moves it.
    glm::mat4 bindPose{1.0f};
    // The head as a sphere (bind-pose world centre, radius; radius 0 = none):
    // only shapes the rest pose, so strands lie over it instead of through
    // it. Collision comes from the head's collider body.
    glm::vec3 headCenter{0.0f};
    float headRadius = 0.0f;
    glm::vec3 down{0.0f, -1.0f, 0.0f}; // where the rest pose falls (gravity)
    glm::vec3 comb{0.0f};            // added to `down` for the rest pose: hairScalp combs it back, off the face
    // Optional, one per root: its length as a fraction of style.length
    // (a cut: a flat top, layers). Empty = all full length.
    std::vector<float> lengths;
    // Hair gathered and tied (a puff, a bun, bantu knots, cornrows): every
    // root within `reach` of a tie (or as tiedTo says) lies along the scalp
    // to it, then a Puff bursts out from there in a ball, a Knot winds
    // round into a knot `size` across and a Hang hangs down from it.
    struct Tie {
        enum class Shape { Puff, Knot, Hang };
        glm::vec3 at{0.0f};          // on the scalp (bind pose, world)
        float reach = 1.0f;          // m, over the scalp
        float size = 0.03f;          // m: a knot's width
        Shape shape = Shape::Puff;
    };
    std::vector<Tie> ties;
    // Optional, one per root: the tie it goes to (-1 = none). Empty = the
    // nearest within reach.
    std::vector<int> tiedTo;
    float wind = 1.0f;               // how much of RigidWorld::setWind reaches it
};

// Guide roots on a head: `count` points spread evenly (Fibonacci sphere)
// over the part of a sphere where hair grows: within `crown` radians of
// `up`, and not over the face (the side towards `front` below `hairline`
// radians from up). Fills desc.roots, desc.directions, headCenter,
// headRadius and comb (back, away from the face).
void hairScalp(HairDesc& desc, const glm::vec3& center, float radius, int count, float crown = 1.9f,
               const glm::vec3& up = glm::vec3(0, 1, 0), const glm::vec3& front = glm::vec3(0, 0, 1), float hairline = 1.0f);

// A hairstyle on a head, as hairScalp() and more: its type, where it
// grows, its cut and ties. "afro", "puff", "high-top fade", "twist-out",
// "bantu knots", "box braids", "cornrows", "locs", "two-strand twists"
// (hairstyleNames()); any hairStyleNames() name gives that type grown
// out. Keeps desc.style's colours. `guides` is a hint: braids, cornrows
// and locs have as many as the style has. False for unknown names.
bool hairstyleOnHead(HairDesc& desc, const std::string& name, const glm::vec3& center, float radius, int guides,
                     const glm::vec3& up = glm::vec3(0, 1, 0), const glm::vec3& front = glm::vec3(0, 0, 1));
std::vector<std::string> hairstyleNames();

// Vertices per guide strand: the root, the follicle (both follow the head)
// and one per segment.
inline int hairStrandVertices(const HairStyle& s) { return (s.segments < 1 ? 1 : s.segments) + 2; }
// Rest pose of every guide, strand after strand (world, bind pose).
std::vector<glm::vec3> hairRestPose(const HairDesc& desc);

struct HairStats {
    uint32_t guides = 0, vertices = 0;
    bool sleeping = false;
};

} // namespace kke
