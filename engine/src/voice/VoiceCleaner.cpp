#include "kke/voice/VoiceCleaner.h"

#include "kke/Log.h"

#include <rnnoise.h>
#include <speex/speex_echo.h>
#include <speex/speex_preprocess.h>

#include <algorithm>
#include <cmath>

// SpeexDSP's warnings, into the engine log (its default is stderr; see
// the kke_speexdsp target in CMakeLists.txt).
extern "C" void kke_speex_warning(const char* text, int value, int hasValue) {
    if (hasValue) kke::log::get("Voice")->info("SpeexDSP: {} {}", text, value);
    else kke::log::get("Voice")->info("SpeexDSP: {}", text);
}

namespace kke::voice {

namespace {

static_assert(kFrameSamples % VoiceCleaner::kSubFrame == 0, "a voice frame is whole 10 ms blocks");

spx_int16_t toPcm(float x) { return static_cast<spx_int16_t>(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f)); }

double energy(const spx_int16_t* x, int n) {
    double e = 0.0;
    for (int i = 0; i < n; ++i) e += double(x[i]) * double(x[i]);
    return e;
}

} // namespace

VoiceCleaner::VoiceCleaner() : VoiceCleaner(Settings{}) {}

VoiceCleaner::VoiceCleaner(const Settings& s)
    : noiseSuppression(s.noiseSuppression), echoCancellation(s.echoCancellation), m_echoWasOn(s.echoCancellation),
      m_tailMs(std::clamp(s.echoTailMs, 20, 1000)) {
    m_denoise = rnnoise_create(nullptr); // the model built into RNNoise 0.1.1
    const int tail = m_tailMs * kSampleRate / 1000;
    m_echo = speex_echo_state_init(kSubFrame, tail);
    int rate = kSampleRate;
    speex_echo_ctl(m_echo, SPEEX_ECHO_SET_SAMPLING_RATE, &rate);
    m_pre = speex_preprocess_state_init(kSubFrame, kSampleRate);
    // Only the residual echo suppression: RNNoise does the noise, far better.
    // (AGC, VAD and dereverb are off by default; setting the VAD at all
    // prints a warning from SpeexDSP, so it's left alone.)
    int off = 0;
    speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_DENOISE, &off);
    speex_preprocess_ctl(m_pre, SPEEX_PREPROCESS_SET_ECHO_STATE, m_echo);
}

VoiceCleaner::~VoiceCleaner() {
    if (m_pre) speex_preprocess_state_destroy(m_pre);
    if (m_echo) speex_echo_state_destroy(m_echo);
    if (m_denoise) rnnoise_destroy(m_denoise);
}

void VoiceCleaner::reset() {
    if (m_echo) speex_echo_state_reset(m_echo);
    if (m_denoise) {
        rnnoise_destroy(m_denoise);
        m_denoise = rnnoise_create(nullptr);
    }
    m_voice = -1.0f;
    m_echoReductionDb = 0.0f;
}

float VoiceCleaner::process(float* frame, const float* played) {
    if (echoCancellation && !m_echoWasOn && m_echo) speex_echo_state_reset(m_echo); // learn afresh
    m_echoWasOn = echoCancellation;
    const bool echo = echoCancellation && m_echo && m_pre;
    const bool noise = noiseSuppression && m_denoise;
    float voice = noise ? 0.0f : -1.0f;
    double before = 0.0, after = 0.0;
    bool anyPlayed = false;
    for (int off = 0; off < kFrameSamples; off += kSubFrame) {
        float* x = frame + off;
        spx_int16_t mic[kSubFrame], ref[kSubFrame], out[kSubFrame];
        for (int i = 0; i < kSubFrame; ++i) mic[i] = toPcm(x[i]);
        if (echo) {
            for (int i = 0; i < kSubFrame; ++i) {
                ref[i] = played ? toPcm(played[off + i]) : spx_int16_t(0);
                anyPlayed = anyPlayed || ref[i] != 0;
            }
            speex_echo_cancellation(m_echo, mic, ref, out);
            before += energy(mic, kSubFrame);
            after += energy(out, kSubFrame);
            speex_preprocess_run(m_pre, out);
        } else {
            std::copy(mic, mic + kSubFrame, out);
        }
        if (noise) {
            // RNNoise takes and gives samples on the 16-bit scale, as floats.
            float in[kSubFrame], cleaned[kSubFrame];
            for (int i = 0; i < kSubFrame; ++i) in[i] = float(out[i]);
            voice = std::max(voice, rnnoise_process_frame(m_denoise, cleaned, in));
            for (int i = 0; i < kSubFrame; ++i) x[i] = std::clamp(cleaned[i] / 32768.0f, -1.0f, 1.0f);
        } else if (echo) {
            for (int i = 0; i < kSubFrame; ++i) x[i] = float(out[i]) / 32768.0f;
        }
    }
    m_echoReductionDb = echo && anyPlayed && before > 0.0 ? float(10.0 * std::log10(before / std::max(after, 1.0))) : 0.0f;
    m_voice = voice;
    return voice;
}

} // namespace kke::voice
