#include "kke/AudioMixer.h"
#include "kke/ImpactSynth.h"
#include "kke/modules/AudioModule.h"
#include "kke/modules/SoundVisualizerModule.h"

#include <gtest/gtest.h>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <memory>
#include <cstdio>

using namespace kke;

namespace {

SoundHandle sine(float freq, float seconds, int rate = 48000, float amp = 0.5f) {
    auto b = std::make_shared<SoundBuffer>();
    b->sampleRate = rate;
    b->samples.resize(size_t(seconds * rate));
    for (size_t i = 0; i < b->samples.size(); ++i) b->samples[i] = amp * std::sin(glm::two_pi<float>() * freq * float(i) / float(rate));
    return b;
}

struct Energy { double left = 0, right = 0; };
Energy mixEnergy(AudioMixer& m, int frames) {
    std::vector<float> out(size_t(frames) * 2);
    m.mix(out.data(), frames);
    Energy e;
    for (int i = 0; i < frames; ++i) { e.left += out[2 * i] * out[2 * i]; e.right += out[2 * i + 1] * out[2 * i + 1]; }
    return e;
}

// Time until the envelope stays below `db` (relative to the peak).
float ringTime(const SoundBuffer& b, float db = -40.0f) {
    float peak = 0;
    for (float s : b.samples) peak = std::max(peak, std::fabs(s));
    const float thr = peak * std::pow(10.0f, db / 20.0f);
    size_t last = 0;
    for (size_t i = 0; i < b.samples.size(); ++i) if (std::fabs(b.samples[i]) > thr) last = i;
    return float(last) / float(b.sampleRate);
}

// Spectral centroid (Hz) of the first 50 ms: "how bright". A plain DFT
// at 25 Hz steps; slow, but this is a test.
float brightness(const SoundBuffer& b) {
    const size_t n = std::min(b.samples.size(), size_t(b.sampleRate / 20));
    double num = 0, den = 0;
    for (float f = 25.0f; f < 16000.0f; f += 25.0f) {
        const double w = glm::two_pi<double>() * f / b.sampleRate;
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) { re += b.samples[i] * std::cos(w * i); im += b.samples[i] * std::sin(w * i); }
        const double mag = std::sqrt(re * re + im * im);
        num += f * mag;
        den += mag;
    }
    return den > 0 ? float(num / den) : 0.0f;
}

float peak(const SoundBuffer& b) {
    float p = 0;
    for (float s : b.samples) p = std::max(p, std::fabs(s));
    return p;
}

} // namespace

TEST(AudioMixer, SoundOnTheRightIsLouderOnTheRight) {
    AudioMixer m;
    Listener l; // at origin looking down -Z
    m.setListener(l);
    VoiceDesc d;
    d.sound = sine(440, 0.5f);
    d.position = glm::vec3(3, 0, 0);
    ASSERT_NE(m.play(d), 0u);
    Energy e = mixEnergy(m, 4800);
    EXPECT_GT(e.right, e.left * 4.0);
}

TEST(AudioMixer, AzimuthAndBehind) {
    Listener l;
    auto ahead = AudioMixer::spatialize(l, glm::vec3(0, 0, -5), 1, 50);
    auto right = AudioMixer::spatialize(l, glm::vec3(5, 0, 0), 1, 50);
    auto back = AudioMixer::spatialize(l, glm::vec3(0, 0, 5), 1, 50);
    EXPECT_NEAR(ahead.azimuth, 0.0f, 1e-4f);
    EXPECT_NEAR(right.azimuth, glm::half_pi<float>(), 1e-4f);
    EXPECT_FALSE(ahead.behind);
    EXPECT_TRUE(back.behind);
}

TEST(AudioMixer, DistanceAttenuatesAndCullsBeyondMax) {
    Listener l;
    auto near = AudioMixer::spatialize(l, glm::vec3(0, 0, -1), 1, 50);
    auto mid = AudioMixer::spatialize(l, glm::vec3(0, 0, -10), 1, 50);
    auto far = AudioMixer::spatialize(l, glm::vec3(0, 0, -60), 1, 50);
    EXPECT_FLOAT_EQ(near.gain, 1.0f);
    EXPECT_NEAR(mid.gain, 0.1f, 1e-4f);
    EXPECT_EQ(far.gain, 0.0f);

    AudioMixer m;
    VoiceDesc d;
    d.sound = sine(440, 0.2f);
    d.position = glm::vec3(0, 0, -100);
    EXPECT_EQ(m.play(d), 0u); // out of range: never takes a voice
    EXPECT_EQ(m.droppedCount(), 1u);
}

TEST(AudioMixer, OcclusionMuffles) {
    // Same 6 kHz tone, clear vs behind a wall (transmission 0.3): much less
    // energy, and more than the gain alone would explain (the low-pass).
    auto run = [](float transmission) {
        AudioMixer m;
        VoiceDesc d;
        d.sound = sine(6000, 0.3f);
        d.position = glm::vec3(0, 0, -1);
        uint32_t id = m.play(d);
        m.setTransmission(id, transmission);
        Energy e = mixEnergy(m, 9600);
        return e.left + e.right;
    };
    const double clear = run(1.0f), walled = run(0.3f);
    EXPECT_LT(walled, clear * 0.3 * 0.3 * 0.5); // gain^2 would be 0.09; the filter takes at least half again
}

// The settings menu's volumes reach the mixer (they used to be stored
// and never applied).
TEST(AudioModule, PlayerVolumesSetMixerGains) {
    AudioMixer m(48000, 2);
    EngineSettings::Audio v;
    v.master = 50;
    v.music = 20;
    v.effects = 70;
    v.muteWhenUnfocused = true;
    AudioModule::applyVolumes(m, v, true);
    EXPECT_FLOAT_EQ(m.masterGain, 0.5f);
    EXPECT_FLOAT_EQ(m.categoryGain[size_t(SoundCategory::Music)], 0.2f);
    EXPECT_FLOAT_EQ(m.categoryGain[size_t(SoundCategory::Impact)], 0.7f);
    EXPECT_FLOAT_EQ(m.categoryGain[size_t(SoundCategory::Ambient)], 0.7f);
    EXPECT_FLOAT_EQ(m.categoryGain[size_t(SoundCategory::Voice)], 1.0f);
    AudioModule::applyVolumes(m, v, false);
    EXPECT_FLOAT_EQ(m.masterGain, 0.0f);
    v.muteWhenUnfocused = false;
    AudioModule::applyVolumes(m, v, false);
    EXPECT_FLOAT_EQ(m.masterGain, 0.5f);
    v.master = 250; // out of range from a hand-edited file
    AudioModule::applyVolumes(m, v, true);
    EXPECT_FLOAT_EQ(m.masterGain, 1.0f);
}

TEST(AudioMixer, VoiceBudgetStealsTheQuietest) {
    AudioMixer m(48000, 2);
    VoiceDesc loud, quiet, medium;
    loud.sound = quiet.sound = medium.sound = sine(300, 1.0f);
    loud.position = glm::vec3(0, 0, -1);
    quiet.position = glm::vec3(0, 0, -30);
    medium.position = glm::vec3(0, 0, -5);
    const uint32_t a = m.play(loud), b = m.play(quiet);
    ASSERT_NE(a, 0u);
    ASSERT_NE(b, 0u);
    const uint32_t c = m.play(medium);
    EXPECT_NE(c, 0u);
    EXPECT_TRUE(m.isPlaying(a));
    EXPECT_FALSE(m.isPlaying(b));
    EXPECT_EQ(m.stolenCount(), 1u);
    // Quieter than everything playing: dropped.
    VoiceDesc tiny = quiet;
    tiny.position = glm::vec3(0, 0, -40);
    EXPECT_EQ(m.play(tiny), 0u);
    EXPECT_EQ(m.voiceCount(), 2u);
}

TEST(AudioMixer, FinishedVoicesAreRemovedLoopsAreNot) {
    AudioMixer m;
    VoiceDesc once, loop;
    once.sound = loop.sound = sine(200, 0.01f); // 480 samples
    once.spatial = loop.spatial = false;
    loop.loop = true;
    const uint32_t a = m.play(once), b = m.play(loop);
    std::vector<float> out(2048 * 2);
    m.mix(out.data(), 2048);
    EXPECT_FALSE(m.isPlaying(a));
    EXPECT_TRUE(m.isPlaying(b));
}

TEST(AudioMixer, ResamplesOtherRates) {
    // A 22.05 kHz, 0.5 s sound lasts 0.5 s at 48 kHz too.
    AudioMixer m(48000);
    VoiceDesc d;
    d.sound = sine(440, 0.5f, 22050);
    d.spatial = false;
    const uint32_t id = m.play(d);
    std::vector<float> out(4800 * 2);
    for (int i = 0; i < 4; ++i) m.mix(out.data(), 4800); // 0.4 s
    EXPECT_TRUE(m.isPlaying(id));
    for (int i = 0; i < 2; ++i) m.mix(out.data(), 4800); // 0.6 s
    EXPECT_FALSE(m.isPlaying(id));
}

TEST(AudioMixer, OutputStaysInRangeWithManyVoices) {
    AudioMixer m(48000, 64);
    for (int i = 0; i < 64; ++i) {
        VoiceDesc d;
        d.sound = sine(100.0f + 37.0f * i, 0.2f, 48000, 1.0f);
        d.spatial = false;
        m.play(d);
    }
    std::vector<float> out(4800 * 2);
    m.mix(out.data(), 4800);
    for (float s : out) ASSERT_LE(std::fabs(s), 1.0f);
}

TEST(ImpactSynth, SameSeedSameSoundDifferentSeedDifferent) {
    AudioMaterialTable t;
    ImpactParams p;
    p.seed = 42;
    SoundBuffer a = synthesizeImpact(t.get(AudioMaterialTable::Wood), p);
    SoundBuffer b = synthesizeImpact(t.get(AudioMaterialTable::Wood), p);
    p.seed = 43;
    SoundBuffer c = synthesizeImpact(t.get(AudioMaterialTable::Wood), p);
    ASSERT_EQ(a.samples.size(), b.samples.size());
    EXPECT_EQ(a.samples, b.samples);
    EXPECT_NE(a.samples, c.samples);
}

TEST(ImpactSynth, MaterialsAreDistinguishable) {
    // What a blind player relies on: metal and glass ring, stone and dirt
    // don't; glass is brightest, rubber and dirt are dullest.
    AudioMaterialTable t;
    ImpactParams p;
    auto get = [&](uint32_t id) { return synthesizeImpact(t.get(id), p); };
    SoundBuffer metal = get(AudioMaterialTable::Metal), glass = get(AudioMaterialTable::Glass), wood = get(AudioMaterialTable::Wood),
                stone = get(AudioMaterialTable::Stone), rubber = get(AudioMaterialTable::Rubber), dirt = get(AudioMaterialTable::Dirt);
    EXPECT_GT(ringTime(metal), 0.4f);
    EXPECT_GT(ringTime(glass), 0.2f);
    EXPECT_LT(ringTime(wood), 0.15f);
    EXPECT_LT(ringTime(stone), 0.1f);
    EXPECT_GT(ringTime(metal), 4.0f * ringTime(wood));
    EXPECT_GT(brightness(glass), brightness(metal));
    EXPECT_GT(brightness(metal), brightness(wood));
    EXPECT_GT(brightness(wood), brightness(rubber));
    EXPECT_GT(brightness(stone), brightness(dirt));
}

TEST(ImpactSynth, HarderHitsAreLouderAndBrighter) {
    AudioMaterialTable t;
    ImpactParams soft, hard;
    soft.intensity = 0.1f;
    hard.intensity = 1.0f;
    SoundBuffer s = synthesizeImpact(t.get(AudioMaterialTable::Stone), soft);
    SoundBuffer h = synthesizeImpact(t.get(AudioMaterialTable::Stone), hard);
    EXPECT_GT(peak(h), 2.0f * peak(s));
    EXPECT_GT(brightness(h), brightness(s));
    EXPECT_LE(peak(h), 1.0f);
}

TEST(ImpactSynth, BiggerRingsLower) {
    AudioMaterialTable t;
    ImpactParams small, big;
    small.size = 0.5f;
    big.size = 2.0f;
    EXPECT_GT(brightness(synthesizeImpact(t.get(AudioMaterialTable::Metal), small)),
              brightness(synthesizeImpact(t.get(AudioMaterialTable::Metal), big)));
}

TEST(ImpactSynth, IntensityFromSpeed) {
    EXPECT_EQ(impactIntensity(0.5f), 0.0f);
    EXPECT_GT(impactIntensity(3.0f), 0.0f);
    EXPECT_EQ(impactIntensity(50.0f), 1.0f);
}

TEST(ImpactBank, CachesVariantsAndLevels) {
    AudioMaterialTable t;
    ImpactBank bank(t, 48000, 4, 4);
    SoundHandle a = bank.get(AudioMaterialTable::Wood, 0.5f, 1);
    SoundHandle b = bank.get(AudioMaterialTable::Wood, 0.52f, 5); // same level, same variant (5 % 4 == 1)
    EXPECT_EQ(a.get(), b.get());
    for (uint32_t s = 0; s < 100; ++s) bank.get(AudioMaterialTable::Wood, float(s % 10) / 10.0f, s);
    EXPECT_LE(bank.cachedCount(), 16u); // 4 levels x 4 variants, however many hits
    EXPECT_NE(bank.get(AudioMaterialTable::Metal, 0.5f, 1).get(), a.get());
    t.get(12345); // unknown ids fall back to Default, never throw
}

TEST(SoundVisualizer, RingPoints) {
    const glm::vec2 c(100, 100);
    glm::vec2 up = SoundVisualizerModule::ringPoint(0.0f, c, 50);
    glm::vec2 right = SoundVisualizerModule::ringPoint(glm::half_pi<float>(), c, 50);
    EXPECT_NEAR(up.x, 100, 1e-3);
    EXPECT_NEAR(up.y, 50, 1e-3);   // ahead = top of the screen
    EXPECT_NEAR(right.x, 150, 1e-3);
    EXPECT_NEAR(right.y, 100, 1e-3);
}

TEST(SoundVisualizer, SettingsRoundTrip) {
    const std::string path = ::testing::TempDir() + "kke_access_test.json";
    std::remove(path.c_str());
    {
        SoundVisualizerModule v(path);
        v.settings.opacity = 0.5f;
        v.settings.captions = false;
        v.settings.showCategory[int(SoundCategory::Footstep)] = false;
        v.settings.color[int(SoundCategory::Impact)] = glm::vec3(0.1f, 0.2f, 0.3f);
        ASSERT_TRUE(v.save());
    }
    SoundVisualizerModule w(path);
    ASSERT_TRUE(w.load());
    EXPECT_FLOAT_EQ(w.settings.opacity, 0.5f);
    EXPECT_FALSE(w.settings.captions);
    EXPECT_FALSE(w.settings.showCategory[int(SoundCategory::Footstep)]);
    EXPECT_NEAR(w.settings.color[int(SoundCategory::Impact)].y, 0.2f, 1e-6);
    std::remove(path.c_str());
}

// ---------------------------------------------------------------- fades, pitch, buses, ducking (ENG-F42)

namespace {

// RMS of one mixed block (both ears).
double blockRms(AudioMixer& m, int frames = 480) {
    const Energy e = mixEnergy(m, frames);
    return std::sqrt((e.left + e.right) / (2.0 * frames));
}

VoiceDesc centred(SoundHandle s, SoundCategory c) {
    VoiceDesc d;
    d.sound = std::move(s);
    d.spatial = false;
    d.loop = true;
    d.category = c;
    d.reverbSend = 0.0f;
    return d;
}

} // namespace

TEST(AudioMixer, FadesGlideAndCanEndAVoice) {
    AudioMixer m;
    const uint32_t id = m.play(centred(sine(300, 0.5f), SoundCategory::Music));
    const double full = blockRms(m);
    ASSERT_GT(full, 0.05);
    m.fade(id, 0.5f, 0.1f); // half the gain in 0.1 s (ten 10 ms blocks)
    const double first = blockRms(m);
    EXPECT_LT(first, full);
    EXPECT_GT(first, full * 0.85); // a glide, not a jump
    for (int i = 0; i < 12; ++i) blockRms(m);
    EXPECT_NEAR(blockRms(m) / full, 0.5, 0.02);
    m.fade(id, 0.0f, 0.05f, true);
    for (int i = 0; i < 8; ++i) blockRms(m);
    EXPECT_FALSE(m.isPlaying(id));
    // A fade-in starts silent and arrives.
    VoiceDesc in = centred(sine(300, 0.5f), SoundCategory::Music);
    in.fadeIn = 0.2f;
    const uint32_t b = m.play(in);
    EXPECT_LT(blockRms(m), full * 0.1);
    for (int i = 0; i < 25; ++i) blockRms(m);
    EXPECT_NEAR(blockRms(m) / full, 1.0, 0.02);
    m.fade(b, 0.25f, 0.0f); // 0 s: at once
    blockRms(m);
    EXPECT_NEAR(blockRms(m) / full, 0.25, 0.02);
}

TEST(AudioMixer, PitchChangesSpeed) {
    AudioMixer m(48000);
    VoiceDesc d;
    d.sound = sine(440, 0.5f);
    d.spatial = false;
    d.pitch = 2.0f; // twice as fast: done in 0.25 s
    const uint32_t id = m.play(d);
    std::vector<float> out(4800 * 2);
    for (int i = 0; i < 2; ++i) m.mix(out.data(), 4800);
    EXPECT_TRUE(m.isPlaying(id));
    m.mix(out.data(), 4800);
    EXPECT_FALSE(m.isPlaying(id)); // 0.3 s in
    d.pitch = 100.0f; // clamped to 4
    const uint32_t fast = m.play(d);
    m.mix(out.data(), 4800);
    EXPECT_TRUE(m.isPlaying(fast)); // 0.1 s in: a quarter of the way at 4x would end at 0.125 s
    m.mix(out.data(), 4800);
    EXPECT_FALSE(m.isPlaying(fast));
}

TEST(AudioMixer, BusesGlideOnTopOfThePlayersVolume) {
    AudioMixer m;
    m.play(centred(sine(300, 0.5f), SoundCategory::Music));
    const double full = blockRms(m);
    m.categoryGain[size_t(SoundCategory::Music)] = 0.5f; // the player's music volume
    m.setBusGain(SoundCategory::Music, 0.5f, 0.2f);      // the game's pause menu
    EXPECT_FLOAT_EQ(m.busTarget(SoundCategory::Music), 0.5f);
    for (int i = 0; i < 25; ++i) blockRms(m);
    EXPECT_NEAR(m.busGain(SoundCategory::Music), 0.5f, 1e-4f);
    EXPECT_NEAR(blockRms(m) / full, 0.25, 0.02); // both multiply
    m.setBusGain(SoundCategory::Music, 1.0f); // at once
    EXPECT_FLOAT_EQ(m.busGain(SoundCategory::Music), 1.0f);
    m.setBusGain(SoundCategory::Ui, -3.0f);
    EXPECT_FLOAT_EQ(m.busTarget(SoundCategory::Ui), 0.0f); // clamped
}

TEST(AudioMixer, VoiceDucksMusicAndItComesBack) {
    AudioMixer m;
    DuckRule r; // voice over music, to 0.35
    m.addDuck(r);
    m.addDuck(r); // the same pair again replaces it
    EXPECT_EQ(m.duckCount(), 1u);
    m.play(centred(sine(200, 0.5f), SoundCategory::Music)); // whole periods per 10 ms block: steady RMS
    const double music = blockRms(m);
    VoiceDesc line = centred(sine(500, 0.6f), SoundCategory::Voice);
    line.loop = false;
    const uint32_t v = m.play(line);
    for (int i = 0; i < 40; ++i) blockRms(m); // 0.4 s in: ducked
    EXPECT_NEAR(m.duckGain(SoundCategory::Music), 0.35f, 0.01f);
    EXPECT_FLOAT_EQ(m.duckGain(SoundCategory::Voice), 1.0f); // the voice itself isn't ducked
    m.stop(v);
    blockRms(m);
    EXPECT_LT(m.duckGain(SoundCategory::Music), 0.5f); // comes back slowly (release 0.6 s) ...
    for (int i = 0; i < 300; ++i) blockRms(m);
    EXPECT_NEAR(m.duckGain(SoundCategory::Music), 1.0f, 1e-3f); // ... all the way
    EXPECT_NEAR(blockRms(m) / music, 1.0, 0.02);
    // A far, quiet voice under the threshold doesn't duck.
    VoiceDesc far = line;
    far.spatial = true;
    far.position = glm::vec3(0, 0, -59.0f);
    far.loop = true;
    far.gain = 0.01f;
    m.play(far);
    for (int i = 0; i < 40; ++i) blockRms(m);
    EXPECT_FLOAT_EQ(m.duckGain(SoundCategory::Music), 1.0f);
    m.clearDucks();
    EXPECT_EQ(m.duckCount(), 0u);
}
