#include "kke/SteamAudioSpatializer.h"

#include <phonon.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace kke {

namespace {
const char* errorName(IPLerror e) {
    switch (e) {
        case IPL_STATUS_SUCCESS: return "success";
        case IPL_STATUS_FAILURE: return "failure";
        case IPL_STATUS_OUTOFMEMORY: return "out of memory";
        case IPL_STATUS_INITIALIZATION: return "could not initialize (a missing or incompatible library, or a bad SOFA file)";
        default: return "unknown error";
    }
}
} // namespace

struct SteamAudioSpatializer::Impl {
    IPLContext context = nullptr;
    IPLHRTF hrtf = nullptr;
    struct Slot {
        IPLBinauralEffect effect = nullptr;
        uint32_t voice = 0;              // 0: free
        std::vector<float> in;           // mono, waiting for a whole frame
        size_t inCount = 0;
        std::vector<float> out;          // stereo interleaved, processed, waiting to be mixed
        size_t outRead = 0, outCount = 0;
        glm::vec3 direction{0.0f, 0.0f, -1.0f};
    };
    std::vector<Slot> slots;
    std::vector<float> left, right;      // one frame, deinterleaved output
    static constexpr int kMaxBlock = 8192; // longest mixer block placed (longer: played unplaced)

    ~Impl() {
        for (Slot& s : slots)
            if (s.effect) iplBinauralEffectRelease(&s.effect);
        if (hrtf) iplHRTFRelease(&hrtf);
        if (context) iplContextRelease(&context);
    }
    Slot* find(uint32_t voice) {
        for (Slot& s : slots)
            if (s.voice == voice) return &s;
        return nullptr;
    }
};

std::shared_ptr<SteamAudioSpatializer> SteamAudioSpatializer::create(const Settings& settings, std::string* error) {
    auto fail = [&](const std::string& why) -> std::shared_ptr<SteamAudioSpatializer> {
        if (error) *error = why;
        return nullptr;
    };
    if (settings.sampleRate < 8000 || settings.frameSize < 32 || settings.frameSize > 8192 || settings.maxVoices < 1)
        return fail("bad settings (sample rate, frame size 32..8192, at least one voice)");
    std::shared_ptr<SteamAudioSpatializer> sp(new SteamAudioSpatializer(settings));
    Impl& m = *sp->m;

    IPLContextSettings cs{};
    cs.version = STEAMAUDIO_VERSION;
    if (IPLerror e = iplContextCreate(&cs, &m.context); e != IPL_STATUS_SUCCESS)
        return fail(std::string("Steam Audio context: ") + errorName(e));
    IPLAudioSettings as{settings.sampleRate, settings.frameSize};
    IPLHRTFSettings hs{};
    hs.type = settings.sofaFile.empty() ? IPL_HRTFTYPE_DEFAULT : IPL_HRTFTYPE_SOFA;
    hs.sofaFileName = settings.sofaFile.empty() ? nullptr : settings.sofaFile.c_str();
    // Measured (tests/test_spatializer.cpp): normalized, the default HRTF
    // carries ~1.6x the input's energy; this brings it to the built-in
    // model's loudness, so switching mode doesn't jump.
    hs.volume = 0.78f;
    hs.normType = IPL_HRTFNORMTYPE_RMS; // about as loud from every direction
    if (IPLerror e = iplHRTFCreate(m.context, &as, &hs, &m.hrtf); e != IPL_STATUS_SUCCESS)
        return fail(settings.sofaFile.empty() ? std::string("Steam Audio HRTF: ") + errorName(e)
                                              : "Steam Audio could not load the HRTF '" + settings.sofaFile + "': " + errorName(e));
    const size_t frame = size_t(settings.frameSize);
    m.slots.resize(size_t(settings.maxVoices));
    for (Impl::Slot& s : m.slots) {
        IPLBinauralEffectSettings es{m.hrtf};
        if (IPLerror e = iplBinauralEffectCreate(m.context, &as, &es, &s.effect); e != IPL_STATUS_SUCCESS)
            return fail(std::string("Steam Audio binaural effect: ") + errorName(e));
        s.in.assign(frame, 0.0f);
        // Room for what a frame makes plus the longest block.
        s.out.assign((frame * 2 + size_t(Impl::kMaxBlock)) * 2, 0.0f);
    }
    m.left.assign(frame, 0.0f);
    m.right.assign(frame, 0.0f);
    return sp;
}

SteamAudioSpatializer::SteamAudioSpatializer(const Settings& s) : m_settings(s), m(std::make_unique<Impl>()) {}
SteamAudioSpatializer::~SteamAudioSpatializer() = default;

int SteamAudioSpatializer::voicesInUse() const {
    int n = 0;
    for (const Impl::Slot& s : m->slots) n += s.voice != 0;
    return n;
}

void SteamAudioSpatializer::release(uint32_t voice) {
    if (Impl::Slot* s = m->find(voice)) {
        iplBinauralEffectReset(s->effect);
        s->voice = 0;
        s->inCount = 0;
        s->outRead = s->outCount = 0;
    }
}

void SteamAudioSpatializer::process(uint32_t voice, const float* mono, int frames, const glm::vec3& direction, float gainFrom,
                                    float gainTo, float* out) {
    if (frames <= 0) return;
    Impl::Slot* slot = m->find(voice);
    if (!slot) {
        slot = m->find(0);
        if (slot) {
            slot->voice = voice;
            // One frame of silence first: the latency every voice has, so
            // the FIFO never runs dry.
            slot->outRead = 0;
            slot->outCount = size_t(m_settings.frameSize);
            std::fill(slot->out.begin(), slot->out.begin() + ptrdiff_t(slot->outCount * 2), 0.0f);
            slot->inCount = 0;
        }
    }
    if (!slot || frames > Impl::kMaxBlock) {
        // Every effect busy, or a block longer than planned for: centred,
        // not placed, rather than a stall or a gap.
        ++m_overflowed;
        const float inv = 1.0f / float(frames);
        for (int f = 0; f < frames; ++f) {
            const float g = (gainFrom + (gainTo - gainFrom) * float(f) * inv) * 0.7071f;
            out[2 * f] += mono[f] * g;
            out[2 * f + 1] += mono[f] * g;
        }
        return;
    }
    const float len = glm::length(direction);
    slot->direction = len > 1e-4f ? direction / len : glm::vec3(0.0f, 0.0f, -1.0f);
    const size_t frame = size_t(m_settings.frameSize);
    const size_t cap = slot->out.size() / 2;

    // Mono in; each whole frame through the HRTF into the output FIFO.
    for (int i = 0; i < frames; ++i) {
        slot->in[slot->inCount++] = mono[i];
        if (slot->inCount < frame) continue;
        slot->inCount = 0;
        float* inPtr = slot->in.data();
        float* outPtrs[2] = {m->left.data(), m->right.data()};
        IPLAudioBuffer inBuf{1, m_settings.frameSize, &inPtr};
        IPLAudioBuffer outBuf{2, m_settings.frameSize, outPtrs};
        IPLBinauralEffectParams p{};
        p.direction = IPLVector3{slot->direction.x, slot->direction.y, slot->direction.z};
        p.interpolation = m_settings.bilinear ? IPL_HRTFINTERPOLATION_BILINEAR : IPL_HRTFINTERPOLATION_NEAREST;
        p.spatialBlend = 1.0f;
        p.hrtf = m->hrtf;
        p.peakDelays = nullptr;
        iplBinauralEffectApply(slot->effect, &p, &inBuf, &outBuf);
        // Compact what's left to the front if the new frame wouldn't fit.
        if (slot->outRead + slot->outCount + frame > cap) {
            std::memmove(slot->out.data(), slot->out.data() + slot->outRead * 2, slot->outCount * 2 * sizeof(float));
            slot->outRead = 0;
        }
        float* dst = slot->out.data() + (slot->outRead + slot->outCount) * 2;
        for (size_t k = 0; k < frame; ++k) {
            dst[2 * k] = m->left[k];
            dst[2 * k + 1] = m->right[k];
        }
        slot->outCount += frame;
    }

    // As much out as came in (the FIFO holds one frame of latency).
    const size_t take = std::min(slot->outCount, size_t(frames));
    const float* src = slot->out.data() + slot->outRead * 2;
    const float inv = 1.0f / float(frames);
    for (size_t f = 0; f < take; ++f) {
        const float g = gainFrom + (gainTo - gainFrom) * float(f) * inv;
        out[2 * f] += src[2 * f] * g;
        out[2 * f + 1] += src[2 * f + 1] * g;
    }
    slot->outRead += take;
    slot->outCount -= take;
    if (slot->outCount == 0) slot->outRead = 0;
}

} // namespace kke
