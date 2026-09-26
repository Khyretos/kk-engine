#pragma once

#include "kke/Animator.h"
#include "kke/ModelAsset.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Jiggle physics: secondary motion on top of animation. Soft parts
// (breasts, bellies, glutes, hair, tails, antennae, cloth tips) lag,
// overshoot and settle when the body they hang from moves. Pure CPU, no
// GPU types; tested in tests/test_jiggle.cpp. See docs/JIGGLE.md.
//
// Three pieces, cheapest first:
//   JiggleRig  - bones. A chain of bones becomes a chain of verlet points
//                that chase the animated pose; the bones are then turned
//                (and optionally stretched) to follow the points. Works on
//                any skeleton; addJiggleBone() adds soft-tissue bones to
//                a rig that has none (most game characters).
//   JiggleSkin - meshes without bones. A few verlet points ride on bones;
//                vertices near a point move with its lag (ModelModule
//                applies it after skinning). Bellies and thighs.
//   JellyBody  - a whole soft object (jelly, slime, a water balloon): a
//                particle lattice kept in shape by overlapping local
//                shape matching, colliding with spheres and a floor.
//
// Why not FEMFX for this: FEM solves real volumetric elasticity and
// fracture (kke::PhysicsModule), which is what breakable hero objects
// need, but a jiggle is secondary motion that has to run on every
// character every frame. These solvers cost microseconds per character
// (a handful of points, no matrices to assemble) and never explode: every
// step is a clamp or a blend, not a force.
//
// Techniques (ideas from naelstrof's JigglePhysics for Unity, rewritten
// for this engine, and the usual failure modes of the "jiggle physics"
// trope avoided on purpose):
//  - Verlet points driven by the *difference* from animation, so the
//    authored pose always wins in the end and nothing drifts.
//  - Fixed internal step (default 90 Hz) with the result interpolated as
//    an offset from the current animated pose: frame-rate independent,
//    and no lag or jitter against the body at any frame rate.
//  - Two drags: `drag` damps motion relative to the parent (the tissue's
//    own wobble), `airDrag` damps world motion (what makes a ponytail
//    trail when running). Separate so a character in an elevator or a car
//    doesn't wobble forever.
//  - `soften`: the pull back to the pose is weaker near rest, so small
//    motions give a soft wobble and big motions are still held in check
//    (the trope's "bounces on its own while standing still" can't happen:
//    with no motion there is no energy to bounce with).
//  - Angle limit, length limits and collision spheres keep it anatomically
//    sane regardless of settings.
//  - Sleep: a rig whose points are at rest and whose pose isn't moving
//    skips its solve entirely. Teleports reset instead of snapping.

struct JiggleSettings {
    float stiffness = 0.25f;  // 0..1: pull toward the animated pose each step (0 = free, 1 = locked)
    float soften = 0.4f;      // 0..1: how much weaker that pull is near rest
    float stretch = 0.08f;    // 0..1: 0 = bone length is kept, 1 = bones stretch freely (squash and stretch)
    float angleLimit = 60.0f; // degrees a point may swing away from its pose; 0 = unlimited
    float drag = 0.12f;       // 0..1: velocity lost per step relative to the parent
    float airDrag = 0.03f;    // 0..1: world velocity lost per step
    float gravity = 0.25f;    // multiple of world gravity acting on the points (sag)
    float blend = 1.0f;       // 0..1: 0 = animation only
    float radius = 0.04f;     // collision radius of each point (metres)
    float maxStretch = 1.35f; // hard clamp on segment length (x rest, and 1/x)
};

// A world-space collider the points are pushed out of: a sphere when a
// and b are equal, a capsule otherwise. Personal colliders (the chest,
// the upper arms) keep breasts from swinging through the body.
struct JiggleCollider {
    glm::vec3 a{ 0.0f }, b{ 0.0f };
    float radius = 0.1f;
};

// Shared fixed-step clock: how many steps to run this frame and the
// interpolation fraction for rendering.
class JiggleClock {
public:
    explicit JiggleClock(float stepHz = 90.0f, int maxSteps = 4) : m_step(1.0f / stepHz), m_maxSteps(maxSteps) {}
    // Returns steps to run; a hitch past maxSteps drops the extra time.
    int advance(float dt);
    float alpha() const { return m_accum / m_step; }
    float step() const { return m_step; }

private:
    float m_step;
    int m_maxSteps;
    float m_accum = 0.0f;
};

// ---------------------------------------------------------------------
// Bones.
class JiggleRig {
public:
    struct Chain {
        int root = -1; // this bone stays animated; its descendants jiggle
        // Where the last bone of a branch "ends", in that bone's local
        // space. Zero = continue the direction and length of its parent
        // segment; a chain of one bone (a breast bone) needs this set.
        glm::vec3 tipOffset{ 0.0f };
        JiggleSettings settings;
    };
    // The root's head stays where the animation puts it; the root and its
    // descendants turn (and, with `stretch`, lengthen) to follow the
    // points. For hair, pass the first hair bone, not the head. A chain
    // of one bone whose tip lies along one of its local axes (what
    // addJiggleBone makes) also squashes and stretches along it, keeping
    // its volume.

    JiggleRig() = default;
    JiggleRig(const ModelData& model, const std::vector<Chain>& chains, float stepHz = 90.0f);

    // Runs the simulation for `dt` and bends `pose` (the model's local
    // TRS pose, e.g. Animator::pose()) to follow it. `toWorld` = the
    // instance transform: jiggle reacts to world motion.
    void apply(const ModelData& model, Pose& pose, const glm::mat4& toWorld, float dt,
               const std::vector<JiggleCollider>& colliders = {});
    // Forget the motion (after a teleport or a cut).
    void reset() { m_initialized = false; }

    bool valid() const { return !m_points.empty(); }
    size_t pointCount() const { return m_points.size(); }
    bool sleeping() const { return m_sleeping; }
    uint64_t stepsRun() const { return m_stepsRun; }
    // World-space simulated / animated position of point i (debug view).
    glm::vec3 pointPosition(size_t i) const { return m_points[i].rendered; }
    glm::vec3 pointTarget(size_t i) const { return m_points[i].animNow; }
    JiggleSettings& settings(size_t chain) { return m_chains[chain].settings; }
    // Largest angle (degrees) any segment is swung away from its pose, and
    // largest length change (ratio - 1) right now: for tuning panels and tests.
    float maxSwingDegrees() const;
    float maxStretchNow() const;
    float teleportDistance = 1.5f; // metres the root may move in one frame before we reset

private:
    struct Point {
        int bone = -1;        // -1 = virtual tip
        int parent = -1;      // point index; -1 = anchored (follows animation)
        int chain = 0;
        int firstChildOf = -1; // this point is the first child of that point (whose bone it turns)
        glm::vec3 tipLocal{ 0.0f }; // virtual tip: offset in the parent bone's space
        float restLength = 0.0f;
        glm::vec3 pos{ 0.0f }, prev{ 0.0f };
        glm::vec3 animPrevFrame{ 0.0f }, animNow{ 0.0f }, animStep{ 0.0f };
        glm::vec3 offsetPrev{ 0.0f }, offsetCur{ 0.0f }; // (sim - anim) at the last two steps
        glm::vec3 rendered{ 0.0f };
        glm::quat delta{ 1.0f, 0.0f, 0.0f, 0.0f }; // rotation this point's bone got this step (for children)
    };
    void stepOnce(float h, float t, const std::vector<JiggleCollider>& colliders);
    std::vector<Chain> m_chains;
    std::vector<Point> m_points;
    JiggleClock m_clock;
    bool m_initialized = false;
    bool m_sleeping = false;
    int m_quietSteps = 0;
    uint64_t m_stepsRun = 0;
    std::vector<glm::mat4> m_scratchModel;
};

// Adds a soft-tissue bone to a rig that has none: a new bone under
// `parent` at `position` (model space, rest pose) whose local +Y points
// along `axis` (so its chain tip is (0, length, 0)), and the skin within
// `radius` of it moves with it (weight `strength` at the centre, fading
// smoothly to 0 at the edge). Every animation clip gets the bone at its
// rest pose. At rest nothing changes visually; driven by a JiggleRig the
// tissue moves. Returns the new bone's index, or -1 if `parent` is invalid.
// Only skin that mostly follows `parent` or one of its ancestors is taken
// (so an arm resting against the chest keeps its own bones).
int addJiggleBone(ModelData& model, int parent, const std::string& name, const glm::vec3& position, const glm::vec3& axis,
                  float radius, float strength = 1.0f);
// Model-space rest transform of every bone.
std::vector<glm::mat4> restModelTransforms(const ModelData& model);

// Pushes the skin near `center` (model space, rest pose) outward by up to
// `amount` metres: away from `center`, blended toward `direction`
// if given (e.g. forward for breasts), with a smooth falloff to 0 at
// `radius`. The shape is edited in the mesh's own space, so skinning is
// unaffected. Returns how many vertices moved.
size_t inflateSkin(ModelData& model, const glm::vec3& center, float radius, float amount,
                   const glm::vec3& direction = glm::vec3(0.0f), float directionBias = 0.5f);

// ---------------------------------------------------------------------
// Meshes without bones.
struct SkinJiggleOffset {
    glm::vec3 center{ 0.0f }; // model space, where the zone is now (animated)
    float radius = 0.1f;
    glm::vec3 offset{ 0.0f }; // model space displacement at the centre
};
// Sum of zone displacements at model-space point p (smooth falloff).
glm::vec3 skinJiggleDisplacement(const std::vector<SkinJiggleOffset>& zones, const glm::vec3& p);

class JiggleSkin {
public:
    struct Zone {
        int bone = -1;
        glm::vec3 local{ 0.0f }; // centre in the bone's local space
        float radius = 0.1f;
        float maxOffset = 0.05f; // metres the skin may be displaced
        JiggleSettings settings;
    };
    JiggleSkin() = default;
    // `modelPositions`: zone centres in model space, rest pose (converted to bone space here).
    JiggleSkin(const ModelData& model, std::vector<Zone> zones, const std::vector<glm::vec3>& modelPositions,
               float stepHz = 90.0f);
    void apply(const ModelData& model, const Pose& pose, const glm::mat4& toWorld, float dt);
    const std::vector<SkinJiggleOffset>& offsets() const { return m_out; }
    bool valid() const { return !m_zones.empty(); }
    void reset() { m_initialized = false; }

private:
    struct State { glm::vec3 pos{ 0 }, prev{ 0 }, animPrev{ 0 }, animNow{ 0 }, offPrev{ 0 }, offCur{ 0 }; };
    std::vector<Zone> m_zones;
    std::vector<State> m_state;
    std::vector<SkinJiggleOffset> m_out;
    JiggleClock m_clock;
    bool m_initialized = false;
};

// ---------------------------------------------------------------------
// Soft tissue for any humanoid in one call: finds the chest, hips and
// thighs from the skeleton (canonical UE-style names: pelvis, spine_01..03,
// thigh_l/r, which Synty, Mixamo and UAL rigs all map to) and the skin
// around them, optionally reshapes the body (volume in metres; 0 leaves
// the mesh as the artist made it), adds breast and glute bones, and
// returns ready-to-use rig chains and skin zones (belly, thighs).
struct HumanoidSoftTissue {
    float bust = 0.0f;     // extra volume, metres
    float glutes = 0.0f;
    float hips = 0.0f;     // widens the hips to the sides
    bool breastBones = true, gluteBones = true;
    bool bellyZone = true, thighZones = true;
    JiggleSettings breast = [] {
        JiggleSettings s;
        s.stiffness = 0.16f; s.soften = 0.5f; s.stretch = 0.18f; s.angleLimit = 32.0f;
        s.drag = 0.1f; s.airDrag = 0.02f; s.gravity = 0.4f;
        return s;
    }();
    JiggleSettings glute = [] {
        JiggleSettings s;
        s.stiffness = 0.26f; s.soften = 0.5f; s.stretch = 0.12f; s.angleLimit = 22.0f;
        s.drag = 0.14f; s.airDrag = 0.02f; s.gravity = 0.25f;
        return s;
    }();
    JiggleSettings skin = [] {
        JiggleSettings s;
        s.stiffness = 0.3f; s.soften = 0.4f; s.drag = 0.16f; s.airDrag = 0.02f; s.gravity = 0.3f;
        return s;
    }();
};
struct HumanoidJiggleSetup {
    std::vector<JiggleRig::Chain> chains;  // for JiggleRig
    std::vector<JiggleSkin::Zone> zones;   // for JiggleSkin
    std::vector<glm::vec3> zonePositions;  // model space, rest pose
    std::vector<std::string> missing;      // what couldn't be found (logged by the caller)
    std::vector<int> addedBones;
};
HumanoidJiggleSetup addHumanoidSoftTissue(ModelData& model, const HumanoidSoftTissue& options);

// ---------------------------------------------------------------------
// A soft body: a lattice of particles filling a box, kept in shape by
// shape matching over every 2x2x2 cell of the lattice (each cell's best
// rigid fit pulls its 8 particles back; overlapping cells make the whole
// thing act as one piece of jelly that can still dent locally). Balls
// (spheres with their own velocity) collide with it both ways: they dent
// the jelly and the jelly pushes them back, which is what makes them
// bounce.
class JellyBody {
public:
    struct Params {
        glm::vec3 min{ -0.5f, 0.0f, -0.5f }, max{ 0.5f, 0.6f, 0.5f };
        glm::ivec3 cells{ 6, 4, 6 };   // lattice cells per axis (particles = cells + 1)
        float stiffness = 0.35f;       // 0..1 shape-matching pull per iteration
        int iterations = 3;
        float damping = 0.02f;         // velocity lost per step
        float gravity = -9.81f;
        float floorY = 0.0f;
        float floorFriction = 0.9f;    // 0..1: sliding motion lost where a particle touches the floor
        bool pinBottom = true;         // bottom layer glued to the floor (a jelly on a plate)
        float mass = 4.0f;             // kg, whole body
        float stepHz = 120.0f;
    };
    struct Ball {
        glm::vec3 pos{ 0.0f }, vel{ 0.0f };
        float radius = 0.1f;
        float mass = 0.3f;
        float restitution = 0.5f; // extra bounce off the floor
    };

    JellyBody() = default;
    explicit JellyBody(const Params& p);

    void step(float dt, std::vector<Ball>& balls);
    void poke(const glm::vec3& at, const glm::vec3& impulse, float radius);

    // The render surface: a grid of `res` quads per box face, embedded in
    // the lattice (trilinear), smooth normals. Rebuilt by deform().
    void buildSurface(int res);
    void deform();
    const std::vector<glm::vec3>& surfacePositions() const { return m_surfPos; }
    const std::vector<glm::vec3>& surfaceNormals() const { return m_surfNrm; }
    const std::vector<glm::vec2>& surfaceUvs() const { return m_surfUv; }
    const std::vector<uint32_t>& surfaceIndices() const { return m_surfIdx; }

    // Where a point given in the rest shape is now (trilinear in the
    // lattice): things suspended in the jelly move with it.
    glm::vec3 deformedPoint(const glm::vec3& restPosition) const;
    const std::vector<glm::vec3>& particles() const { return m_x; }
    size_t particleCount() const { return m_x.size(); }
    // How far the lattice is from its rest shape: mean particle displacement (m).
    float deformation() const;
    const Params& params() const { return m_p; }
    Params& params() { return m_p; }
    void reset();

private:
    size_t id(int x, int y, int z) const { return (static_cast<size_t>(z) * (m_p.cells.y + 1) + y) * (m_p.cells.x + 1) + x; }
    void substep(float h, std::vector<Ball>& balls);
    Params m_p;
    std::vector<glm::vec3> m_rest, m_x, m_prev, m_goal;
    std::vector<float> m_goalW;
    std::vector<glm::quat> m_cellRot; // warm start for each cell's rotation
    std::vector<uint8_t> m_pinned;
    float m_accum = 0.0f;
    float m_particleMass = 0.0f;
    // surface embedding
    struct Embed { uint32_t cell; glm::vec3 w; };
    std::vector<Embed> m_embed;
    std::vector<glm::vec3> m_surfPos, m_surfNrm;
    std::vector<glm::vec2> m_surfUv;
    std::vector<uint32_t> m_surfIdx;
};

// Rotation part of a 3x3 matrix, iteratively (Mueller et al. 2016, "A
// robust method to extract the rotational part of deformations"), warm
// started from `q`. Exposed for tests.
void extractRotation(const glm::mat3& a, glm::quat& q, int iterations = 8);

} // namespace kke
