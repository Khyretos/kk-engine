#pragma once

#include "kke/Animator.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kke {

// Animation compression (ENG-F63, docs/PROCEDURAL_ANIMATION.md "Compression").
//
// A baked clip is a 4x4 matrix per bone per frame: 64 bytes x 60 bones x 30
// frames a second is 115 KB for every second of animation. Most of it is
// redundant: a bone's rotation moves along smooth curves, and a finger that
// doesn't move is the same value 30 times a second. Packing a clip
//
//   1. splits it into translation, rotation and scale tracks per bone,
//   2. keeps only the frames a track needs: a key is dropped when the curve
//      through its neighbours stays within a tolerance of it (Ramer-Douglas-
//      Peucker over time; rotations measured as the angle between them),
//      and a track that never moves is one key,
//   3. stores the keys as 16-bit numbers: translations and scales over the
//      track's own range, rotations as the three smaller components of the
//      unit quaternion (the "smallest three" scheme).
//
// Sampling gives back the same poses AnimationSet::sample gave, within the
// tolerance, and a packed clip serializes to a compact file (cooked art).
// Unit-tested in tests/test_anim_compress.cpp.

struct AnimCompressionOptions {
    float positionTolerance = 0.0005f;        // metres: 0.5 mm
    float rotationToleranceDegrees = 0.05f;
    float scaleTolerance = 0.0005f;
    bool quantize = true;                     // false keeps 32-bit floats (about twice the size)
};

struct AnimCompressionStats {
    size_t rawBytes = 0;          // the clip as 4x4 matrices per bone per frame
    size_t packedBytes = 0;       // what it serializes to
    size_t rawKeys = 0;           // frames x bones x 3 channels
    size_t keys = 0;
    float maxPositionError = 0.0f;          // against the original frames, over every frame
    float maxRotationErrorDegrees = 0.0f;
    float maxScaleError = 0.0f;
    float ratio() const { return packedBytes ? static_cast<float>(rawBytes) / static_cast<float>(packedBytes) : 0.0f; }
    AnimCompressionStats& operator+=(const AnimCompressionStats& o);
};

class PackedClip {
public:
    std::string name;
    float duration = 0.0f;
    float sampleRate = 30.0f;

    size_t boneCount() const { return m_bones.size(); }
    size_t frameCount() const { return m_frames; }
    bool empty() const { return m_frames == 0; }
    bool quantized() const { return m_quantized; }

    // The pose at a whole frame, as the curves give it.
    void frame(size_t f, Pose& out) const;
    // The pose at `time` seconds, the way AnimationSet::sample plays a clip:
    // the two frames around it blended (wrapping to the first when looping).
    void sample(float time, bool loop, Pose& out) const;
    // Every frame (to edit a clip again).
    std::vector<Pose> unpack() const;

    size_t keyCount() const;
    size_t byteSize() const; // what serialize() writes
    std::vector<uint8_t> serialize() const;
    // False with a reason for bytes that are not a packed clip (nothing is
    // trusted: sizes, orders and ranges are all checked).
    static bool deserialize(const std::vector<uint8_t>& bytes, PackedClip& out, std::string* error = nullptr);

    static constexpr size_t kMaxFrames = 65535; // key frames are 16 bits

private:
    friend bool packFrames(const std::vector<Pose>&, const std::string&, float, float, const AnimCompressionOptions&, PackedClip&,
                           AnimCompressionStats*, std::string*);
    struct Channel {
        std::vector<uint16_t> frames;  // frames holding a key, ascending, the first is 0
        std::vector<float> raw;        // 3 per key (a rotation: x, y, z, w x 4) when not quantized
        std::vector<uint16_t> quant;   // 3 per key when quantized
        std::vector<uint8_t> largest;  // rotations: the component left out (0-3)
        float lo[3] = { 0, 0, 0 };     // translations and scales: value = lo + quant * step
        float step[3] = { 0, 0, 0 };
        size_t keys() const { return frames.size(); }
    };
    struct Track {
        Channel pos, rot, scl;
    };
    glm::vec3 vec3At(const Channel& c, size_t key) const;
    glm::quat quatAt(const Channel& c, size_t key) const;
    template <typename T, typename Get, typename Mix>
    T evaluate(const Channel& c, size_t frame, Get get, Mix mix) const;

    std::vector<Track> m_bones;
    size_t m_frames = 0;
    bool m_quantized = true;
};

// Packs TRS frames (frames[f][bone]). False with a reason for an empty clip
// or one over kMaxFrames. `stats` gets the sizes and the largest error
// against the original frames.
bool packFrames(const std::vector<Pose>& frames, const std::string& name, float duration, float sampleRate, const AnimCompressionOptions& options,
                PackedClip& out, AnimCompressionStats* stats = nullptr, std::string* error = nullptr);

// A model clip (matrices) packed.
bool packAnimation(const ModelAnimation& clip, const AnimCompressionOptions& options, PackedClip& out, AnimCompressionStats* stats = nullptr,
                   std::string* error = nullptr);

} // namespace kke
