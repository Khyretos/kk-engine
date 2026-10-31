#pragma once

#include "kke/AudioMixer.h"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace kke {

// Adaptive music (ENG-F39, docs/AUDIO.md "Adaptive music"): what Wwise and
// FMOD's music systems and Unreal's MetaSounds music do, as data.
//
//   segment     a piece of music with a tempo and a length in bars, made of
//               layers (stems) that play in sync and loop together
//   layers      each has a sound and a parameter band: "drums" come in as
//               intensity rises from 0.3 to 0.6, "strings" fade out above 0.9.
//               All layers always play, silent ones at gain 0, so one that
//               fades in is already in time
//   transitions rules for getting from one segment to another: at once, on
//               the next beat or bar, on a grid of bars, or when the segment
//               ends; with a fade out and in, optionally through a bridge
//               segment ("exploring" -> sting -> "combat")
//   stingers    one-shots on the next beat or bar, over the music
//
// The moment of a transition is found inside the mixer, to the sample
// (AudioMixer::playScheduled), from the playing segment's own position: it
// is on the beat whatever the game's frame rate does. The director only
// needs a MusicBackend, so everything here is tested without a sound device
// (tests/test_music.cpp).

enum class MusicWhen : uint8_t { Immediate, NextBeat, NextBar, Grid, EndOfSegment };
const char* musicWhenName(MusicWhen w);
bool musicWhenFromName(const std::string& name, MusicWhen& out);

struct MusicLayerDef {
    std::string sound;
    std::string parameter = "intensity";
    // Gain by the parameter: 0 below `from`, rising to 1 at `full` (full <= from:
    // a switch at `from`), 1 up to `fadeFrom`, falling to 0 at `to`.
    float from = 0.0f, full = 0.0f;
    float fadeFrom = 1.0e9f, to = 1.0e9f;
    float gain = 1.0f;
    float fade = 1.0f; // seconds a change of gain takes
};

struct MusicSegmentDef {
    std::string name;
    float bpm = 120.0f;
    int beatsPerBar = 4;
    int bars = 0;        // its length; 0 takes the layers' sound length
    bool loop = true;
    float gain = 1.0f;
    std::string next;    // after a segment that does not loop ends
    std::vector<MusicLayerDef> layers;
};

struct MusicTransitionDef {
    std::string from = "*", to = "*"; // segment names; "*" any
    MusicWhen when = MusicWhen::NextBar;
    int gridBars = 4;                 // for Grid: every this many bars from the segment's start
    float fadeOut = 0.5f;             // the old segment, from the moment of the transition
    float fadeIn = 0.0f;              // the new one
    std::string via;                  // a bridge segment played in between (it should not loop)
};

struct MusicStingerDef {
    std::string name, sound;
    float gain = 1.0f;
    MusicWhen when = MusicWhen::NextBar; // Immediate, NextBeat or NextBar
};

struct MusicVoiceSpec {
    std::string sound;
    float gain = 1.0f;
    bool loop = true;
    float fadeIn = 0.0f;
};

// What the director plays through.
class MusicBackend {
public:
    virtual ~MusicBackend() = default;
    // Starts the voices together when `reference` (0: now) has played
    // `atSeconds` of its sound, and fades `fadeOut` out then. An id per voice
    // in order (0 for one that did not start).
    virtual std::vector<uint32_t> start(const std::vector<MusicVoiceSpec>& voices, uint32_t reference, double atSeconds,
                                        const std::vector<uint32_t>& fadeOut, float fadeOutSeconds) = 0;
    virtual void setGain(uint32_t voice, float gain, float seconds) = 0;
    virtual void stop(uint32_t voice, float fadeSeconds) = 0;
    virtual double position(uint32_t voice) const = 0; // seconds into its sound
    virtual double length(uint32_t voice) const = 0;   // the sound's length in seconds
    virtual bool playing(uint32_t voice) const = 0;    // false once it has ended (a voice waiting to start is playing)
};

// The mixer as a backend. `load` turns a name into a sound (AudioModule's
// cachedSound, usually); a sound it cannot load is a voice that does not start.
class MixerMusicBackend : public MusicBackend {
public:
    MixerMusicBackend(AudioMixer& mixer, std::function<SoundHandle(const std::string&)> load) : m_mixer(mixer), m_load(std::move(load)) {}
    std::vector<uint32_t> start(const std::vector<MusicVoiceSpec>& voices, uint32_t reference, double atSeconds, const std::vector<uint32_t>& fadeOut,
                                float fadeOutSeconds) override;
    void setGain(uint32_t voice, float gain, float seconds) override { m_mixer.fade(voice, gain, seconds); }
    void stop(uint32_t voice, float fadeSeconds) override;
    double position(uint32_t voice) const override { return m_mixer.voiceSeconds(voice); }
    double length(uint32_t voice) const override { return m_mixer.voiceLength(voice); }
    bool playing(uint32_t voice) const override { return m_mixer.isPlaying(voice); }

private:
    AudioMixer& m_mixer;
    std::function<SoundHandle(const std::string&)> m_load;
};

class MusicDirector {
public:
    explicit MusicDirector(MusicBackend* backend = nullptr) : m_backend(backend) {}
    void setBackend(MusicBackend* b) { m_backend = b; }

    static constexpr size_t kMaxSegments = 256, kMaxLayers = 16, kMaxTransitions = 1024, kMaxStingers = 256;
    // Seconds before a boundary that is too near to catch (the mixer works a block at a time).
    float minLead = 0.06f;

    // ---- definitions (false with a reason for a wrong one)
    bool defineSegment(const MusicSegmentDef& segment, std::string* error = nullptr);
    bool defineTransition(const MusicTransitionDef& transition, std::string* error = nullptr);
    bool defineStinger(const MusicStingerDef& stinger, std::string* error = nullptr);
    // { "segments": [...], "transitions": [...], "stingers": [...] }
    bool fromJson(const nlohmann::json& j, std::string* error = nullptr);
    bool hasSegment(const std::string& name) const { return m_segments.count(name) != 0; }
    const MusicSegmentDef* segment(const std::string& name) const;
    void clear(); // stops everything and forgets every definition

    // ---- control
    // Goes to a segment by the first transition rule that fits (the next bar
    // by default). Nothing playing: it starts at once. The same segment again
    // does nothing. A request made while one is waiting replaces it.
    bool play(const std::string& segment, std::string* error = nullptr);
    // At once, whatever the rules say.
    bool playNow(const std::string& segment, float fadeIn = 0.0f, std::string* error = nullptr);
    void stop(float fade = 1.0f);
    // A one-shot on the next beat or bar (by its definition).
    bool stinger(const std::string& name, std::string* error = nullptr);

    void setParameter(const std::string& name, float value);
    float parameter(const std::string& name) const;
    void setIntensity(float v) { setParameter("intensity", v); }
    float intensity() const { return parameter("intensity"); }

    // Call every frame.
    void update(float dt);

    // ---- state
    const std::string& current() const { return m_current; }
    // The segment a transition is waiting to start ("" when none).
    std::string pending() const { return m_stages.empty() ? std::string() : m_stages.back().name; }
    bool playing() const { return !m_current.empty(); }
    // Where the current segment is: the beat from its start (fractional), and the bar.
    float beat() const { return m_beat; }
    int bar() const { return m_bar; }
    // Seconds into the current segment's loop.
    double position() const;
    float layerGain(size_t layer) const { return layer < m_layerGain.size() ? m_layerGain[layer] : 0.0f; }

    struct Event {
        enum class Kind : uint8_t { Beat, Bar, Segment, Ended, Stinger } kind = Kind::Beat;
        std::string name;   // the segment, or the stinger
        int beat = 0;       // 0-based beat in the bar
        int bar = 0;        // bars since the segment started
    };
    // What happened since the last call: beats, bars, segments that began or ended, stingers.
    std::vector<Event> takeEvents();

private:
    struct Stage {
        std::string name;
        std::vector<uint32_t> ids;
        std::vector<float> gains;
    };
    const MusicTransitionDef* findTransition(const std::string& from, const std::string& to) const;
    std::vector<MusicVoiceSpec> specsFor(const MusicSegmentDef& s, float fadeIn, std::vector<float>& gains) const;
    float layerTarget(const MusicSegmentDef& s, const MusicLayerDef& l) const;
    double boundary(const MusicSegmentDef& s, MusicWhen when, int gridBars, double pos, double length) const;
    void adopt(const Stage& st);        // a stage has started: it is the current segment
    void cancelStages();
    void push(Event e);
    double segmentLength(const MusicSegmentDef& s, uint32_t voice) const;

    MusicBackend* m_backend;
    std::map<std::string, MusicSegmentDef> m_segments;
    std::vector<MusicTransitionDef> m_transitions;
    std::map<std::string, MusicStingerDef> m_stingers;
    std::map<std::string, float> m_params;

    std::string m_current;
    std::vector<uint32_t> m_voices;
    std::vector<float> m_layerGain, m_sentGain;
    std::vector<Stage> m_stages;
    std::vector<uint32_t> m_stingerVoices;
    int m_beatIndex = -1;
    float m_beat = 0.0f;
    int m_bar = 0;
    std::vector<Event> m_events;
};

} // namespace kke
