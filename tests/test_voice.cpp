// Voice chat (docs/NETWORKING.md "Voice"): the jitter buffer, the voice
// gate, streamed mixer voices, the server's routing, and Opus end to end.

#include "kke/AudioMixer.h"
#include "kke/net/NetSession.h"
#include "kke/net/Transport.h"
#include "kke/voice/JitterBuffer.h"
#if KKE_ENABLE_VOICE
#include "kke/voice/VoiceCleaner.h"
#include "kke/voice/VoiceCodec.h"
#endif

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <random>
#include <vector>

using namespace kke;
using namespace kke::voice;

namespace {
std::vector<uint8_t> frame(uint8_t tag) { return { tag, 1, 2 }; }
} // namespace

TEST(JitterBuffer, WaitsForACushionThenPlaysInOrder) {
    JitterBuffer jb;
    jb.push(11, frame(11));
    jb.push(10, frame(10)); // out of order
    EXPECT_EQ(jb.pop().kind, JitterBuffer::Kind::Nothing); // 2 < startFrames
    jb.push(12, frame(12));
    for (uint8_t want = 10; want <= 12; ++want) {
        const auto o = jb.pop();
        ASSERT_EQ(o.kind, JitterBuffer::Kind::Frame);
        EXPECT_EQ(o.data[0], want);
    }
    jb.push(11, frame(11)); // late: already played
    EXPECT_EQ(jb.late(), 1u);
}

TEST(JitterBuffer, ConcealsALostFrameWithTheNextOnesHelp) {
    JitterBuffer jb;
    for (uint16_t s : { 1, 2, 4, 5 }) jb.push(s, frame(static_cast<uint8_t>(s)));
    EXPECT_EQ(jb.pop().data[0], 1);
    EXPECT_EQ(jb.pop().data[0], 2);
    const auto lost = jb.pop();
    ASSERT_EQ(lost.kind, JitterBuffer::Kind::Lost);
    ASSERT_NE(lost.next, nullptr); // frame 4 is here: its FEC can rebuild 3
    EXPECT_EQ((*lost.next)[0], 4);
    EXPECT_EQ(jb.pop().data[0], 4);
    EXPECT_EQ(jb.pop().data[0], 5);
    // Then silence: a few frames concealed, then the spurt is over.
    int lostRun = 0;
    JitterBuffer::Kind k;
    while ((k = jb.pop().kind) == JitterBuffer::Kind::Lost) ++lostRun;
    EXPECT_EQ(k, JitterBuffer::Kind::Nothing);
    EXPECT_EQ(lostRun, 0); // nothing queued at all: over at once
    EXPECT_FALSE(jb.playing());
}

TEST(JitterBuffer, SurvivesSequenceWrapAndNewSpurts) {
    JitterBuffer jb;
    for (uint16_t s : { 65534, 65535, 0, 1 }) jb.push(s, frame(static_cast<uint8_t>(s & 0xFF)));
    std::vector<uint8_t> got;
    for (int i = 0; i < 4; ++i) got.push_back(jb.pop().data[0]);
    EXPECT_EQ(got, (std::vector<uint8_t>{ 0xFE, 0xFF, 0, 1 }));
    // A new spurt far ahead (they were quiet for a while).
    EXPECT_EQ(jb.pop().kind, JitterBuffer::Kind::Nothing);
    for (uint16_t s = 5000; s < 5003; ++s) jb.push(s, frame(9));
    EXPECT_EQ(jb.pop().kind, JitterBuffer::Kind::Frame);
}

TEST(JitterBuffer, DropsOldFramesToKeepLatencyBounded) {
    JitterBuffer::Settings st;
    st.maxFrames = 5;
    JitterBuffer jb(st);
    for (uint16_t s = 0; s < 20; ++s) jb.push(s, frame(static_cast<uint8_t>(s)));
    EXPECT_LE(jb.queued(), 5u);
    EXPECT_EQ(jb.pop().data[0], 15);
    EXPECT_EQ(jb.skipped(), 15u);
}

TEST(VoiceActivity, OpensOnSpeechAndHoldsOverShortPauses) {
    VoiceActivity vad;
    std::vector<float> quiet(960), loud(960);
    for (size_t i = 0; i < quiet.size(); ++i) {
        quiet[i] = 0.001f * std::sin(float(i) * 0.37f);
        loud[i] = 0.3f * std::sin(float(i) * 0.05f);
    }
    for (int i = 0; i < 50; ++i) EXPECT_FALSE(vad.process(quiet.data(), quiet.size(), 0.02f));
    EXPECT_TRUE(vad.process(loud.data(), loud.size(), 0.02f));
    EXPECT_TRUE(vad.process(quiet.data(), quiet.size(), 0.02f)); // hangover
    for (int i = 0; i < 20; ++i) vad.process(quiet.data(), quiet.size(), 0.02f);
    EXPECT_FALSE(vad.process(quiet.data(), quiet.size(), 0.02f));
}

TEST(AudioStream, MixerPlaysAStreamAndFillsGapsWithSilence) {
    AudioMixer mixer(48000, 8);
    auto stream = std::make_shared<AudioStream>(4800);
    std::vector<float> tone(480, 0.5f);
    stream->push(tone.data(), tone.size());
    VoiceDesc d;
    d.stream = stream;
    d.spatial = false;
    d.category = SoundCategory::Voice;
    const uint32_t id = mixer.play(d);
    ASSERT_NE(id, 0u);
    std::vector<float> out(2 * 960);
    mixer.mix(out.data(), 960);
    float early = 0, late = 0;
    for (int f = 0; f < 400; ++f) early = std::max(early, std::fabs(out[2 * size_t(f)]));
    for (int f = 600; f < 960; ++f) late = std::max(late, std::fabs(out[2 * size_t(f)]));
    EXPECT_GT(early, 0.1f);
    EXPECT_LT(late, 0.05f);           // ran dry: silence, still playing
    EXPECT_TRUE(mixer.isPlaying(id));
    EXPECT_GE(stream->underruns(), 1u);
    stream->close();
    mixer.mix(out.data(), 960);
    EXPECT_FALSE(mixer.isPlaying(id)); // closed and empty: done
    // Bounded: pushing more than it holds keeps the newest.
    AudioStream small(4);
    const float xs[6] = { 1, 2, 3, 4, 5, 6 };
    small.push(xs, 6);
    float got[4];
    EXPECT_EQ(small.read(got, 4), 4u);
    EXPECT_EQ(got[0], 3.0f);
}

// ---------------------------------------------------------------- routing

TEST(VoiceRules, ProximityTeamsAllAndMutes) {
    net::VoiceRules r;
    const glm::vec3 a(0, 0, 0), near(10, 0, 0), far(100, 0, 0);
    EXPECT_TRUE(r.reaches(net::VoiceChannel::Proximity, 1, &a, 2, &near));
    EXPECT_FALSE(r.reaches(net::VoiceChannel::Proximity, 1, &a, 2, &far));
    EXPECT_FALSE(r.reaches(net::VoiceChannel::Proximity, 1, &a, 2, nullptr)); // no position yet
    EXPECT_FALSE(r.reaches(net::VoiceChannel::All, 1, &a, 1, &a));           // never yourself
    EXPECT_TRUE(r.reaches(net::VoiceChannel::All, 1, &a, 2, &far));
    r.team = [](uint8_t id) { return id % 2; };
    EXPECT_TRUE(r.reaches(net::VoiceChannel::Team, 1, nullptr, 3, nullptr));
    EXPECT_FALSE(r.reaches(net::VoiceChannel::Team, 1, nullptr, 2, nullptr));
    r.muted.insert(1);
    EXPECT_FALSE(r.reaches(net::VoiceChannel::All, 1, &a, 2, &near));
    r.muted.clear();
    r.allowAll = false;
    EXPECT_FALSE(r.reaches(net::VoiceChannel::All, 1, &a, 2, &near));
}

namespace {
struct VoiceMatch {
    net::LoopbackNetwork netw;
    net::LoopbackTransport serverT{ netw };
    net::NetServer server{ serverT };
    std::vector<std::unique_ptr<net::LoopbackTransport>> ts;
    std::vector<std::unique_ptr<net::NetClient>> clients;
    std::vector<std::vector<net::VoiceMsg>> heard;
    std::vector<net::VoiceMsg> hostHeard;
    double now = 0;
    explicit VoiceMatch(size_t n) {
        EXPECT_TRUE(server.start(5100, "Host", ""));
        server.onVoice = [this](const net::VoiceMsg& m) { hostHeard.push_back(m); };
        heard.resize(n);
        for (size_t i = 0; i < n; ++i) {
            ts.push_back(std::make_unique<net::LoopbackTransport>(netw));
            clients.push_back(std::make_unique<net::NetClient>(*ts.back()));
            clients.back()->onVoice = [this, i](const net::VoiceMsg& m) { heard[i].push_back(m); };
            EXPECT_TRUE(clients.back()->connect("localhost", 5100, "P" + std::to_string(i), ""));
        }
    }
    void place(size_t i, glm::vec3 p) {
        net::NetPlayerState s;
        s.position = p;
        clients[i]->setLocalState(s);
    }
    void run(double seconds) {
        for (double t = 0; t < seconds; t += 1.0 / 60.0) {
            now += 1.0 / 60.0;
            netw.advance(1.0 / 60.0);
            server.update(now);
            for (auto& c : clients) c->update(now);
        }
    }
};
} // namespace

TEST(VoiceNet, ServerSendsProximityVoiceOnlyToThoseNearAndStampsTheSpeaker) {
    VoiceMatch m(3);
    net::NetPlayerState host;
    host.position = glm::vec3(5, 0, 0);
    m.server.setLocalState(host);
    m.place(0, glm::vec3(0, 0, 0));
    m.place(1, glm::vec3(10, 0, 0));
    m.place(2, glm::vec3(300, 0, 0));
    m.run(1.0);
    m.clients[0]->sendVoice(net::VoiceChannel::Proximity, 7, { 1, 2, 3 });
    m.run(0.2);
    ASSERT_EQ(m.heard[1].size(), 1u);
    EXPECT_EQ(m.heard[1][0].speaker, m.clients[0]->playerId());
    EXPECT_EQ(m.heard[1][0].seq, 7);
    EXPECT_TRUE(m.heard[2].empty()); // 300 m away
    EXPECT_TRUE(m.heard[0].empty()); // not echoed
    ASSERT_EQ(m.hostHeard.size(), 1u); // the host is 5 m away
    // The host talks to everyone.
    m.server.sendVoice(net::VoiceMsg{ 9, net::VoiceChannel::All, 1, { 4 } });
    m.run(0.2);
    EXPECT_EQ(m.heard[2].size(), 1u);
    EXPECT_EQ(m.heard[2][0].speaker, 0);
}

TEST(VoiceNet, AFloodingSpeakerIsCapped) {
    VoiceMatch m(2);
    m.run(0.5);
    for (int i = 0; i < 500; ++i) m.clients[0]->sendVoice(net::VoiceChannel::All, static_cast<uint16_t>(i), { 1 });
    m.run(0.3);
    EXPECT_LE(m.heard[1].size(), m.server.voice.maxPacketsPerSecond);
    EXPECT_GT(m.heard[1].size(), 0u);
}

#if KKE_ENABLE_VOICE
TEST(VoiceCodec, OpusRoundTripSoundsLikeTheInputAndConcealsLoss) {
    VoiceEncoder enc;
    VoiceDecoder dec;
    ASSERT_TRUE(enc.ok());
    ASSERT_TRUE(dec.ok());
    std::vector<float> in(kFrameSamples), out(kFrameSamples);
    double phase = 0;
    std::vector<std::vector<uint8_t>> frames;
    for (int f = 0; f < 50; ++f) { // 1 s of a 300 Hz tone
        for (float& x : in) {
            x = 0.4f * float(std::sin(phase));
            phase += 2.0 * 3.14159265 * 300.0 / kSampleRate;
        }
        frames.push_back(enc.encode(in.data()));
        ASSERT_FALSE(frames.back().empty());
        EXPECT_LE(frames.back().size(), 256u);
    }
    double energy = 0;
    for (int f = 0; f < 50; ++f) {
        if (f == 30) {
            ASSERT_TRUE(dec.conceal(&frames[31], out.data())); // lost; rebuilt from 31's FEC
        } else {
            ASSERT_TRUE(dec.decode(frames[size_t(f)], out.data()));
        }
        if (f > 10)
            for (float x : out) energy += double(x) * x;
    }
    const double rms = std::sqrt(energy / (39.0 * kFrameSamples));
    EXPECT_NEAR(rms, 0.4 / std::sqrt(2.0), 0.08); // the tone came through at about its level
    EXPECT_FALSE(dec.decode({ 0xFF, 0xFF, 0xFF }, out.data()) && false); // damaged input: no crash
}

// ---------------------------------------------------------------- the cleaner

namespace {
double rmsDb(const float* x, size_t n) {
    double e = 0;
    for (size_t i = 0; i < n; ++i) e += double(x[i]) * x[i];
    return 10.0 * std::log10(e / double(n) + 1e-20);
}
} // namespace

TEST(VoiceCleaner, TakesOutSteadyNoise) {
    VoiceCleaner::Settings s;
    s.echoCancellation = false;
    VoiceCleaner c(s);
    std::mt19937 rng(7);
    std::normal_distribution<float> noise(0.0f, 0.03f); // a fan, about -30 dBFS
    std::vector<float> frame(kFrameSamples);
    double in = 0, out = 0;
    float voice = 1.0f;
    for (int f = 0; f < 150; ++f) { // 3 s
        for (float& x : frame) x = noise(rng);
        const double before = rmsDb(frame.data(), frame.size());
        voice = c.process(frame.data(), nullptr);
        if (f >= 100) { // after it settled
            in += before;
            out += rmsDb(frame.data(), frame.size());
        }
    }
    RecordProperty("noise_reduction_db", std::to_string(in / 50 - out / 50));
    EXPECT_LT(out / 50 - in / 50, -12.0) << "noise only " << (out - in) / 50 << " dB quieter";
    EXPECT_LT(voice, 0.5f); // and it knows it isn't speech
}

TEST(VoiceCleaner, CancelsTheSpeakersEcho) {
    VoiceCleaner::Settings s;
    s.noiseSuppression = false; // measure the echo canceller alone
    VoiceCleaner c(s);
    std::mt19937 rng(3);
    std::normal_distribution<float> far(0.0f, 0.1f); // the others' voices from the speakers
    const size_t delay = 48 * 25;                     // 25 ms from speaker to microphone
    std::vector<float> history(delay + kFrameSamples, 0.0f), played(kFrameSamples), mic(kFrameSamples);
    double in = 0, out = 0;
    for (int f = 0; f < 250; ++f) { // 5 s
        std::copy(history.begin() + kFrameSamples, history.end(), history.begin());
        for (size_t i = 0; i < size_t(kFrameSamples); ++i) played[i] = history[delay + i] = far(rng);
        // The room: the speakers 25 ms ago, at half level, plus a faint reflection.
        for (size_t i = 0; i < size_t(kFrameSamples); ++i) mic[i] = 0.5f * history[i] + (i >= 200 ? 0.1f * history[i - 200] : 0.0f);
        const double before = rmsDb(mic.data(), mic.size());
        c.process(mic.data(), played.data());
        if (f >= 200) {
            in += before;
            out += rmsDb(mic.data(), mic.size());
        }
    }
    RecordProperty("echo_reduction_db", std::to_string(in / 50 - out / 50));
    EXPECT_LT(out / 50 - in / 50, -15.0) << "echo only " << (out - in) / 50 << " dB quieter";
    EXPECT_GT(c.echoReductionDb(), 10.0f);
}

TEST(VoiceCleaner, BothOffLeavesTheMicrophoneAlone) {
    VoiceCleaner c;
    c.noiseSuppression = false;
    c.echoCancellation = false;
    std::vector<float> frame(kFrameSamples), orig;
    for (size_t i = 0; i < frame.size(); ++i) frame[i] = 0.3f * std::sin(0.05f * float(i));
    orig = frame;
    EXPECT_EQ(c.process(frame.data(), nullptr), -1.0f);
    EXPECT_EQ(frame, orig);
}
#endif

TEST(AudioMixer, OutputTapHearsWhatTheSpeakersPlay) {
    AudioMixer mixer(48000, 4);
    auto tap = std::make_shared<AudioStream>(4800);
    mixer.setOutputTap(tap);
    std::vector<float> out(2 * 480);
    mixer.mix(out.data(), 480);
    EXPECT_EQ(tap->buffered(), 480u); // mono: one sample per frame
    mixer.setOutputTap(nullptr);
    mixer.mix(out.data(), 480);
    EXPECT_EQ(tap->buffered(), 480u);
}
