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

private:
    struct Clip {
        std::string name;
        float duration = 0.0f, sampleRate = 30.0f;
        std::vector<Pose> frames;
    };
    std::vector<Clip> m_clips;
    Pose m_rest;
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
    // The blend-space parameter (e.g. ground speed in m/s).
    void setParameter(float value) { m_param = value; }

    void update(float dt);
    const Pose& pose() const { return m_pose; }

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
    Pose m_pose, m_scratchA, m_scratchB;
};

} // namespace kke
