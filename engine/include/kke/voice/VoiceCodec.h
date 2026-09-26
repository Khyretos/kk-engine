#pragma once

// Opus (BSD, https://opus-codec.org), the codec for voice chat: 48 kHz
// mono, 20 ms frames (960 samples), with in-band forward error correction
// so one lost packet is rebuilt from the next. Built with KKE_ENABLE_VOICE.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

namespace kke::voice {

constexpr int kSampleRate = 48000;
constexpr int kFrameSamples = 960; // 20 ms
constexpr float kFrameSeconds = 0.02f;

class VoiceEncoder {
public:
    VoiceEncoder();
    ~VoiceEncoder();
    VoiceEncoder(const VoiceEncoder&) = delete;
    VoiceEncoder& operator=(const VoiceEncoder&) = delete;

    bool ok() const { return m_enc != nullptr; }
    const std::string& error() const { return m_error; }
    // bits per second: 12000 (small) .. 64000 (music-grade); 24000 is clear speech.
    void setBitrate(int bps);
    // Expected packet loss, percent: more = more redundancy (FEC).
    void setExpectedLoss(int percent);
    // One frame of kFrameSamples; empty on failure.
    std::vector<uint8_t> encode(const float* frame);

private:
    OpusEncoder* m_enc = nullptr;
    std::string m_error;
};

class VoiceDecoder {
public:
    VoiceDecoder();
    ~VoiceDecoder();
    VoiceDecoder(const VoiceDecoder&) = delete;
    VoiceDecoder& operator=(const VoiceDecoder&) = delete;

    bool ok() const { return m_dec != nullptr; }
    // Each fills `out` with kFrameSamples; false (and silence) on a damaged frame.
    bool decode(const std::vector<uint8_t>& frame, float* out);
    // A lost frame: rebuilt from the next frame's redundancy when given,
    // otherwise Opus's best guess from what came before.
    bool conceal(const std::vector<uint8_t>* next, float* out);

private:
    OpusDecoder* m_dec = nullptr;
};

} // namespace kke::voice
