#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Cloth: capes, flags, curtains, blankets, sheets, nets. Simulated by
// Jolt Physics' soft bodies (MIT, the same solver as the rigid bodies,
// so cloth collides with everything in a kke::RigidWorld), plus the
// engine's own clipping protection on top. docs/CLOTH.md.
//
// Why Jolt and not FEMFX: FEMFX solves volumes made of tetrahedra
// (jelly, rubber, breakable hero objects). A sheet of cloth is a surface
// with no volume; as tets it would need paper-thin elements, which are
// both slow and unstable. Jolt's soft bodies are position-based (XPBD)
// cloth with stretch, shear and bend constraints, long-range tethers,
// skinned back-stops and vertex-vs-shape collision with CCD.
//
// Clipping protection (ClothProtection), on Full by default:
//  - Every vertex is a ball of `thickness` (Jolt's vertex radius): cloth
//    rests on things instead of in them.
//  - Tethers (long-range attachments) to the pinned vertices: cloth can't
//    stretch past its length, so it can't be yanked through a collider.
//  - Self and cloth-vs-cloth collision (Jolt has none): every vertex is
//    kept `thickness` away from the triangles of every Full-protected
//    cloth, and a vertex that crossed a triangle between two steps is put
//    back on the side it came from (continuous, so fast folds don't pass
//    through either). A blanket folds on itself, never through itself.
//  - Skinned cloth (a cape on a character) gets back-stops: a vertex can't
//    go behind the body surface under its skinned position.
//  - Characters (RigidWorld::addCharacter) carry a collider only cloth
//    sees: walking into a curtain pushes it aside.
// Basic keeps the first two (cheap), Off is raw Jolt: the developer's
// choice when the cost matters more than the look.

enum class ClothProtection : uint8_t { Off, Basic, Full };

// How a fabric behaves and looks. Density is real (kg/m^2). Stretch,
// shear and bend are softness: 0 = as stiff as the solver can make it,
// 1 = each solver sub-step fixes about two thirds of the error, 100 =
// about 2% (very floppy). Softness is scaled by each vertex's mass and
// the sub-step, so a fabric feels the same at any mesh resolution.
// clothFabric("silk") etc. give tuned presets; tweak a copy.
struct Fabric {
    std::string name = "cotton";
    // --- behaviour
    float density = 0.15f;          // kg/m^2: silk ~0.06, cotton ~0.15, denim ~0.45, wool ~0.35
    float stretch = 0.0f;           // softness along the threads
    float shear = 0.2f;             // softness of the diagonals: how it skews
    float bend = 25.0f;             // softness of folding: silk drapes (high), denim and leather are stiff (low)
    float damping = 0.1f;           // Jolt linear damping: how quickly motion settles
    float airDrag = 1.0f;           // multiplier on air resistance (0 = none): light fabrics float down, flags fly
    float friction = 0.5f;
    float thickness = 0.008f;       // m: the collision radius of every vertex
    float maxStretch = 1.05f;       // tethers: never longer than this x the rest distance to a pin
    int iterations = 6;             // solver sub-steps per physics step
    // --- look (drawn by DynamicMeshRenderer::drawCloth)
    glm::vec3 color{0.85f, 0.85f, 0.82f}; // sRGB
    float roughness = 0.8f;
    glm::vec3 sheenColor{1.0f};     // sRGB: the soft rim light fibres give (velvet, wool)
    float sheen = 0.3f;             // 0..1
    int weave = 0;                  // pattern: 0 plain, 1 twill (denim), 2 satin (silk), 3 knit (wool), 4 net, 5 none (leather, rubber)
    float weaveScale = 60.0f;       // pattern repeats per metre (clothGrid UVs are metres)
    float fuzz = 0.0f;              // 0..1: fibres standing off the surface (wool, fleece)
    float specular = 0.5f;          // 0..1: satin/silk gloss along the threads
};

// Presets: "silk", "satin", "cotton", "linen", "denim", "wool", "fleece",
// "leather", "canvas", "net", "rubber". Unknown names give cotton.
Fabric clothFabric(const std::string& name);
std::vector<std::string> clothFabricNames();

// A cloth's shape in its rest pose, world space. Faces are what's drawn
// and what collides; without faces (a net), `lines` are the threads.
struct ClothMesh {
    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> uvs;          // optional, per vertex
    std::vector<uint32_t> indices;       // triangles
    std::vector<uint32_t> lines;         // pairs: extra threads (nets, ropes)
    int columns = 0, rows = 0;           // set by clothGrid (0 for other meshes)
};

// A rectangle of `columns` x `rows` vertices, `width` x `height` metres,
// centred on `center`, spanning `right` and `down` (unit vectors):
// down = (0,-1,0) hangs it like a curtain, down = (0,0,1) lays it flat.
ClothMesh clothGrid(const glm::vec3& center, float width, float height, int columns, int rows,
                    const glm::vec3& right = glm::vec3(1, 0, 0), const glm::vec3& down = glm::vec3(0, -1, 0));
// A net: the same grid, but only threads (every `step`-th row and column
// of vertices), knots where they cross. Drawn as threads.
ClothMesh clothNet(const glm::vec3& center, float width, float height, int columns, int rows,
                   const glm::vec3& right = glm::vec3(1, 0, 0), const glm::vec3& down = glm::vec3(0, -1, 0));
// Grid vertex index helpers (for pinning).
inline uint32_t clothGridIndex(const ClothMesh& m, int column, int row) { return static_cast<uint32_t>(row * m.columns + column); }

struct ClothDesc {
    ClothMesh mesh;
    Fabric fabric;
    ClothProtection protection = ClothProtection::Full;
    // Pinned vertices don't move (a curtain rod, a flag pole). They follow
    // joint `pinJoint` of setClothJoints() if joints are given.
    std::vector<uint32_t> pinned;
    // Skinning (capes, skirts): per vertex, up to 4 joints and weights
    // into `bindPose` (the joints' world matrices at the rest pose). A
    // vertex with maxDistance 0 is glued to its skinned position; with
    // maxDistance > 0 it may swing that far. Back-stops keep skinned
    // vertices out of the body (protection Full or Basic).
    struct SkinVertex { glm::uvec4 joints{0}; glm::vec4 weights{0}; float maxDistance = -1.0f; }; // < 0 = not skinned
    std::vector<SkinVertex> skin;
    std::vector<glm::mat4> bindPose;
    float backStop = 0.02f;          // m behind the skinned surface a vertex may go before it's pushed out
    uint32_t pinJoint = 0;
    float gravity = 1.0f;            // multiple of world gravity
};

struct ClothStats {
    uint32_t vertices = 0, triangles = 0;
    bool sleeping = false;
    uint32_t selfContacts = 0;       // vertex-triangle pairs the protection pass pushed apart last step
    uint32_t crossingsUndone = 0;    // vertices that had crossed a triangle and were put back
};

// Smooth per-vertex normals of a triangle list (for drawing).
void clothNormals(const std::vector<glm::vec3>& positions, const std::vector<uint32_t>& indices, std::vector<glm::vec3>& normals);

} // namespace kke
