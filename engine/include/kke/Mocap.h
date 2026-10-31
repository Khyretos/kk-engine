#pragma once

#include "kke/Animator.h"
#include "kke/ModelAsset.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Motion capture import and clean-up (ENG-F23, docs/MOVEMENT.md "Motion capture").
//
// BVH (Biovision Hierarchy) is the format mocap libraries ship (CMU, Mixamo
// exports, Rokoko, Perception Neuron, Blender): a skeleton with offsets and
// rotation channels, then one line of numbers per frame. A BVH file becomes
// a skeleton-only ModelData with the motion as its clip, so everything that
// takes a model takes it, and the retargeter puts it on any character:
//
//   kke::ModelData mocap = kke::loadBvh("walk.bvh");                 // or loadModel("walk.bvh")
//   kke::BoneMatch match = kke::matchBones(mocap, hero);              // by canonical bone name
//   auto clips = kke::retargetAnimations(mocap, hero, match);         // clips for the hero's skeleton
//
// Capture is noisy: cleanAnimation() fixes quaternion flips, removes spikes,
// smooths, trims, resamples, takes the root's travel out and crossfades the
// end into the start so the clip loops. Unit-tested in tests/test_mocap.cpp.

enum class BvhChannel : uint8_t { Xposition, Yposition, Zposition, Xrotation, Yrotation, Zrotation };

struct BvhJoint {
    std::string name;
    int parent = -1;              // index into BvhMotion::joints; parents come first
    glm::vec3 offset{ 0.0f };     // from the parent, in the file's units
    std::vector<BvhChannel> channels;
    size_t firstChannel = 0;      // where its channels start in a frame
    bool hasEndSite = false;
    glm::vec3 endSite{ 0.0f };
};

struct BvhMotion {
    std::vector<BvhJoint> joints;
    float frameTime = 1.0f / 30.0f;
    size_t frameCount = 0;
    size_t channelCount = 0;
    std::vector<float> values;    // frameCount x channelCount, as in the file
    float rate() const { return frameTime > 0.0f ? 1.0f / frameTime : 30.0f; }
    float at(size_t frame, size_t channel) const { return values[frame * channelCount + channel]; }
};

constexpr size_t kBvhMaxJoints = 512;
constexpr size_t kBvhMaxFrames = 200000;

// False with a reason (and the line) for text that is not a BVH file.
bool parseBvh(const std::string& text, BvhMotion& out, std::string* error = nullptr);

struct BvhImportOptions {
    // File units to metres. 0 guesses: a skeleton over 3 units tall is in
    // centimetres (the usual), otherwise metres.
    float scale = 0.0f;
    bool zUp = false;             // Z is up in the file (Blender, many mocap suites): turned to Y up
    std::string name;             // the clip's name; the file's name, or "mocap"
};

// The skeleton and the motion: bones with the file's offsets as their rest
// pose (BVH rest is the zero-rotation pose, usually a T-pose), one clip
// sampled at the file's frame rate. Joint rotations are applied in the
// order the channels list them, as BVH defines.
ModelData bvhToModel(const BvhMotion& motion, const BvhImportOptions& options = {});
// Reads and converts a file (through the cooked-asset reader, so a game's
// packed files work). Throws std::runtime_error with the reason.
ModelData loadBvh(const std::string& path, const BvhImportOptions& options = {});

struct MotionCleanOptions {
    bool fixFlips = true;          // q and -q are one rotation: keep a bone's neighbours on the same side
    float spikeDegrees = 0.0f;     // > 0: a rotation that jumps this far and straight back is replaced by its neighbours' midpoint
    float spikeMetres = 0.0f;      // the same for translations
    float smoothSeconds = 0.0f;    // > 0: Gaussian smoothing with this sigma
    float trimStart = 0.0f;        // seconds cut from the start
    float trimEnd = 0.0f;          // and from the end
    float resampleRate = 0.0f;     // > 0: frames per second of the result
    int inPlaceBone = -1;          // takes this bone's horizontal travel out (the root); its height stays
    float loopBlendSeconds = 0.0f; // > 0: the end crossfades into the start (for a clip that returns to its pose); the clip gets shorter by this
};

struct MotionCleanReport {
    size_t framesBefore = 0, framesAfter = 0;
    size_t spikesFixed = 0;
    glm::vec3 travel{ 0.0f };      // the in-place bone's horizontal travel, first frame to last, before it was removed
    float sampleRate = 0.0f;
};

// In order: trim, flips, spikes, smoothing, resampling, in place, loop.
std::vector<Pose> cleanMotion(const std::vector<Pose>& frames, float sampleRate, const MotionCleanOptions& options, MotionCleanReport* report = nullptr);
// A model clip (matrices) cleaned; its rate and duration follow.
ModelAnimation cleanAnimation(const ModelAnimation& clip, const MotionCleanOptions& options, MotionCleanReport* report = nullptr);

} // namespace kke
