#pragma once

#include "kke/ModelAsset.h"

#include <cstdint>
#include <vector>

namespace kke {

// Animation compression (ENG-F63, docs/ANIMATION_COMPRESSION.md). A clip is stored as a matrix per bone per frame (64 bytes
// each: 43 clips of a 66 bone mannequin are 16 MB). Packed, each bone is three tracks (rotation, translation, scale) of only the
// keys that matter: a key is dropped when the line between its neighbours is within the tolerance of what it was (a straight
// walk cycle needs a few keys per bone, not 30 a second), a track that never moves is one key, rotations are the "smallest
// three" components of a unit quaternion in 16 bits each, translations 16 bits per axis within the track's own range.
// Sampling gives back the matrix a frame had, to within the tolerance, without ever unpacking the whole clip.
struct AnimCompressOptions {
    float rotationError = 0.0005f;    // radians (about 0.03 degrees): the most a rotation may be off after dropping keys
    float translationError = 0.0005f; // metres (half a millimetre)
    float scaleError = 0.001f;
};

struct PackedAnimation {
    // Smallest-three: the three components that are not the largest, each as an int16 over +-1/sqrt(2); the largest is
    // rebuilt from the unit length, and kept positive (a quaternion and its negative are the same rotation).
    struct Quat48 {
        int16_t a = 0, b = 0, c = 0;
        uint8_t largest = 3; // which component is rebuilt: 0 x, 1 y, 2 z, 3 w
    };
    struct Vec3u16 {
        uint16_t x = 0, y = 0, z = 0;
    };
    struct Track {
        std::vector<uint16_t> rotFrames; // the frames the keys are at (ascending); one key = constant
        std::vector<Quat48> rot;
        std::vector<uint16_t> posFrames;
        std::vector<Vec3u16> pos;        // within [posMin, posMin + posRange] per axis ...
        std::vector<glm::vec3> posFull;  // ... or, for a track too wide for 16 bits to hold a fraction of the tolerance, full floats
        glm::vec3 posMin{0.0f}, posRange{0.0f};
        std::vector<uint16_t> sclFrames;
        std::vector<glm::vec3> scl;
    };
    std::vector<Track> tracks; // per bone
    uint32_t frameCount = 0;

    size_t bytes() const; // what it holds, for comparing with frames: frameCount * bones * 64
    // The local matrices of bone `bone` at a whole frame.
    glm::mat4 local(size_t bone, uint32_t frame) const;
};

// One matrix per bone per frame in, the packed form out. Matrices must be translation, rotation and scale (no shear),
// as every skeleton's are.
PackedAnimation packFrames(const std::vector<std::vector<glm::mat4>>& frames, const AnimCompressOptions& options = {});
// Back to a matrix per bone per frame (what sampling does, for every frame).
std::vector<std::vector<glm::mat4>> unpackFrames(const PackedAnimation& packed);

struct AnimCompressStats {
    size_t clips = 0, denseBytes = 0, packedBytes = 0;
    float ratio() const { return packedBytes ? static_cast<float>(denseBytes) / static_cast<float>(packedBytes) : 1.0f; }
};
// Packs every clip of a model in place: `frames` is emptied and `packed` set (sampling through ModelAnimation
// still works the same). Clips that are already packed, or whose size would not shrink, stay as they are.
AnimCompressStats compressAnimations(ModelData& model, const AnimCompressOptions& options = {});

} // namespace kke
