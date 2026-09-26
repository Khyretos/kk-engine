#pragma once

#include <cstdint>
#include <map>
#include <vector>

namespace kke::voice {

// One speaker's incoming voice frames, put back in order and let out at a
// steady 20 ms pace (docs/NETWORKING.md "Voice"). Packets arrive late,
// early, twice or never; the playout side asks for the next frame and
// gets it, or "lost" (conceal it: Opus guesses, or rebuilds it from the
// next frame's redundancy), or "nothing" (they stopped talking).
//
// It waits for `startFrames` frames before playing (the first cushion
// against jitter); after `maxConcealed` lost frames in a row it counts the
// talk spurt as over and waits again. Packets older than what already
// played are dropped; a jump far ahead (a new talk spurt after silence,
// or the speaker restarted) starts over from there.
class JitterBuffer {
public:
    struct Settings {
        int startFrames = 3;   // 60 ms cushion before a talk spurt plays
        int maxFrames = 25;    // 500 ms: more than that queued, skip ahead
        int maxConcealed = 5;  // 100 ms of lost frames, then the spurt is over
    };
    enum class Kind { Nothing, Frame, Lost };
    struct Out {
        Kind kind = Kind::Nothing;
        uint16_t seq = 0;
        std::vector<uint8_t> data;       // Frame: the coded frame
        const std::vector<uint8_t>* next = nullptr; // Lost: the following frame if it's here (its FEC rebuilds this one)
    };

    JitterBuffer() = default;
    explicit JitterBuffer(const Settings& s) : m_settings(s) {}

    void push(uint16_t seq, std::vector<uint8_t> data);
    // The frame to play now (call once per 20 ms of audio needed).
    Out pop();

    bool playing() const { return m_playing; }
    size_t queued() const { return m_frames.size(); }
    uint64_t late() const { return m_late; }       // arrived after their turn
    uint64_t lost() const { return m_lost; }       // concealed
    uint64_t skipped() const { return m_skipped; } // dropped to catch up
    void reset();

private:
    // Signed distance a - b on the 16-bit circle.
    static int diff(uint16_t a, uint16_t b) { return static_cast<int16_t>(static_cast<uint16_t>(a - b)); }
    Settings m_settings;
    std::map<int64_t, std::vector<uint8_t>> m_frames; // unwrapped seq -> frame
    bool m_haveBase = false;
    uint16_t m_lastSeq = 0; // latest pushed, for unwrapping
    int64_t m_lastUnwrapped = 0;
    int64_t m_next = 0;     // unwrapped seq to play next
    bool m_playing = false;
    int m_concealedRun = 0;
    uint64_t m_late = 0, m_lost = 0, m_skipped = 0;
};

// A simple voice gate: open while the level is well above the background
// noise it tracks, and for `hangover` after (so word ends aren't cut).
class VoiceActivity {
public:
    float thresholdDb = 12.0f;   // above the noise floor to open
    float minLevelDb = -50.0f;   // never opens below this (dBFS)
    float hangover = 0.3f;       // s
    // One frame of mono samples (-1..1) lasting `seconds`; true = voice.
    bool process(const float* samples, size_t count, float seconds);
    float levelDb() const { return m_levelDb; }
    float noiseDb() const { return m_noiseDb; }

private:
    float m_levelDb = -100.0f, m_noiseDb = -60.0f, m_open = 0.0f;
};

} // namespace kke::voice
