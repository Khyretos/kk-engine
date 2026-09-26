#pragma once

// Cleans up the microphone before voice chat codes it (docs/NETWORKING.md
// "Voice"), 20 ms at a time:
//
//   Echo cancellation (SpeexDSP's MDF filter, BSD): without headphones the
//   microphone hears the speakers, and everyone else would hear
//   themselves back a moment later. Given what the speakers played, an
//   adaptive filter learns the path from speaker to microphone (up to
//   `echoTailMs` of delay and room echo) and subtracts it; a residual
//   echo suppressor takes out what the filter missed.
//   Noise suppression (RNNoise, BSD): a small neural network trained on
//   speech takes out fans, keyboards, traffic and hiss, and says how
//   likely the frame is speech (a better voice-activation signal than
//   loudness).
//
// Pure DSP, no devices: VoiceModule feeds it the microphone and the
// mixer's output (AudioMixer::setOutputTap); tests feed it signals.
// Built with KKE_ENABLE_VOICE.

#include "kke/voice/VoiceCodec.h"

#include <cstdint>

struct DenoiseState;
struct SpeexEchoState_;
struct SpeexPreprocessState_;

namespace kke::voice {

class VoiceCleaner {
public:
    struct Settings {
        bool noiseSuppression = true;
        bool echoCancellation = true;
        int echoTailMs = 200; // the longest speaker-to-microphone delay + room echo it can take out
    };

    VoiceCleaner();
    explicit VoiceCleaner(const Settings& s);
    ~VoiceCleaner();
    VoiceCleaner(const VoiceCleaner&) = delete;
    VoiceCleaner& operator=(const VoiceCleaner&) = delete;

    // Either can be switched while running (the echo filter starts
    // learning again when it comes back on).
    bool noiseSuppression = true;
    bool echoCancellation = true;

    // One microphone frame (kFrameSamples, 48 kHz mono, -1..1), cleaned in
    // place. `played`: what the speakers played meanwhile (kFrameSamples),
    // or null for silence. Returns how likely the frame is speech, 0..1
    // (-1 with noise suppression off: no opinion).
    float process(float* frame, const float* played);
    // Forget what the filters learned (a new microphone, a new device).
    void reset();

    float voiceProbability() const { return m_voice; }
    // How much quieter the echo filter made the last frame, dB (0 when off
    // or nothing was playing): a check that it's working.
    float echoReductionDb() const { return m_echoReductionDb; }
    int tailMs() const { return m_tailMs; }

    static constexpr int kSubFrame = 480; // both libraries work in 10 ms

private:
    DenoiseState* m_denoise = nullptr;
    SpeexEchoState_* m_echo = nullptr;
    SpeexPreprocessState_* m_pre = nullptr;
    bool m_echoWasOn = true;
    int m_tailMs = 200;
    float m_voice = -1.0f;
    float m_echoReductionDb = 0.0f;
};

} // namespace kke::voice
