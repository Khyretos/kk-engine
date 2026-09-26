#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kke {

// Mono PCM, float -1..1. Every sound the mixer plays is one of these:
// synthesized impacts (kke::ImpactSynth) or decoded files (AudioModule).
struct SoundBuffer {
    std::vector<float> samples;
    int sampleRate = 48000;
    float seconds() const { return sampleRate > 0 ? float(samples.size()) / float(sampleRate) : 0.0f; }
};
using SoundHandle = std::shared_ptr<const SoundBuffer>;

// What a sound *is*, for the accessibility visualizer and captions, and
// for per-category volume. Every sound carries one (AUDIO.md).
enum class SoundCategory : uint8_t { Impact, Footstep, Voice, Ambient, Ui, Music, Alert, Count };
const char* soundCategoryName(SoundCategory c);

struct VoiceDesc {
    SoundHandle sound;
    glm::vec3 position{0.0f};
    bool spatial = true;          // false: plays centered, no distance (UI, music)
    float gain = 1.0f;
    float minDistance = 1.0f;     // full volume inside this radius
    float maxDistance = 60.0f;    // silent (and culled) beyond this
    float priority = 1.0f;        // multiplies loudness when choosing a voice to steal
    bool loop = false;
    SoundCategory category = SoundCategory::Impact;
    uint32_t material = 0;        // game/audio material id, for captions and the visualizer
};

// The ears. forward/up need not be normalized.
struct Listener {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

// One sound that is playing right now, as the visualizer sees it.
struct ActiveSound {
    uint32_t id = 0;
    glm::vec3 position{0.0f};
    bool spatial = true;
    float loudness = 0.0f;        // peak of the last mixed block after distance/occlusion, 0..~1
    float azimuth = 0.0f;         // radians, 0 = straight ahead, +pi/2 = right
    float distance = 0.0f;
    float transmission = 1.0f;    // 1 = clear path, lower = behind something
    SoundCategory category = SoundCategory::Impact;
    uint32_t material = 0;
};

// A small software mixer: N mono voices -> interleaved stereo float.
//
// Why our own and not miniaudio's engine/node graph: the mix is the part
// the accessibility layer, the occlusion rays and the benchmarks all need
// to look inside, and it has to run with no audio device at all (CI, the
// 1-core VM, servers) and give the same result every time for tests. It's
// ~200 lines of plain loops: per voice a constant-power pan, an inverse-
// distance gain, and a one-pole low-pass whose cutoff drops as the path to
// the listener gets blocked (walls muffle highs first) and as the sound
// moves behind the listener (a cheap front/back cue). miniaudio only
// supplies the output device (AudioModule).
//
// Budget (OPTIMIZATION.md rule 5): at most maxVoices at once; a new sound
// steals the quietest voice (loudness x priority) or is dropped if it
// would be the quietest itself.
//
// Threading: play/stop/setListener/setTransmission are called from the
// game thread, mix() from the audio device's thread; one mutex guards the
// voice list. The lock is held for one block (~5 ms of audio) at a time;
// a lock-free command queue is the known next step if that ever shows up
// in a profile (AUDIO.md).
class AudioMixer {
public:
    explicit AudioMixer(int sampleRate = 48000, int maxVoices = 32);

    int sampleRate() const { return m_sampleRate; }
    int maxVoices() const { return m_maxVoices; }

    // 0 when the sound was dropped (budget, silent, out of range).
    uint32_t play(const VoiceDesc& desc);
    void stop(uint32_t id);
    void stopAll();
    bool isPlaying(uint32_t id) const;
    void setPosition(uint32_t id, const glm::vec3& position);
    // 0..1: how much of the sound gets through to the listener (occlusion).
    void setTransmission(uint32_t id, float transmission);

    void setListener(const Listener& l);
    Listener listener() const;

    float masterGain = 1.0f;
    float categoryGain[size_t(SoundCategory::Count)] = {1, 1, 1, 1, 1, 1, 1};

    // Writes `frames` stereo frames (2 floats each) to out, overwriting.
    void mix(float* out, int frames);

    std::vector<ActiveSound> activeSounds() const;
    size_t voiceCount() const;
    uint64_t droppedCount() const { return m_dropped; }
    uint64_t stolenCount() const { return m_stolen; }

    // The spatial part on its own (used by mix, the visualizer and tests):
    // gain from distance, pan -1 (left)..1 (right), azimuth, and whether
    // the source is behind the listener.
    struct Spatial { float gain = 1.0f, pan = 0.0f, azimuth = 0.0f, distance = 0.0f; bool behind = false; };
    static Spatial spatialize(const Listener& l, const glm::vec3& pos, float minDistance, float maxDistance);

private:
    struct Voice {
        uint32_t id = 0;
        VoiceDesc desc;
        size_t cursor = 0;                        // read position in 1/65536 samples (16.16 fixed point, exact resampling)
        float transmission = 1.0f;
        float lpState = 0.0f;
        float lastPeak = 0.0f;
        float prevGainL = 0.0f, prevGainR = 0.0f; // ramped per block: no clicks when a sound moves
        bool started = false;
        float estLoudness = 0.0f;                 // for stealing, before the first mix
    };
    float estimate(const VoiceDesc& d) const;

    int m_sampleRate;
    int m_maxVoices;
    mutable std::mutex m_mutex;
    std::vector<Voice> m_voices;
    Listener m_listener;
    uint32_t m_nextId = 1;
    uint64_t m_dropped = 0, m_stolen = 0;
};

} // namespace kke
