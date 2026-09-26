#include "kke/AudioMixer.h"
#if KKE_ENABLE_STEAM_AUDIO
#include "kke/SteamAudioSpatializer.h"
#endif

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using namespace kke;

namespace {
SoundHandle tone(float seconds, float hz = 700.0f, int rate = 48000) {
    auto b = std::make_shared<SoundBuffer>();
    b->sampleRate = rate;
    b->samples.resize(size_t(seconds * float(rate)));
    for (size_t i = 0; i < b->samples.size(); ++i) b->samples[i] = 0.5f * std::sin(6.2831853f * hz * float(i) / float(rate));
    return b;
}

// Records what the mixer hands it; puts everything in the left ear.
struct FakeSpatializer : Spatializer {
    struct Call { uint32_t voice; int frames; glm::vec3 dir; float from, to; };
    std::vector<Call> calls;
    std::vector<uint32_t> released;
    const char* name() const override { return "fake"; }
    void process(uint32_t voice, const float* mono, int frames, const glm::vec3& d, float from, float to, float* out) override {
        calls.push_back({voice, frames, d, from, to});
        for (int f = 0; f < frames; ++f) out[2 * f] += mono[f] * to;
    }
    void release(uint32_t voice) override { released.push_back(voice); }
};

double energy(const std::vector<float>& v, int ch) {
    double e = 0.0;
    for (size_t i = size_t(ch); i < v.size(); i += 2) e += double(v[i]) * v[i];
    return e;
}
} // namespace

TEST(Spatializer, HrtfModeHandsSpatialVoicesToTheBackendInListenerSpace) {
    AudioMixer m(48000, 4);
    auto fake = std::make_shared<FakeSpatializer>();
    m.setSpatializer(fake);
    m.setSpatialMode(SpatialMode::Hrtf);
    Listener l; // at the origin, looking down -z
    m.setListener(l);
    VoiceDesc d;
    d.sound = tone(0.05f);
    d.position = glm::vec3(0.0f, 2.0f, -2.0f); // ahead and above
    const uint32_t id = m.play(d);
    ASSERT_NE(id, 0u);
    VoiceDesc ui = d;
    ui.spatial = false; // not spatial: mixed as before, never sent to the backend
    ASSERT_NE(m.play(ui), 0u);
    std::vector<float> out(2 * 480);
    m.mix(out.data(), 480);
    ASSERT_EQ(fake->calls.size(), 1u);
    EXPECT_EQ(fake->calls[0].voice, id);
    EXPECT_EQ(fake->calls[0].frames, 480);
    EXPECT_NEAR(fake->calls[0].dir.x, 0.0f, 1e-4f);
    EXPECT_NEAR(fake->calls[0].dir.y, 0.7071f, 1e-3f);  // up
    EXPECT_NEAR(fake->calls[0].dir.z, -0.7071f, 1e-3f); // ahead is -z
    EXPECT_GT(fake->calls[0].to, 0.0f);
    EXPECT_GT(energy(out, 1), 0.0);   // the UI voice, both ears
    // The spatial voice ends within 0.05 s: released once, when it does.
    for (int i = 0; i < 6; ++i) m.mix(out.data(), 480);
    ASSERT_EQ(fake->released.size(), 2u); // the spatial voice and the UI one (the backend ignores ids it never saw)
    EXPECT_EQ(fake->released[0], id);
}

TEST(Spatializer, HrtfWithoutABackendFallsBackToBinaural) {
    AudioMixer m(48000, 4);
    m.setSpatialMode(SpatialMode::Hrtf);
    VoiceDesc d;
    d.sound = tone(0.1f);
    d.position = glm::vec3(3.0f, 0.0f, 0.0f); // right
    ASSERT_NE(m.play(d), 0u);
    std::vector<float> out(2 * 2400);
    m.mix(out.data(), 2400);
    EXPECT_GT(energy(out, 1), energy(out, 0) * 1.5); // still placed, by the built-in model
}

TEST(Spatializer, StoppingAndStealingReleaseTheBackendsVoices) {
    AudioMixer m(48000, 1);
    auto fake = std::make_shared<FakeSpatializer>();
    m.setSpatializer(fake);
    m.setSpatialMode(SpatialMode::Hrtf);
    VoiceDesc quiet;
    quiet.sound = tone(1.0f);
    quiet.position = glm::vec3(0.0f, 0.0f, -30.0f);
    const uint32_t a = m.play(quiet);
    VoiceDesc loud = quiet;
    loud.position = glm::vec3(0.0f, 0.0f, -1.0f);
    const uint32_t b = m.play(loud); // steals a
    ASSERT_NE(b, 0u);
    ASSERT_EQ(fake->released.size(), 1u);
    EXPECT_EQ(fake->released[0], a);
    m.stop(b);
    ASSERT_EQ(fake->released.size(), 2u);
    EXPECT_EQ(fake->released[1], b);
}

#if KKE_ENABLE_STEAM_AUDIO
TEST(SteamAudio, PlacesASoundOnTheRightAndAbove) {
    std::string err;
    SteamAudioSpatializer::Settings s;
    s.maxVoices = 2;
    auto sa = SteamAudioSpatializer::create(s, &err);
    ASSERT_TRUE(sa) << err;
    std::vector<float> mono(4800);
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.3f * std::sin(0.37f * float(i)) + 0.2f * std::sin(1.9f * float(i));
    auto render = [&](uint32_t voice, const glm::vec3& dir, int block) {
        std::vector<float> out(mono.size() * 2, 0.0f);
        for (size_t at = 0; at + size_t(block) <= mono.size(); at += size_t(block))
            sa->process(voice, mono.data() + at, block, dir, 1.0f, 1.0f, out.data() + at * 2);
        sa->release(voice);
        return out;
    };
    // Odd block sizes: the FIFO turns them into whole frames.
    const std::vector<float> right = render(1, glm::vec3(1.0f, 0.0f, 0.0f), 480);
    EXPECT_GT(energy(right, 1), energy(right, 0) * 3.0);
    const std::vector<float> left = render(2, glm::vec3(-1.0f, 0.0f, 0.0f), 333);
    EXPECT_GT(energy(left, 0), energy(left, 1) * 3.0);
    // The first frame is latency (silence); after it, sound.
    EXPECT_EQ(right[0], 0.0f);
    EXPECT_GT(energy(right, 1), 1.0);
    // Above and ahead differ (elevation), though both are centred.
    const std::vector<float> ahead = render(1, glm::vec3(0.0f, 0.0f, -1.0f), 480);
    const std::vector<float> above = render(1, glm::vec3(0.0f, 1.0f, 0.0f), 480);
    double diff = 0.0;
    for (size_t i = 0; i < ahead.size(); ++i) diff += std::fabs(double(ahead[i]) - above[i]);
    EXPECT_GT(diff, 10.0);
    EXPECT_EQ(sa->voicesInUse(), 0);
}

TEST(SteamAudio, AboutAsLoudAsTheBuiltInModelFromEveryDirection) {
    // Switching mode must not jump in loudness: both ears together carry
    // about the input's energy (the built-in pan's constant power), from
    // any direction.
    std::string err;
    auto sa = SteamAudioSpatializer::create({}, &err);
    ASSERT_TRUE(sa) << err;
    std::vector<float> mono(9600);
    for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.3f * std::sin(0.11f * float(i)) + 0.2f * std::sin(0.9f * float(i)) + 0.1f * std::sin(2.3f * float(i));
    double in = 0.0;
    for (size_t i = 256; i < mono.size() - 256; ++i) in += double(mono[i]) * mono[i];
    uint32_t voice = 1;
    for (const glm::vec3& dir : {glm::vec3(0, 0, -1), glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0), glm::vec3(-0.6f, -0.8f, 0)}) {
        std::vector<float> out(mono.size() * 2, 0.0f);
        for (size_t at = 0; at + 480 <= mono.size(); at += 480) sa->process(voice, mono.data() + at, 480, dir, 1.0f, 1.0f, out.data() + at * 2);
        sa->release(voice++);
        const double e = energy(out, 0) + energy(out, 1);
        EXPECT_GT(e, in * 0.5) << dir.x << " " << dir.y << " " << dir.z;
        EXPECT_LT(e, in * 2.0) << dir.x << " " << dir.y << " " << dir.z;
    }
}

TEST(SteamAudio, MoreVoicesThanEffectsPlayUnplacedNotSilent) {
    std::string err;
    SteamAudioSpatializer::Settings s;
    s.maxVoices = 1;
    auto sa = SteamAudioSpatializer::create(s, &err);
    ASSERT_TRUE(sa) << err;
    std::vector<float> mono(512, 0.5f), out(1024, 0.0f);
    sa->process(1, mono.data(), 512, glm::vec3(1, 0, 0), 1.0f, 1.0f, out.data());
    std::vector<float> out2(1024, 0.0f);
    sa->process(2, mono.data(), 512, glm::vec3(1, 0, 0), 1.0f, 1.0f, out2.data());
    EXPECT_EQ(sa->overflowed(), 1u);
    EXPECT_GT(energy(out2, 0), 1.0);
    EXPECT_NEAR(energy(out2, 0), energy(out2, 1), 1e-6);
}

TEST(SteamAudio, ABadSofaFileIsReportedNotFatal) {
    std::string err;
    SteamAudioSpatializer::Settings s;
    s.sofaFile = "/nonexistent/ears.sofa";
    EXPECT_FALSE(SteamAudioSpatializer::create(s, &err));
    EXPECT_NE(err.find("ears.sofa"), std::string::npos);
}
#endif
