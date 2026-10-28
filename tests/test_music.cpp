// Adaptive music (kke/MusicDirector.h, AudioMixer::playScheduled, ENG-F39).
#include "kke/MusicDirector.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <set>

using namespace kke;
using nlohmann::json;

namespace {

constexpr int kRate = 48000;

SoundHandle makeSound(float value, double seconds) {
    auto b = std::make_shared<SoundBuffer>();
    b->sampleRate = kRate;
    b->samples.assign(static_cast<size_t>(seconds * kRate), value);
    return b;
}

VoiceDesc voice(const SoundHandle& s, bool loop = false) {
    VoiceDesc d;
    d.sound = s;
    d.spatial = false;
    d.loop = loop;
    d.category = SoundCategory::Music;
    d.reverbSend = 0.0f;
    return d;
}

// The left channel of `frames` frames, mixed in blocks of `block`.
std::vector<float> render(AudioMixer& m, int frames, int block = 441) {
    std::vector<float> left;
    std::vector<float> out(static_cast<size_t>(block) * 2);
    for (int done = 0; done < frames;) {
        const int n = std::min(block, frames - done);
        m.mix(out.data(), n);
        for (int i = 0; i < n; ++i) left.push_back(out[static_cast<size_t>(2 * i)]);
        done += n;
    }
    return left;
}

// The first frame that is not silence, or -1.
long onset(const std::vector<float>& left, float threshold = 0.05f) {
    for (size_t i = 0; i < left.size(); ++i)
        if (std::fabs(left[i]) > threshold) return static_cast<long>(i);
    return -1;
}

} // namespace

// ---- the mixer: voices that start at a musical moment -------------------------------

TEST(ScheduledVoices, StartOnTheExactSampleWhateverTheBlockSize) {
    for (int block : { 441, 480, 512, 1000, 64 }) {
        AudioMixer mixer(kRate, 32);
        const uint32_t ref = mixer.play(voice(makeSound(0.0f, 1.0), true));
        ASSERT_NE(ref, 0u);
        AudioMixer::VoiceSchedule when;
        when.reference = ref;
        when.atSeconds = 0.5;
        const std::vector<uint32_t> ids = mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, when);
        ASSERT_EQ(ids.size(), 1u);
        ASSERT_NE(ids[0], 0u);
        const std::vector<float> out = render(mixer, 30000, block);
        const long at = onset(out);
        EXPECT_NEAR(static_cast<double>(at), 24000.0, 1.0) << "block " << block;
    }
}

TEST(ScheduledVoices, AGroupStartsTogether) {
    AudioMixer mixer(kRate, 32);
    const uint32_t ref = mixer.play(voice(makeSound(0.0f, 1.0), true));
    AudioMixer::VoiceSchedule when;
    when.reference = ref;
    when.atSeconds = 0.25;
    // two voices that are silent apart from a click at their first sample: one positive, one negative
    auto click = [](float v) {
        auto b = std::make_shared<SoundBuffer>();
        b->sampleRate = kRate;
        b->samples.assign(4800, 0.0f);
        b->samples[0] = v;
        b->samples[1] = v;
        return SoundHandle(b);
    };
    const std::vector<uint32_t> ids = mixer.playScheduled({ voice(click(0.6f)), voice(click(0.4f)) }, when);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_NE(ids[0], 0u);
    EXPECT_NE(ids[1], 0u);
    EXPECT_NE(ids[0], ids[1]);
    EXPECT_TRUE(mixer.isPlaying(ids[0])) << "waiting counts as playing";
    EXPECT_TRUE(mixer.isPlaying(ids[1]));
    const std::vector<float> out = render(mixer, 14000, 333);
    // both clicks land on one frame: the sum is bigger than either alone (0.6 * lowpass * pan, 0.4 ...)
    const long at = onset(out, 0.2f);
    EXPECT_NEAR(static_cast<double>(at), 12000.0, 1.0);
}

TEST(ScheduledVoices, TheOldVoicesFadeOutAtThatMomentAndEnd) {
    AudioMixer mixer(kRate, 32);
    const uint32_t ref = mixer.play(voice(makeSound(0.5f, 1.0), true));
    AudioMixer::VoiceSchedule when;
    when.reference = ref;
    when.atSeconds = 0.5;
    when.fadeOut = { ref };
    when.fadeOutSeconds = 0.1f;
    const std::vector<uint32_t> ids = mixer.playScheduled({ voice(makeSound(-0.5f, 2.0), true) }, when);
    ASSERT_NE(ids[0], 0u);
    const std::vector<float> out = render(mixer, 48000, 480);
    EXPECT_GT(out[10000], 0.1f) << "the old one plays before";
    EXPECT_LT(out[40000], -0.1f) << "and the new one after";
    EXPECT_FALSE(mixer.isPlaying(ref)) << "the faded-out voice has ended";
    EXPECT_TRUE(mixer.isPlaying(ids[0]));
    // the crossfade passes through silence-ish in between rather than doubling up
    EXPECT_LT(std::fabs(out[24000 + 2400]), 0.35f);
}

TEST(ScheduledVoices, ThePitchOfTheReferenceCountsAndTheLoopWraps) {
    {
        AudioMixer mixer(kRate, 32);
        VoiceDesc fast = voice(makeSound(0.0f, 1.0), true);
        fast.pitch = 2.0f;
        const uint32_t ref = mixer.play(fast);
        AudioMixer::VoiceSchedule when;
        when.reference = ref;
        when.atSeconds = 0.5; // half a second of its own sound: a quarter of a second of ours
        mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, when);
        EXPECT_NEAR(static_cast<double>(onset(render(mixer, 20000))), 12000.0, 2.0);
    }
    {
        AudioMixer mixer(kRate, 32);
        const uint32_t ref = mixer.play(voice(makeSound(0.0f, 1.0), true));
        render(mixer, 43200); // 0.9 s into the loop
        EXPECT_NEAR(mixer.voiceSeconds(ref), 0.9, 0.01);
        AudioMixer::VoiceSchedule when;
        when.reference = ref;
        when.atSeconds = 0.2; // already past: the next time round, 0.3 s from now
        mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, when);
        EXPECT_NEAR(static_cast<double>(onset(render(mixer, 20000))), 14400.0, 2.0);
    }
    {
        AudioMixer mixer(kRate, 32);
        const uint32_t ref = mixer.play(voice(makeSound(0.0f, 1.0), true));
        render(mixer, 4800);
        AudioMixer::VoiceSchedule when;
        when.reference = ref;
        when.atSeconds = 1.0; // the end of the loop
        mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, when);
        EXPECT_NEAR(static_cast<double>(onset(render(mixer, 48000))), 43200.0, 2.0);
    }
}

TEST(ScheduledVoices, WithoutAReferenceTheyStartAtTheNextBlock) {
    AudioMixer mixer(kRate, 32);
    AudioMixer::VoiceSchedule none;
    mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, none);
    EXPECT_EQ(onset(render(mixer, 2000)), 0);
    AudioMixer other(kRate, 32);
    AudioMixer::VoiceSchedule gone;
    gone.reference = 999; // no such voice
    gone.atSeconds = 3.0;
    other.playScheduled({ voice(makeSound(0.5f, 0.5)) }, gone);
    EXPECT_EQ(onset(render(other, 2000)), 0) << "a reference that is gone is not waited for";
}

TEST(ScheduledVoices, AWaitingVoiceCanBeStoppedBeforeItStarts) {
    AudioMixer mixer(kRate, 32);
    const uint32_t ref = mixer.play(voice(makeSound(0.0f, 1.0), true));
    AudioMixer::VoiceSchedule when;
    when.reference = ref;
    when.atSeconds = 0.5;
    when.fadeOut = { ref };
    const std::vector<uint32_t> ids = mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, when);
    ASSERT_TRUE(mixer.isPlaying(ids[0]));
    mixer.stop(ids[0]);
    EXPECT_FALSE(mixer.isPlaying(ids[0]));
    const std::vector<float> out = render(mixer, 30000);
    EXPECT_EQ(onset(out), -1) << "it never started";
    EXPECT_TRUE(mixer.isPlaying(ref)) << "and the one it was to replace was not faded";
}

TEST(ScheduledVoices, ABridgeStartsWhatFollowsIt) {
    AudioMixer mixer(kRate, 32);
    const uint32_t ref = mixer.play(voice(makeSound(0.0f, 1.0), true));
    AudioMixer::VoiceSchedule first;
    first.reference = ref;
    first.atSeconds = 0.5;
    const std::vector<uint32_t> bridge = mixer.playScheduled({ voice(makeSound(0.0f, 0.25)) }, first); // silent, a quarter second long
    ASSERT_NE(bridge[0], 0u);
    AudioMixer::VoiceSchedule second;
    second.reference = bridge[0];
    second.atSeconds = 0.25; // when the bridge has played to its end
    mixer.playScheduled({ voice(makeSound(0.5f, 0.5)) }, second);
    const long at = onset(render(mixer, 48000));
    EXPECT_NEAR(static_cast<double>(at), 24000.0 + 12000.0, 3.0) << "0.5 s for the bridge to start, 0.25 s long";
}

TEST(ScheduledVoices, PositionAndLengthAreReported) {
    AudioMixer mixer(kRate, 32);
    const uint32_t id = mixer.play(voice(makeSound(0.1f, 2.0), true));
    EXPECT_NEAR(mixer.voiceLength(id), 2.0, 1e-6);
    EXPECT_NEAR(mixer.voiceSeconds(id), 0.0, 1e-6);
    render(mixer, 4800);
    EXPECT_NEAR(mixer.voiceSeconds(id), 0.1, 0.002);
    EXPECT_EQ(mixer.voiceLength(12345), 0.0);
    EXPECT_EQ(mixer.voiceSeconds(12345), 0.0);
}

// ---- the director, with a backend that keeps an exact clock ------------------------

namespace {

// What the mixer does, in closed form: every voice has a start time (found from its
// reference and `atSeconds`), and old voices end when a new one that fades them starts.
class FakeBackend : public MusicBackend {
public:
    struct V {
        std::string sound;
        float gain = 1.0f;
        bool loop = true;
        float fadeIn = 0.0f;
        uint32_t ref = 0;
        double at = 0.0, scheduled = 0.0, length = 4.0;
        bool stopped = false;
        std::vector<std::pair<uint32_t, float>> killers; // voices whose start fades this one
    };
    double clock = 0.0;
    std::map<std::string, double> lengths;
    std::set<std::string> broken;
    std::map<uint32_t, V> voices;
    std::vector<std::string> log;
    uint32_t next = 1;

    std::vector<uint32_t> start(const std::vector<MusicVoiceSpec>& specs, uint32_t reference, double atSeconds, const std::vector<uint32_t>& fadeOut,
                                float fadeOutSeconds) override {
        std::vector<uint32_t> ids;
        for (const MusicVoiceSpec& s : specs) {
            if (broken.count(s.sound)) {
                ids.push_back(0);
                continue;
            }
            V v;
            v.sound = s.sound;
            v.gain = s.gain;
            v.loop = s.loop;
            v.fadeIn = s.fadeIn;
            v.ref = reference;
            v.at = atSeconds;
            v.scheduled = clock;
            v.length = lengths.count(s.sound) ? lengths[s.sound] : 4.0;
            const uint32_t id = next++;
            voices[id] = v;
            for (uint32_t old : fadeOut)
                if (voices.count(old)) voices[old].killers.emplace_back(id, fadeOutSeconds);
            ids.push_back(id);
        }
        std::string line = "start";
        for (uint32_t id : ids) line += ":" + std::to_string(id);
        log.push_back(line + " ref=" + std::to_string(reference) + " at=" + std::to_string(atSeconds));
        return ids;
    }
    void setGain(uint32_t voice, float gain, float seconds) override {
        voices[voice].gain = gain;
        log.push_back("gain:" + std::to_string(voice) + ":" + std::to_string(gain) + ":" + std::to_string(seconds));
    }
    void stop(uint32_t voice, float fade) override {
        if (voices.count(voice)) voices[voice].stopped = true;
        log.push_back("stop:" + std::to_string(voice) + ":" + std::to_string(fade));
    }
    double startTime(uint32_t id) const {
        const V& v = voices.at(id);
        const auto r = voices.find(v.ref);
        if (v.ref == 0 || r == voices.end()) return v.scheduled;
        const double rs = startTime(v.ref);
        if (r->second.loop && r->second.length > 0.0) {
            double t = rs + std::fmod(v.at, r->second.length);
            while (t < v.scheduled - 1e-9) t += r->second.length;
            return t;
        }
        return std::max(rs + v.at, v.scheduled);
    }
    double endTime(uint32_t id) const { // when fading kills it
        double t = std::numeric_limits<double>::infinity();
        for (const auto& [killer, fade] : voices.at(id).killers)
            if (!voices.at(killer).stopped) t = std::min(t, startTime(killer) + fade);
        return t;
    }
    double position(uint32_t id) const override {
        const auto it = voices.find(id);
        if (it == voices.end()) return 0.0;
        const double elapsed = clock - startTime(id);
        if (elapsed <= 0.0) return 0.0;
        return it->second.loop ? std::fmod(elapsed, it->second.length) : std::min(elapsed, it->second.length);
    }
    double length(uint32_t id) const override {
        const auto it = voices.find(id);
        return it == voices.end() ? 0.0 : it->second.length;
    }
    bool playing(uint32_t id) const override {
        const auto it = voices.find(id);
        if (it == voices.end() || it->second.stopped) return false;
        if (clock >= endTime(id)) return false;
        const double elapsed = clock - startTime(id);
        return it->second.loop || elapsed < it->second.length;
    }
    void advance(double dt) { clock += dt; }
    bool started(uint32_t id) const { return clock > startTime(id); }
};

MusicSegmentDef explore() {
    MusicSegmentDef s;
    s.name = "explore";
    s.bpm = 120.0f;
    s.beatsPerBar = 4;
    s.bars = 2; // 4 s
    MusicLayerDef pad;
    pad.sound = "pad";
    MusicLayerDef drums;
    drums.sound = "drums";
    drums.from = 0.3f;
    drums.full = 0.6f;
    MusicLayerDef strings;
    strings.sound = "strings";
    strings.fadeFrom = 0.8f;
    strings.to = 1.0f;
    s.layers = { pad, drums, strings };
    return s;
}

MusicSegmentDef combat() {
    MusicSegmentDef s = explore();
    s.name = "combat";
    s.bpm = 140.0f;
    return s;
}

struct Rig {
    FakeBackend backend;
    MusicDirector director{ &backend };
    Rig() {
        EXPECT_TRUE(director.defineSegment(explore()));
        EXPECT_TRUE(director.defineSegment(combat()));
    }
    void run(double seconds, double step = 0.05) {
        for (double t = 0.0; t < seconds - 1e-9; t += step) {
            backend.advance(step);
            director.update(static_cast<float>(step));
        }
    }
};

bool hasEvent(const std::vector<MusicDirector::Event>& events, MusicDirector::Event::Kind kind, const std::string& name = {}) {
    for (const auto& e : events)
        if (e.kind == kind && (name.empty() || e.name == name)) return true;
    return false;
}

} // namespace

TEST(MusicDirector, LayersFollowTheirParameters) {
    Rig r;
    ASSERT_TRUE(r.director.play("explore"));
    EXPECT_EQ(r.director.current(), "explore");
    EXPECT_EQ(r.backend.voices.size(), 3u);
    EXPECT_NEAR(r.director.layerGain(0), 1.0f, 1e-6f) << "pad: always";
    EXPECT_NEAR(r.director.layerGain(1), 0.0f, 1e-6f) << "drums: not yet (it starts anyway, silent, to be in time)";
    EXPECT_NEAR(r.director.layerGain(2), 1.0f, 1e-6f);
    EXPECT_NEAR(r.backend.voices[2].gain, 0.0f, 1e-6f);
    r.director.setIntensity(0.45f);
    r.run(0.05);
    EXPECT_NEAR(r.director.layerGain(1), 0.5f, 1e-5f) << "halfway up the drums' ramp";
    EXPECT_NEAR(r.backend.voices[2].gain, 0.5f, 1e-5f);
    r.director.setIntensity(0.9f);
    r.run(0.05);
    EXPECT_NEAR(r.director.layerGain(1), 1.0f, 1e-6f);
    EXPECT_NEAR(r.director.layerGain(2), 0.5f, 1e-5f) << "the strings fade out between 0.8 and 1.0";
    r.director.setIntensity(1.5f);
    r.run(0.05);
    EXPECT_NEAR(r.director.layerGain(2), 0.0f, 1e-6f);
    EXPECT_NEAR(r.director.parameter("intensity"), 1.5f, 1e-6f);
    EXPECT_EQ(r.director.parameter("unknown"), 0.0f);
    // a change that is no change sends nothing
    const size_t before = r.backend.log.size();
    r.run(0.05);
    EXPECT_EQ(r.backend.log.size(), before);
}

TEST(MusicDirector, ALayerOnADifferentParameterAndASwitch) {
    Rig r;
    MusicSegmentDef s = explore();
    s.name = "custom";
    s.layers[1].parameter = "danger";
    s.layers[1].from = 0.5f;
    s.layers[1].full = 0.5f; // a switch
    s.layers[1].gain = 0.8f;
    ASSERT_TRUE(r.director.defineSegment(s));
    ASSERT_TRUE(r.director.play("custom"));
    EXPECT_NEAR(r.director.layerGain(1), 0.0f, 1e-6f);
    r.director.setParameter("danger", 0.49f);
    r.run(0.05);
    EXPECT_NEAR(r.director.layerGain(1), 0.0f, 1e-6f);
    r.director.setParameter("danger", 0.5f);
    r.run(0.05);
    EXPECT_NEAR(r.director.layerGain(1), 0.8f, 1e-6f);
    r.director.setIntensity(1.0f); // another parameter: no effect on it
    r.run(0.05);
    EXPECT_NEAR(r.director.layerGain(1), 0.8f, 1e-6f);
    r.director.setParameter("danger", std::nanf("")); // ignored
    EXPECT_NEAR(r.director.parameter("danger"), 0.5f, 1e-6f);
}

TEST(MusicDirector, BeatsAndBarsAreReportedAsTheSegmentPlays) {
    Rig r;
    ASSERT_TRUE(r.director.play("explore"));
    r.director.update(0.0f);
    auto ev = r.director.takeEvents();
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Segment, "explore"));
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Beat)) << "the first beat is heard at once";
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Bar));
    EXPECT_TRUE(r.director.takeEvents().empty()) << "and taken once";
    // 120 bpm: a beat every half second, a bar (4 beats) every 2 s
    std::vector<std::pair<int, int>> beats; // beat in bar, bar
    int bars = 0;
    r.run(0.01); // not exactly on a beat: sampling a boundary to the last bit would be a coin toss for either side
    for (int i = 0; i < 16; ++i) {
        r.run(0.5);
        for (const auto& e : r.director.takeEvents()) {
            if (e.kind == MusicDirector::Event::Kind::Beat) beats.emplace_back(e.beat, e.bar);
            if (e.kind == MusicDirector::Event::Kind::Bar) ++bars;
        }
    }
    ASSERT_EQ(beats.size(), 16u);
    EXPECT_EQ(beats[0], (std::pair<int, int>{ 1, 0 }));
    EXPECT_EQ(beats[2], (std::pair<int, int>{ 3, 0 }));
    EXPECT_EQ(beats[3], (std::pair<int, int>{ 0, 1 })) << "the second bar";
    EXPECT_EQ(bars, 4) << "8 s is four bars of 2 s (the loop wraps at 4 s)";
}

TEST(MusicDirector, ATransitionWaitsForTheNextBarAndStartsThereExactly) {
    Rig r;
    ASSERT_TRUE(r.director.play("explore"));
    r.run(2.3);
    r.director.takeEvents();
    ASSERT_TRUE(r.director.play("combat"));
    EXPECT_EQ(r.director.current(), "explore");
    EXPECT_EQ(r.director.pending(), "combat");
    // the combat voices (ids 4-6) start at the bar line 4.0 s, from the explore voice (id 1)
    ASSERT_TRUE(r.backend.voices.count(4));
    EXPECT_NEAR(r.backend.startTime(4), 4.0, 1e-9);
    EXPECT_EQ(r.backend.voices[4].ref, 1u);
    EXPECT_NEAR(r.backend.endTime(1), 4.0 + 0.5, 1e-9) << "the old layers fade out over the default half second from that moment";
    r.run(1.5); // 3.8
    EXPECT_EQ(r.director.current(), "explore");
    r.run(0.4); // 4.2
    EXPECT_EQ(r.director.current(), "combat");
    EXPECT_EQ(r.director.pending(), "");
    EXPECT_TRUE(hasEvent(r.director.takeEvents(), MusicDirector::Event::Kind::Segment, "combat"));
    r.run(0.4);
    EXPECT_FALSE(r.backend.playing(1)) << "the old voices are gone";
    EXPECT_TRUE(r.backend.playing(4));
}

TEST(MusicDirector, TheTransitionRulesPickTheMoment) {
    auto startOf = [](MusicWhen when, double pos, int grid = 4, const char* from = "*") {
        Rig r;
        MusicTransitionDef t;
        t.from = from;
        t.when = when;
        t.gridBars = grid;
        EXPECT_TRUE(r.director.defineTransition(t));
        EXPECT_TRUE(r.director.play("explore"));
        r.run(pos, 0.01);
        EXPECT_TRUE(r.director.play("combat"));
        return r.backend.startTime(4) - r.backend.clock;
    };
    EXPECT_NEAR(startOf(MusicWhen::Immediate, 2.3), 0.0, 1e-9);
    EXPECT_NEAR(startOf(MusicWhen::NextBeat, 2.3), 2.5 - 2.3, 1e-6);
    EXPECT_NEAR(startOf(MusicWhen::NextBeat, 2.46), 3.0 - 2.46, 1e-6) << "a beat 0.04 s away is too near to catch: the one after";
    EXPECT_NEAR(startOf(MusicWhen::NextBar, 2.3), 4.0 - 2.3, 1e-6);
    EXPECT_NEAR(startOf(MusicWhen::NextBar, 0.3), 2.0 - 0.3, 1e-6);
    EXPECT_NEAR(startOf(MusicWhen::EndOfSegment, 0.5), 3.5, 1e-6);
    EXPECT_NEAR(startOf(MusicWhen::Grid, 0.5, 1), 2.0 - 0.5, 1e-6) << "a grid of one bar is the next bar";
    EXPECT_NEAR(startOf(MusicWhen::Grid, 2.3, 4), 4.0 - 2.3, 1e-6) << "a grid longer than the loop: its end";
    EXPECT_NEAR(startOf(MusicWhen::NextBar, 2.3, 4, "other"), 4.0 - 2.3, 1e-6) << "a rule for another segment: the default (next bar)";
}

TEST(MusicDirector, TheFirstMatchingRuleWinsAndFadesAreUsed) {
    Rig r;
    MusicTransitionDef specific;
    specific.from = "explore";
    specific.to = "combat";
    specific.when = MusicWhen::NextBeat;
    specific.fadeOut = 2.0f;
    specific.fadeIn = 0.75f;
    MusicTransitionDef any;
    any.when = MusicWhen::Immediate;
    ASSERT_TRUE(r.director.defineTransition(specific));
    ASSERT_TRUE(r.director.defineTransition(any));
    ASSERT_TRUE(r.director.play("explore"));
    r.run(1.2);
    ASSERT_TRUE(r.director.play("combat"));
    EXPECT_NEAR(r.backend.startTime(4), 1.5, 1e-9) << "the specific rule: the next beat";
    EXPECT_NEAR(r.backend.endTime(1), 1.5 + 2.0, 1e-9);
    EXPECT_NEAR(r.backend.voices[4].fadeIn, 0.75f, 1e-6f);
}

TEST(MusicDirector, ABridgeSegmentPlaysBetweenTwoOthers) {
    Rig r;
    MusicSegmentDef sting;
    sting.name = "sting";
    sting.bars = 1; // 2 s at 120 bpm
    sting.loop = false;
    MusicLayerDef l;
    l.sound = "sting";
    sting.layers = { l };
    ASSERT_TRUE(r.director.defineSegment(sting));
    r.backend.lengths["sting"] = 2.0;
    MusicTransitionDef t;
    t.from = "explore";
    t.to = "combat";
    t.via = "sting";
    ASSERT_TRUE(r.director.defineTransition(t));
    ASSERT_TRUE(r.director.play("explore"));
    r.run(2.3);
    r.director.takeEvents();
    ASSERT_TRUE(r.director.play("combat"));
    EXPECT_EQ(r.director.pending(), "combat");
    // explore: 1-3, sting: 4, combat: 5-7
    EXPECT_NEAR(r.backend.startTime(4), 4.0, 1e-9) << "the bridge at the next bar";
    EXPECT_FALSE(r.backend.voices[4].loop);
    EXPECT_NEAR(r.backend.startTime(5), 6.0, 1e-9) << "combat when the bridge's two seconds are over";
    r.run(1.8); // 4.1
    EXPECT_EQ(r.director.current(), "sting");
    EXPECT_EQ(r.director.pending(), "combat");
    r.run(2.0); // 6.1
    EXPECT_EQ(r.director.current(), "combat");
    const auto ev = r.director.takeEvents();
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Segment, "sting"));
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Segment, "combat"));
    EXPECT_FALSE(hasEvent(ev, MusicDirector::Event::Kind::Ended)) << "the bridge handing over is not an end";
}

TEST(MusicDirector, ASecondRequestReplacesTheFirstAndGoingBackCancels) {
    Rig r;
    MusicSegmentDef boss = explore();
    boss.name = "boss";
    ASSERT_TRUE(r.director.defineSegment(boss));
    ASSERT_TRUE(r.director.play("explore"));
    r.run(2.3);
    ASSERT_TRUE(r.director.play("combat"));
    ASSERT_TRUE(r.director.play("boss"));
    EXPECT_EQ(r.director.pending(), "boss");
    for (uint32_t id : { 4u, 5u, 6u }) EXPECT_TRUE(r.backend.voices[id].stopped) << "the first request's voices were dropped: " << id;
    EXPECT_FALSE(r.backend.voices[7].stopped);
    EXPECT_TRUE(r.director.play("boss")) << "asking again for what is on its way changes nothing";
    EXPECT_EQ(r.backend.voices.size(), 9u) << "no third copy: 3 explore, 3 combat, 3 boss";
    // going back to what is playing cancels what was waiting
    ASSERT_TRUE(r.director.play("explore"));
    EXPECT_EQ(r.director.pending(), "");
    for (uint32_t id : { 7u, 8u, 9u }) EXPECT_TRUE(r.backend.voices[id].stopped);
    r.run(3.0);
    EXPECT_EQ(r.director.current(), "explore");
    EXPECT_TRUE(r.backend.playing(1)) << "and the explore voices were never faded";
    EXPECT_TRUE(r.director.play("explore")) << "the same segment again: nothing";
}

TEST(MusicDirector, ASegmentThatEndsGoesToItsNext) {
    Rig r;
    MusicSegmentDef intro = explore();
    intro.name = "intro";
    intro.loop = false;
    intro.bars = 1; // 2 s
    intro.next = "explore";
    ASSERT_TRUE(r.director.defineSegment(intro));
    r.backend.lengths["pad"] = 2.0;
    r.backend.lengths["drums"] = 2.0;
    r.backend.lengths["strings"] = 2.0;
    ASSERT_TRUE(r.director.play("intro"));
    r.run(1.5);
    EXPECT_EQ(r.director.current(), "intro");
    r.director.takeEvents();
    r.run(0.7);
    EXPECT_EQ(r.director.current(), "explore");
    const auto ev = r.director.takeEvents();
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Ended, "intro"));
    EXPECT_TRUE(hasEvent(ev, MusicDirector::Event::Kind::Segment, "explore"));
}

TEST(MusicDirector, AStingerLandsOnTheNextBeatOverTheMusic) {
    Rig r;
    MusicStingerDef hit;
    hit.name = "hit";
    hit.sound = "hit_sound";
    hit.gain = 0.7f;
    hit.when = MusicWhen::NextBeat;
    ASSERT_TRUE(r.director.defineStinger(hit));
    MusicStingerDef now;
    now.name = "now";
    now.sound = "now_sound";
    now.when = MusicWhen::Immediate;
    ASSERT_TRUE(r.director.defineStinger(now));
    ASSERT_TRUE(r.director.play("explore"));
    r.run(2.3);
    r.director.takeEvents();
    ASSERT_TRUE(r.director.stinger("hit"));
    ASSERT_EQ(r.backend.voices.size(), 4u);
    EXPECT_FALSE(r.backend.voices[4].loop) << "a one-shot";
    EXPECT_NEAR(r.backend.voices[4].gain, 0.7f, 1e-6f);
    EXPECT_NEAR(r.backend.startTime(4), 2.5, 1e-9);
    EXPECT_EQ(r.backend.voices[4].ref, 1u);
    EXPECT_TRUE(hasEvent(r.director.takeEvents(), MusicDirector::Event::Kind::Stinger, "hit"));
    ASSERT_TRUE(r.director.stinger("now"));
    EXPECT_NEAR(r.backend.startTime(5), r.backend.clock, 1e-9);
    EXPECT_EQ(r.director.current(), "explore") << "the music goes on";
    std::string error;
    EXPECT_FALSE(r.director.stinger("nothing", &error));
    EXPECT_NE(error.find("no stinger called 'nothing'"), std::string::npos);
    // with no music playing, a stinger is just played
    r.director.stop(0.0f);
    ASSERT_TRUE(r.director.stinger("hit"));
    EXPECT_NEAR(r.backend.startTime(r.backend.next - 1), r.backend.clock, 1e-9);
}

TEST(MusicDirector, StopFadesTheMusicOut) {
    Rig r;
    ASSERT_TRUE(r.director.play("explore"));
    r.run(1.0);
    r.director.takeEvents();
    r.director.stop(2.0f);
    EXPECT_FALSE(r.director.playing());
    EXPECT_EQ(r.director.current(), "");
    EXPECT_TRUE(hasEvent(r.director.takeEvents(), MusicDirector::Event::Kind::Ended, "explore"));
    for (uint32_t id : { 1u, 2u, 3u }) EXPECT_TRUE(r.backend.voices[id].stopped);
    EXPECT_NE(r.backend.log.back().find(":2.0"), std::string::npos) << r.backend.log.back();
    r.run(1.0); // nothing playing: nothing happens
    EXPECT_TRUE(r.director.takeEvents().empty());
    ASSERT_TRUE(r.director.play("combat")) << "and music can start again";
    EXPECT_EQ(r.director.current(), "combat");
}

TEST(MusicDirector, PlayNowCutsInWhateverTheRules) {
    Rig r;
    MusicTransitionDef slow;
    slow.when = MusicWhen::EndOfSegment;
    ASSERT_TRUE(r.director.defineTransition(slow));
    ASSERT_TRUE(r.director.play("explore"));
    r.run(1.0);
    ASSERT_TRUE(r.director.playNow("combat", 0.5f));
    EXPECT_EQ(r.director.current(), "combat");
    EXPECT_NEAR(r.backend.startTime(4), r.backend.clock, 1e-9);
    EXPECT_NEAR(r.backend.voices[4].fadeIn, 0.5f, 1e-6f);
    EXPECT_LT(r.backend.endTime(1), r.backend.clock + 0.5) << "the old music goes at once (a short fade)";
}

TEST(MusicDirector, MistakesAreReportedAndLeaveNothingHalfStarted) {
    Rig r;
    std::string error;
    EXPECT_FALSE(r.director.play("nothing", &error));
    EXPECT_NE(error.find("no music segment called 'nothing'"), std::string::npos);
    MusicDirector none;
    EXPECT_FALSE(none.play("explore", &error));
    EXPECT_NE(error.find("no music backend"), std::string::npos);
    // a layer whose sound cannot be played: the others that started are stopped, nothing plays
    r.backend.broken.insert("drums");
    EXPECT_FALSE(r.director.play("explore", &error));
    EXPECT_NE(error.find("could not be played"), std::string::npos) << error;
    EXPECT_FALSE(r.director.playing());
    for (const auto& [id, v] : r.backend.voices) EXPECT_TRUE(v.stopped) << id;
    // a transition whose bridge is not a segment
    r.backend.broken.clear();
    MusicTransitionDef t;
    t.from = "explore";
    t.to = "combat";
    t.via = "ghost";
    ASSERT_TRUE(r.director.defineTransition(t));
    ASSERT_TRUE(r.director.play("explore"));
    EXPECT_FALSE(r.director.play("combat", &error));
    EXPECT_NE(error.find("via 'ghost'"), std::string::npos) << error;
    EXPECT_EQ(r.director.current(), "explore") << "still playing what it was";
    // a bad definition
    MusicSegmentDef bad;
    EXPECT_FALSE(r.director.defineSegment(bad, &error));
    EXPECT_NE(error.find("needs a name"), std::string::npos);
    bad.name = "x";
    EXPECT_FALSE(r.director.defineSegment(bad, &error));
    EXPECT_NE(error.find("no layers"), std::string::npos);
    bad.layers = { MusicLayerDef{} };
    EXPECT_FALSE(r.director.defineSegment(bad, &error));
    EXPECT_NE(error.find("has no sound"), std::string::npos);
    bad.layers[0].sound = "a";
    bad.bpm = 1000.0f;
    EXPECT_FALSE(r.director.defineSegment(bad, &error));
    EXPECT_NE(error.find("bpm is 20 to 400"), std::string::npos);
    bad.bpm = 100.0f;
    bad.beatsPerBar = 0;
    EXPECT_FALSE(r.director.defineSegment(bad, &error));
    bad.beatsPerBar = 4;
    EXPECT_TRUE(r.director.defineSegment(bad));
    MusicStingerDef s;
    EXPECT_FALSE(r.director.defineStinger(s, &error));
    s.name = "n";
    s.sound = "s";
    s.when = MusicWhen::Grid;
    EXPECT_FALSE(r.director.defineStinger(s, &error));
    EXPECT_NE(error.find("when is immediate, next_beat or next_bar"), std::string::npos);
    MusicTransitionDef badT;
    badT.gridBars = 0;
    EXPECT_FALSE(r.director.defineTransition(badT, &error));
}

TEST(MusicDirector, LoadsFromJsonAndSaysWhereItIsWrong) {
    MusicDirector d;
    const json good = json::parse(R"({
        "segments": [
            { "name": "explore", "bpm": 90, "beatsPerBar": 3, "bars": 4, "gain": 0.8, "layers": [
                { "sound": "pad.ogg" },
                { "sound": "drums.ogg", "from": 0.5 },
                { "sound": "lead.ogg", "from": 0.3, "full": 0.7, "fadeFrom": 0.9, "to": 1.0, "fade": 2, "gain": 0.5 } ] },
            { "name": "sting", "loop": false, "next": "explore", "layers": [ { "sound": "sting.ogg" } ] } ],
        "transitions": [ { "from": "explore", "to": "*", "when": "next_beat", "fadeOut": 1.5, "via": "sting" } ],
        "stingers": [ { "name": "hit", "sound": "hit.ogg", "gain": 0.9, "when": "next_bar" } ] })");
    std::string error;
    ASSERT_TRUE(d.fromJson(good, &error)) << error;
    ASSERT_TRUE(d.hasSegment("explore"));
    const MusicSegmentDef* s = d.segment("explore");
    EXPECT_FLOAT_EQ(s->bpm, 90.0f);
    EXPECT_EQ(s->beatsPerBar, 3);
    EXPECT_EQ(s->bars, 4);
    ASSERT_EQ(s->layers.size(), 3u);
    EXPECT_FLOAT_EQ(s->layers[1].from, 0.5f);
    EXPECT_FLOAT_EQ(s->layers[1].full, 0.5f) << "a from alone is a switch";
    EXPECT_FLOAT_EQ(s->layers[2].full, 0.7f);
    EXPECT_FLOAT_EQ(s->layers[2].to, 1.0f);
    EXPECT_FALSE(d.segment("sting")->loop);
    EXPECT_EQ(d.segment("sting")->next, "explore");
    auto bad = [&](const char* text, const std::string& expect) {
        MusicDirector x;
        EXPECT_FALSE(x.fromJson(json::parse(text), &error)) << text;
        EXPECT_NE(error.find(expect), std::string::npos) << "'" << error << "' should say '" << expect << "'";
    };
    bad("[]", "an object");
    bad(R"({ "segments": 5 })", "segments is a list");
    bad(R"({ "segments": [ 5 ] })", "segments[0] is an object");
    bad(R"({ "segments": [ { "name": "x", "bpm": "fast", "layers": [] } ] })", "segments[0]: bpm must be a number");
    bad(R"({ "segments": [ { "name": "x", "layers": [] } ] })", "segments[0]: segment 'x' has no layers");
    bad(R"({ "segments": [ { "name": "x", "layers": [ { "from": 1 } ] } ] })", "layer 1 has no sound");
    bad(R"({ "segments": [ { "name": "x", "layers": [ { "sound": 5 } ] } ] })", "sound must be text");
    bad(R"({ "transitions": [ { "when": "someday" } ] })", "transitions[0]: when is immediate");
    bad(R"({ "transitions": [ { "gridBars": 0 } ] })", "gridBars is 1 to 256");
    bad(R"({ "stingers": [ { "name": "a" } ] })", "stingers[0]");
    // empty Lua tables arrive as {}
    EXPECT_TRUE(MusicDirector().fromJson(json::parse(R"({ "segments": {}, "transitions": {}, "stingers": {} })"), &error)) << error;
}

TEST(MusicDirector, ClearStopsAndForgets) {
    Rig r;
    ASSERT_TRUE(r.director.play("explore"));
    r.director.setIntensity(0.7f);
    r.director.clear();
    EXPECT_FALSE(r.director.playing());
    EXPECT_FALSE(r.director.hasSegment("explore"));
    EXPECT_EQ(r.director.parameter("intensity"), 0.0f);
    EXPECT_TRUE(r.backend.voices[1].stopped);
}

// ---- end to end: the director on the real mixer ---------------------------------------

TEST(MusicDirector, ATransitionOnTheRealMixerLandsOnTheBarLineToTheSample) {
    AudioMixer mixer(kRate, 32);
    MixerMusicBackend backend(mixer, [](const std::string& name) -> SoundHandle {
        if (name == "calm") return makeSound(0.0f, 4.0);  // silent: the transition is what we hear
        if (name == "loud") return makeSound(0.5f, 4.0);
        return nullptr;
    });
    MusicDirector director(&backend);
    MusicSegmentDef calm;
    calm.name = "calm";
    calm.bpm = 120.0f;
    calm.bars = 2;
    MusicLayerDef a;
    a.sound = "calm";
    calm.layers = { a };
    MusicSegmentDef loud = calm;
    loud.name = "loud";
    loud.layers[0].sound = "loud";
    ASSERT_TRUE(director.defineSegment(calm));
    ASSERT_TRUE(director.defineSegment(loud));
    ASSERT_TRUE(director.play("calm"));
    // a game loop: a frame of 7.3 ms at a time, the mixer called in blocks as the audio thread would
    std::vector<float> all;
    std::vector<float> block(2 * 441);
    long frame = 0;
    bool asked = false;
    while (frame < 4.4 * kRate) {
        if (!asked && frame > 2.3 * kRate) {
            ASSERT_TRUE(director.play("loud")); // 2.3 s in: the next bar line is at 4.0 s
            asked = true;
        }
        mixer.mix(block.data(), 441);
        for (int i = 0; i < 441; ++i) all.push_back(block[static_cast<size_t>(2 * i)]);
        frame += 441;
        director.update(441.0f / kRate);
    }
    EXPECT_NEAR(static_cast<double>(onset(all)), 4.0 * kRate, 2.0) << "the new segment starts on the bar line, to the sample";
    EXPECT_EQ(director.current(), "loud");
}
