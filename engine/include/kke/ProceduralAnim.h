#pragma once

#include "kke/AnimRig.h"
#include "kke/Animator.h"
#include "kke/ModelAsset.h"
#include "kke/Ragdoll.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <string>
#include <vector>

namespace kke {

// Procedural animation: motion worked out every frame instead of played
// from a clip. docs/PROCEDURAL_ANIMATION.md is the guide; this header is
// the reference. Pure CPU and physics-agnostic (the active ragdoll talks
// to IRagdollPhysics), unit-tested in tests/test_procedural_anim.cpp.
//
// Everything here is a *layer*: it takes a Pose (from the Animator, or the
// rest pose when a creature has no clips) and changes it, with a weight,
// in a fixed order:
//
//   clips (Animator) -> layered clips (blendPosesMasked / addPose)
//     -> legs (ProceduralGait + applyGait, or LegPlacer on clips)
//     -> look-at / aim (LookAt) -> reach (solveTwoBone / solveFabrik)
//     -> secondary motion (JigglePhysics) -> active ragdoll (physics)
//
// The algorithms are the standard published ones, not inventions: FABRIK
// (Aristidou & Lasenby 2011), the Raibert foot-placement heuristic (1986),
// gait phase tables and Froude-number gait changes (Alexander 1984;
// Hildebrand), and motor-driven active ragdolls the way Jolt's own
// JPH::Ragdoll::DriveToPoseUsingMotors does it.

// =====================================================================
// Layering clips with each other and with procedural motion

// Per-bone weight 0..1 (index = bone). Empty = every bone at 1.
using BoneMask = std::vector<float>;

// The bones named in `roots` (canonicalBoneName matching, so "Spine_01"
// finds "spine_01") and, if `children`, everything under them, at
// `weight`; every other bone 0. Unknown names are skipped.
BoneMask boneMask(const ModelData& model, const std::vector<std::string>& roots, float weight = 1.0f, bool children = true);

// out = base blended toward `layer` bone by bone, by weight * mask[bone]
// (an upper-body wave over a walk: mask = boneMask(model, {"spine_01"})).
// `out` may be `base`.
void blendPosesMasked(const Pose& base, const Pose& layer, const BoneMask& mask, float weight, Pose& out);

// Additive layer: what `additive` changes relative to `reference` (a
// breathing or flinch clip against its own first frame), added on top of
// `base`, times weight * mask. `out` may be `base`.
void addPose(const Pose& base, const Pose& additive, const Pose& reference, const BoneMask& mask, float weight, Pose& out);

// =====================================================================
// Look-at and aim

// Turns a chain of bones (spine to head, or neck to head for an animal)
// so the last one faces a target, each bone taking its share of the turn.
// Yaw and pitch are limited and smoothed, and a target far behind is
// dropped (the head comes back to forward instead of snapping round).
class LookAt {
public:
    struct Settings {
        float maxYaw = 80.0f;       // degrees left/right, the whole chain together
        float maxPitch = 50.0f;     // degrees up/down
        float giveUpYaw = 130.0f;   // target further round than this: look forward again
        float speed = 6.0f;         // 1/s: how fast the head turns toward where it wants
    };
    struct Link {
        int bone = -1;
        float share = 1.0f;         // relative; the shares are normalised
    };

    LookAt() = default;
    // `forward`: which way the head faces in model space at rest (for most
    // characters modelForward(model); an aim chain can use the weapon's
    // direction instead).
    LookAt(std::vector<Link> chain, const glm::vec3& forward, const Settings& settings = Settings{});
    // spine_02, spine_03, neck_01, head (the ones present), shares
    // 0.15 / 0.2 / 0.3 / 0.35. Invalid if there is no head.
    static LookAt humanoid(const ModelData& model, const Settings& settings = Settings{});
    // Neck and head of a QuadrupedBones rig (Quaternius names by default).
    static LookAt quadruped(const ModelData& model, const QuadrupedBones& bones = QuadrupedBones{},
                            const Settings& settings = Settings{});
    bool valid() const { return !m_chain.empty() && m_chain.back().bone >= 0; }

    // `target` in model space; nullptr = nothing to look at (back to
    // forward). `weight` 0..1 fades the whole layer.
    void apply(const ModelData& model, Pose& pose, const glm::vec3* target, float dt, float weight = 1.0f);

    float yawDegrees() const { return m_yaw; }     // current, smoothed, + = to the character's left
    float pitchDegrees() const { return m_pitch; } // + = up
    Settings& settings() { return m_s; }

private:
    std::vector<Link> m_chain;
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};
    Settings m_s;
    float m_yaw = 0.0f, m_pitch = 0.0f;
};

// =====================================================================
// N-bone IK (FABRIK), for chains two-bone IK can't do: tails, necks,
// tentacles, spider and insect legs with three or more segments.

struct IkChain {
    std::vector<int> bones; // root first, end effector last
    bool valid() const { return bones.size() >= 2; }
};
// The bones from `root` down to `end` (end must be below root).
IkChain findIkChain(const ModelData& model, const std::string& root, const std::string& end);

struct FabrikSettings {
    int iterations = 12;
    float tolerance = 1e-3f; // metres: close enough
};
// Moves the chain's end to `target` (model space), bone lengths kept.
// `pole` (optional, model space) is where the bends point. `weight`
// blends from the current pose.
void solveFabrik(const ModelData& model, Pose& pose, const IkChain& chain, const glm::vec3& target, const glm::vec3* pole = nullptr,
                 float weight = 1.0f, const FabrikSettings& settings = FabrikSettings{});
// The same on bare points (joint positions, root first), for creatures
// drawn from primitives: `points` is changed in place.
void fabrikPoints(std::vector<glm::vec3>& points, const glm::vec3& target, const glm::vec3* pole = nullptr,
                  const FabrikSettings& settings = FabrikSettings{});

// Where the knee (or elbow) of a two-segment limb goes: `hip` to `foot`
// with segment lengths `upper` and `lower`, bending toward `bend`.
glm::vec3 kneePosition(const glm::vec3& hip, const glm::vec3& foot, float upper, float lower, const glm::vec3& bend);

// =====================================================================
// Gaits: which legs are in the air when

enum class Gait {
    Walk,   // four-beat, lateral sequence (every quadruped's slow gait)
    Trot,   // diagonal pairs (dogs, horses at a jog)
    Pace,   // same-side pairs (camels, llamas, giraffes)
    Canter, // three-beat
    Gallop, // four-beat, rotary, with a moment in the air
    Bound,  // front pair, then back pair (small animals, rabbits)
    Pronk,  // all together (springbok, a happy lamb)
    Tripod, // six legs: two alternating tripods (insects at speed)
    Wave,   // one leg at a time, back to front along each side (slow insects, spiders)
    Auto,   // pick from speed (gaitForSpeed)
};
const char* gaitName(Gait gait);
// "walk", "trot", ... (any case); Auto if unknown.
Gait gaitFromName(const std::string& name);

// When each leg lifts: leg i is on the ground for the first `duty` of the
// cycle after `phase[i]` and in the air for the rest.
struct GaitPattern {
    Gait gait = Gait::Walk;
    std::vector<float> phase; // per leg, 0..1
    float duty = 0.6f;        // share of the cycle each foot is on the ground
};
// Legs are numbered front to back, left then right: two legs L, R; four
// legs FL, FR, BL, BR (as QuadrupedBones); six L1, R1, L2, R2, L3, R3; and
// so on. Gaits that need four legs fall back sensibly (a biped "trots" by
// running, a hexapod "trots" as a tripod).
GaitPattern gaitPattern(Gait gait, int legCount);

// v^2 / (g h): speed relative to leg length. Animals of every size change
// gait at about the same Froude number (Alexander 1984).
float froudeNumber(float speed, float hipHeight);
// Walk below Fr 0.5, trot to 2.5, gallop above (four legs); bipeds walk
// then run ("trot"); six and more legs wave, then tripod.
Gait gaitForSpeed(float speed, float hipHeight, int legCount);

// =====================================================================
// Procedural walking for any number of legs

struct LegDesc {
    glm::vec3 hip{0.0f};      // body space: +Z forward, +Y up, origin on the ground under the body
    glm::vec3 restFoot{0.0f}; // body space, where the foot stands at rest (usually y = 0)
    float upper = 0.25f;      // segment lengths (the reach is their sum)
    float lower = 0.25f;
    float length() const { return upper + lower; }
};

// Mirror-pairs of legs for a body `length` long and `width` wide with
// hips `hipHeight` up: 2 = biped, 4 = quadruped, 6 = insect, 8 = spider
// (the spider's feet spread wider than its hips).
std::vector<LegDesc> makeLegs(int count, float length, float width, float hipHeight);

// Plans footsteps and the body's sway for a creature whose controller
// moves it (velocity, turning); nothing here moves the creature itself.
// Each foot stays planted in the world until its turn in the gait, then
// swings in an arc to where it will be needed (the Raibert heuristic:
// under the hip at landing, plus half a stride in the direction of
// travel), found by asking the ground. The body rises and settles with
// the ground under its feet, pitches and rolls to their plane, and leans
// into turns.
class ProceduralGait {
public:
    // A ray straight down from `from` (world space): the ground's point
    // and normal. False = no ground there.
    using SurfaceQuery = std::function<bool(const glm::vec3& from, glm::vec3& hit, glm::vec3& normal)>;

    struct Settings {
        Gait gait = Gait::Auto;
        float stepHeight = 0.25f;  // swing arc, as a share of leg length
        float strideScale = 0.8f;  // longest ground stride of one foot, as a share of leg length
        float cycleFast = 0.2f;    // shortest cycle, in pendulum periods of the leg (2 pi sqrt(L / g))
        float cycleSlow = 0.6f;    // longest cycle, likewise
        float resettle = 0.12f;    // standing still: a foot further than this share of leg length from rest steps back
        float bodyFollow = 1.0f;   // 0..1 how much the body pitches/rolls to the feet
        float bob = 0.04f;         // body lift at push-off, as a share of leg length
        float lean = 0.5f;         // 0..1 leaning into turns (1 = like a cyclist)
        float maxTilt = 25.0f;     // degrees of pitch or roll, at most
        float smoothing = 10.0f;   // 1/s for the body's height and tilt
    };

    struct Foot {
        glm::vec3 position{0.0f};         // world
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        glm::vec3 hip{0.0f};              // world, from bodyPose()
        bool planted = true;
        float swing = 0.0f;               // 0..1 through the current swing (0 when planted)
        bool landed = false;              // touched down during the last update (footstep sound, dust)
    };

    ProceduralGait() = default;
    explicit ProceduralGait(std::vector<LegDesc> legs, const Settings& settings = Settings{});
    bool valid() const { return !m_legs.empty(); }
    int legCount() const { return static_cast<int>(m_legs.size()); }
    const LegDesc& leg(int i) const { return m_legs[i]; }

    // Plants every foot at rest under `body` (world transform: position on
    // the ground, rotation = heading). Also call after a teleport.
    void reset(const glm::mat4& body, const SurfaceQuery& ground);
    // `body` is where the controller has the creature now, `velocity` its
    // world velocity (m/s), `turnRate` its yaw speed (degrees/s, + = left).
    void update(const glm::mat4& body, const glm::vec3& velocity, float turnRate, const SurfaceQuery& ground, float dt);

    const Foot& foot(int i) const { return m_feet[i]; }
    // The body as drawn: `body` from update() raised or lowered to the
    // ground under the feet, bobbing, pitched, rolled and leaning.
    const glm::mat4& bodyPose() const { return m_bodyPose; }
    // World knee position of leg i (for drawing a creature from primitives).
    glm::vec3 knee(int i) const;

    Gait gait() const { return m_pattern.gait; }
    float cycleSeconds() const { return m_cycle; }
    float phase() const { return m_phase; }
    Settings& settings() { return m_s; }
    const Settings& settings() const { return m_s; }

private:
    struct LegState {
        glm::vec3 from{0.0f};  // where the swing started
        float swingTime = 0.0f, swingLength = 0.0f;
        float lastPhase = 0.0f; // the leg's own phase last update, to see it cross into swing
    };
    glm::vec3 stepTarget(int i, const glm::mat4& body, const glm::vec3& velocity, float turnRate, float ahead,
                         const SurfaceQuery& ground, glm::vec3& normal) const;
    void updateBody(const glm::mat4& body, const glm::vec3& velocity, float turnRate, float dt);

    std::vector<LegDesc> m_legs;
    std::vector<Foot> m_feet;
    std::vector<LegState> m_state;
    Settings m_s;
    GaitPattern m_pattern;
    float m_phase = 0.0f, m_cycle = 1.0f;
    float m_hipHeight = 0.5f, m_legLength = 0.5f;
    bool m_moving = false;
    float m_height = 0.0f, m_pitch = 0.0f, m_roll = 0.0f, m_bobNow = 0.0f;
    glm::mat4 m_bodyPose{1.0f};
};

// ---------------------------------------------------------------------
// Driving a skeleton from a ProceduralGait

// Leg chains of a QuadrupedBones rig, FL, FR, BL, BR (upper, lower, foot).
std::vector<TwoBoneChain> quadrupedLegChains(const ModelData& model, const QuadrupedBones& bones = QuadrupedBones{});
// Legs as the gait wants them, from the skeleton's rest pose: hips and
// feet in model space (with `modelToBody` taking model space to body
// space, usually identity or a turn to face +Z), segment lengths from the
// bones. Chains that aren't valid are skipped.
std::vector<LegDesc> legsFromSkeleton(const ModelData& model, const std::vector<TwoBoneChain>& chains,
                                      const glm::mat4& modelToBody = glm::mat4(1.0f));
// Poses the skeleton to what `gait` says: `bodyBone` (the pelvis / hips)
// moves and turns by the gait's body sway, and two-bone IK puts each foot
// on gait.foot(i). `modelWorld` is the model instance's world transform,
// which should follow the controller's body (not gait.bodyPose()). Knees
// keep bending the way they bend at rest (front knees and hind hocks
// opposite ways). `weight` fades it.
void applyGait(const ModelData& model, Pose& pose, const std::vector<TwoBoneChain>& chains, int bodyBone,
               const ProceduralGait& gait, const glm::mat4& modelWorld, float weight = 1.0f);

// ---------------------------------------------------------------------
// Clip-driven legs on uneven ground (FootPlacer for any number of legs)

// Each foot keeps its animated height above the ground under it; the
// body drops as far as the lowest foot needs and pitches / rolls to the
// ground; two-bone IK bends the legs. For animals playing walk and run
// clips across hills.
class LegPlacer {
public:
    using SurfaceQuery = FootPlacer::SurfaceQuery; // model space

    struct Settings {
        float maxDrop = 0.4f;     // metres the body may go down (scaled to the rig's hip height if <= 0)
        float maxRaise = 0.4f;    // metres a foot may go up
        float probeUp = 0.5f;     // ground ray starts this far above the foot
        float smoothing = 12.0f;  // 1/s
        float bodyFollow = 1.0f;  // 0..1 how much the body pitches/rolls to the ground
        float maxTilt = 25.0f;    // degrees
    };

    LegPlacer() = default;
    LegPlacer(std::vector<TwoBoneChain> legs, int bodyBone, const Settings& settings = Settings{});
    bool valid() const;

    void apply(const ModelData& model, Pose& pose, const SurfaceQuery& ground, float dt, float weight = 1.0f);
    float bodyOffset() const { return m_bodyOffset; }
    float pitchDegrees() const { return m_pitch; }
    float rollDegrees() const { return m_roll; }
    Settings& settings() { return m_s; }

private:
    std::vector<TwoBoneChain> m_legs;
    int m_body = -1;
    Settings m_s;
    std::vector<float> m_footOffset;
    float m_bodyOffset = 0.0f, m_pitch = 0.0f, m_roll = 0.0f;
};

// =====================================================================
// Active ragdoll: physics that tries to follow the animation

// What IRagdollPhysics::driveRagdoll is asked to do for one step.
struct RagdollDrive {
    std::vector<glm::mat4> targets;     // per body: where the animation has it (world)
    std::vector<float> jointStrength;   // per joint, 0 = limp .. 1 = full muscle
    float torquePerKg = 40.0f;          // full-strength motor torque: N*m per kg of the joint's heavier body
    float frequency = 8.0f;             // Hz: how stiffly a joint springs toward its target
    int assistBody = -1;                // a body pulled straight onto its target ("hand of god"), usually the pelvis
    float assist = 0.0f;                // 0..1 how hard
};

// Hit reactions and balance on top of a ragdoll whose joints have motors
// (Jolt: RigidBodyModule; FEMFX ragdolls have none and stay limp).
//
//   Animated -- hit() --> Active: the ragdoll follows the animation with
//                         muscles weakened around the hit, which recover
//   Active   -- steady --> Animated (hand back to clips, blendPoses)
//   Active   -- off balance --> Fallen (limp) -- getUpDelay --> GettingUp
//
// The game owns the ragdoll (IRagdollPhysics::createRagdoll when this goes
// Active, destroyRagdoll when it is back to Animated) and skins from it
// while physical() is true (poseFromRagdoll).
class ActiveRagdoll {
public:
    enum class State { Animated, Active, Fallen, GettingUp };

    struct Settings {
        float recoverPerSecond = 0.9f; // muscle strength regained per second
        float hitWeakening = 0.12f;    // strength lost per m/s of push at the body hit
        int hitSpread = 2;             // joints away from the hit that also weaken (halving each step)
        float minStrength = 0.15f;     // a hit never takes a joint below this (0 = can go limp)
        float balanceAssist = 1.0f;    // pull on the pelvis toward the animation at full balance
        float balanceLoss = 0.25f;     // balance lost per m/s of push anywhere
        float balanceRecover = 0.8f;   // per second
        float fallTilt = 55.0f;        // degrees the torso may lean from its target before falling
        float fallDrop = 0.35f;        // share of the pelvis's height it may sink before falling
        float calmSeconds = 0.6f;      // steady this long at full strength = back to Animated
        float getUpDelay = 1.6f;       // seconds lying down before GettingUp
        float getUpSeconds = 0.6f;     // blend from the ragdoll back to the clip
    };

    ActiveRagdoll() = default;
    ActiveRagdoll(const RagdollDesc& desc, const Settings& settings = Settings{});
    bool valid() const { return !m_bodyJoint.empty(); }

    // Body targets from the animated skeleton (bone world transforms,
    // e.g. instanceTransform * poseToModel), through the binding the skin
    // uses (bindSkeletonToRagdoll).
    void setTargets(const RagdollSkinBinding& binding, const std::vector<glm::mat4>& boneWorld);
    void setTargets(std::vector<glm::mat4> bodyWorld) { m_targets = std::move(bodyWorld); }
    const std::vector<glm::mat4>& targets() const { return m_targets; }

    // A push (m/s, like IRagdollPhysics::pushRagdollBody) on one body:
    // weakens the joints around it and knocks the balance. Goes Active if
    // Animated. The game also gives the body the push itself.
    void hit(int body, const glm::vec3& push);
    // Everything limp now (a big blow, a fall from a height).
    void knockOut();
    // `bodyWorld`: the ragdoll's bodies as the physics has them now
    // (ignored while Animated). Advances strengths, balance and state.
    void update(float dt, const std::vector<glm::mat4>& bodyWorld);
    // What to send the physics this step (empty targets while Animated).
    RagdollDrive drive() const;

    State state() const { return m_state; }
    // The ragdoll exists and the skin should follow it (Active, Fallen, GettingUp).
    bool physical() const { return m_state != State::Animated; }
    // GettingUp: 0..1 blend from the ragdoll pose back to the clip
    // (blendPoses(ragdollPose, animPose, getUpBlend())).
    float getUpBlend() const;
    float jointStrength(int joint) const { return m_strength[joint]; }
    float balance() const { return m_balance; }
    // Seconds in the current state.
    float stateTime() const { return m_time; }

private:
    void enter(State s);
    RagdollDesc m_desc;
    Settings m_s;
    State m_state = State::Animated;
    std::vector<glm::mat4> m_targets;
    std::vector<float> m_strength;          // per joint
    std::vector<int> m_bodyJoint;           // per body: the joint that moves it (bodyB), -1 = root
    std::vector<std::vector<int>> m_adjacent; // per joint: joints sharing a body
    int m_pelvis = -1, m_torso = -1;
    float m_restPelvisHeight = 1.0f;
    float m_balance = 1.0f, m_time = 0.0f, m_calm = 0.0f;
};

// Body world transforms for a pose: the inverse of poseFromRagdoll, body
// by body from the first bone that rides it.
std::vector<glm::mat4> ragdollTargetsFromPose(const RagdollSkinBinding& binding, const std::vector<glm::mat4>& boneWorld,
                                              size_t bodyCount);

} // namespace kke
