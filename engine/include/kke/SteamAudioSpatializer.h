#pragma once

#include "kke/AudioMixer.h"

#include <memory>
#include <string>

namespace kke {

// Steam Audio (Valve, Apache-2.0) as the mixer's HRTF backend
// (SpatialMode::Hrtf, docs/AUDIO.md "Steam Audio"): each spatial voice goes
// through its own IPLBinauralEffect with Steam Audio's measured default
// HRTF, or one loaded from a SOFA file (your own ears' measurements, or
// another database). Compared with the built-in Binaural mode (a delay and
// a shelf per ear) it adds elevation (above and below you) and a real
// front/back difference, for a few percent of a core.
//
// Steam Audio works in fixed frames (`frameSize`); the mixer's blocks can
// be any length, so each voice has a small FIFO in and out: one frame of
// latency (5.3 ms at 256/48 kHz). Everything is allocated up front for
// `maxVoices` voices; the audio thread never allocates. A voice beyond
// that (all effects busy) plays unplaced rather than stalling.
//
// Only built with KKE_ENABLE_STEAM_AUDIO=ON (the SDK is a 150 MB download);
// create() then returns null, with the reason, if Steam Audio fails to
// start (no libphonon, a bad SOFA file).
class SteamAudioSpatializer : public Spatializer {
public:
    struct Settings {
        int sampleRate = 48000;
        int frameSize = 256;
        int maxVoices = 32;
        std::string sofaFile;   // empty: Steam Audio's default HRTF
        bool bilinear = false;  // smoother between measured directions, ~2x the cost
    };
    static std::shared_ptr<SteamAudioSpatializer> create(const Settings& settings, std::string* error);
    ~SteamAudioSpatializer() override;

    const char* name() const override { return "Steam Audio"; }
    void process(uint32_t voice, const float* mono, int frames, const glm::vec3& direction, float gainFrom, float gainTo,
                 float* out) override;
    void release(uint32_t voice) override;

    const Settings& settings() const { return m_settings; }
    int voicesInUse() const;
    uint64_t overflowed() const { return m_overflowed; } // blocks played unplaced: every effect was busy

private:
    struct Impl;
    explicit SteamAudioSpatializer(const Settings& s);
    Settings m_settings;
    std::unique_ptr<Impl> m;
    uint64_t m_overflowed = 0;
};

} // namespace kke
