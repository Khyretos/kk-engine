#pragma once

#include "kke/ModelAsset.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace kke {

// Character animation on the CPU: sampling, blending, 1D blend spaces and a
// crossfading state machine — the part of Unreal's AnimGraph / Unity's
// Animator that every game needs first. Pure data (no GPU), unit-tested;
// the result goes into ModelModule::boneLocals().
//
// Poses are per-bone translation / rotation / scale, not matrices:
// rotations must be interpolated as rotations (slerp), which blending
// matrices directly gets wrong (they shrink halfway between two poses).

struct BoneTRS {
    glm::vec3 t{0.0f};
    glm::quat r{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 s{1.0f};
};
using Pose = std::vector<BoneTRS>;

void blendPoses(const Pose& a, const Pose& b, float weight, Pose& out); // weight 0 = a, 1 = b
void poseToLocals(const Pose& pose, std::vector<glm::mat4>& locals);

// A model's clips converted once to TRS frames (decomposing matrices every
// frame would cost more than the blending itself).
class AnimationSet {
public:
    AnimationSet() = default;
    explicit AnimationSet(const ModelData& model);

    size_t boneCount() const { return m_rest.size(); }
    size_t clipCount() const { return m_clips.size(); }
    // First clip whose name contains `part` (e.g. "Walk_Loop"), or -1.
    int find(const std::string& part) const;
    const std::string& clipName(int clip) const { return m_clips[clip].name; }
    float duration(int clip) const { return m_clips[clip].duration; }
    const Pose& restPose() const { return m_rest; }
    // Pose at `time` seconds (wrapped when looping, clamped otherwise).
    void sample(int clip, float time, bool loop, Pose& out) const;

    // Root motion (Unreal's "root motion from animation", as data): the
    // horizontal travel of `bone` (the root, or the pelvis for clips made
    // in place-less style) moves out of every clip into a track, so the
    // clip plays in place and the game decides where the capsule goes.
    // `model` gives the bone's parents (assumed not animated).
    void extractRootMotion(const ModelData& model, int bone);
    // Vertical travel (a climb or vault authored in place: the pelvis goes
    // up and over, then the clip snaps back) moves out of the clips whose
    // name contains `clipPart`, so a controller that moves the capsule up
    // doesn't lift the body twice. Returns how many clips it changed.
    int removeLift(const ModelData& model, int bone, const std::string& clipPart);
    bool hasRootMotion() const { return m_rootBone >= 0; }
    // Model-space travel between two clip times (seconds, unwrapped:
    // a looping clip adds a whole cycle's travel per wrap).
    glm::vec3 rootTravel(int clip, float from, float to, bool loop) const;

private:
    struct Clip {
        std::string name;
        float duration = 0.0f, sampleRate = 30.0f;
        std::vector<Pose> frames;
        std::vector<glm::vec3> root; // travel since frame 0, per frame (root motion)
    };
    glm::vec3 rootAt(const Clip& c, float time) const; // time within [0, duration]
    std::vector<Clip> m_clips;
    Pose m_rest;
    int m_rootBone = -1;
};

// Clips spread along one parameter (usually speed): idle at 0, walk at
// 1.4 m/s, jog at 3, sprint at 6. The two nearest are blended, and all
// run at a shared *phase* so feet land together (no foot sliding when
// walk and jog have different lengths).
struct BlendSpace1D {
    struct Point { int clip; float value; };
    std::vector<Point> points; // sorted by value
};

class Animator {
public:
    explicit Animator(const AnimationSet& set);

    int addClipState(const std::string& name, int clip, bool loop = true, float speed = 1.0f);
    int addBlendState(const std::string& name, BlendSpace1D space, bool loop = true);
    int findState(const std::string& name) const;

    // Switch state, crossfading over `fade` seconds. Re-playing the current
    // state does nothing unless `restart`.
    void play(int state, float fade = 0.2f, bool restart = false);
    int current() const { return m_current; }
    // Seconds spent in the current state, and whether a non-looping
    // state has reached its end (e.g. "Jump_Land" done -> back to move).
    float stateTime() const { return m_time; }
    bool finished() const;
    // Moves the controller times (a vault or climb lasts as long as the
    // obstacle needs): pose the current clip state at `fraction` (0..1) of
    // its length; the next update() stays there instead of advancing.
    void setProgress(float fraction);
    // The blend-space parameter (e.g. ground speed in m/s).
    void setParameter(float value) { m_param = value; }

    void update(float dt);
    const Pose& pose() const { return m_pose; }
    // Model-space root travel during the last update(), from clip states
    // (blend spaces are locomotion: the controller moves those). Zero
    // unless the set has root motion.
    glm::vec3 rootMotion() const { return m_rootDelta; }

private:
    struct State {
        std::string name;
        int clip = -1;
        BlendSpace1D space;
        bool loop = true;
        float speed = 1.0f;
    };
    void evaluate(const State& s, float time, float& phase, Pose& out) const;
    float stateDuration(const State& s) const;

    const AnimationSet* m_set;
    std::vector<State> m_states;
    int m_current = -1, m_previous = -1;
    float m_time = 0.0f, m_prevTime = 0.0f;
    float m_phase = 0.0f, m_prevPhase = 0.0f;
    float m_fade = 0.0f, m_fadeLength = 0.0f;
    float m_param = 0.0f;
    bool m_held = false; // setProgress() since the last update()
    glm::vec3 m_rootDelta{0.0f};
    Pose m_pose, m_scratchA, m_scratchB;
};

} // namespace kke
