// music.* in Lua (kke/ScriptMusic.h, ENG-F39).
#include "kke/ScriptMusic.h"
#include "kke/ScriptVM.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <filesystem>
#include <fstream>

using namespace kke;

namespace {

constexpr int kRate = 48000;

double globalNumber(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const double v = lua_tonumber(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
std::string globalString(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    std::string v = lua_isstring(vm.state(), -1) ? lua_tostring(vm.state(), -1) : "";
    lua_pop(vm.state(), 2);
    return v;
}
bool globalBool(ScriptVM& vm, const char* name, const std::string& source = "console") {
    vm.pushEnv(source);
    lua_getfield(vm.state(), -1, name);
    const bool v = lua_toboolean(vm.state(), -1);
    lua_pop(vm.state(), 2);
    return v;
}
std::string lastError(const ScriptVM& vm) { return vm.errors().empty() ? std::string() : vm.errors().back().message; }

SoundHandle dc(float v, double seconds) {
    auto b = std::make_shared<SoundBuffer>();
    b->sampleRate = kRate;
    b->samples.assign(static_cast<size_t>(seconds * kRate), v);
    return b;
}

struct Fixture {
    ScriptVM vm;
    AudioMixer mixer{ kRate, 32 };
    ScriptMusic music;
    explicit Fixture(const std::string& folder = {}) {
        music.attach(
            &mixer,
            [](const std::string& name) -> SoundHandle {
                if (name == "missing.wav") return nullptr;
                if (name == "hit.wav") return dc(0.2f, 0.5);
                return dc(0.0f, 4.0);
            },
            folder);
        music.bind(vm);
        vm.onUnload = [this](const std::string& source) { music.release(source); };
    }
    bool run(const std::string& code, const std::string& source = "console") { return vm.runString(code, source); }
    // One audio block and one game frame, as the engine does them.
    void step(int frames = 441) {
        std::vector<float> out(static_cast<size_t>(frames) * 2);
        mixer.mix(out.data(), frames);
        music.update(static_cast<float>(frames) / static_cast<float>(kRate), vm);
    }
    void seconds(double s) {
        for (int done = 0; done < static_cast<int>(s * kRate); done += 441) step();
    }
};

const char* kScore = R"(
    ok = music.define{
        segments = {
            { name = "calm", bpm = 120, bars = 2, layers = { { sound = "calm.wav" }, { sound = "drums.wav", from = 0.5 } } },
            { name = "fight", bpm = 120, bars = 2, layers = { { sound = "fight.wav" } } },
        },
        transitions = { { to = "fight", when = "next_bar", fadeOut = 0.1 } },
        stingers = { { name = "hit", sound = "hit.wav", when = "next_beat" } },
    }
)";

} // namespace

TEST(ScriptMusic, DefinesPlaysAndReportsBeatsBarsAndSegments) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kScore) + R"(
        beats, bars, segments = {}, {}, {}
        hook.Add("MusicBeat", "t", function(e) beats[#beats + 1] = e.beat end)
        hook.Add("MusicBar", "t", function(e) bars[#bars + 1] = e.bar end)
        hook.Add("MusicSegment", "t", function(e) segments[#segments + 1] = e.segment end)
        started, why = music.play("calm")
        cur = music.current()
        pl = music.playing()
    )")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok"));
    EXPECT_TRUE(globalBool(f.vm, "started"));
    EXPECT_EQ(globalString(f.vm, "cur"), "calm");
    EXPECT_TRUE(globalBool(f.vm, "pl"));
    f.seconds(2.6); // 120 bpm, 4/4: beats at 0, 0.5, ... and a bar line every 2 s
    ASSERT_TRUE(f.run("nb, nbar, ns = #beats, #bars, #segments first = beats[1] second = beats[2] third = segments[1] pos = music.position() beat = music.beat()")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "nb"), 6.0) << "beats at 0, 0.5, 1.0, 1.5, 2.0, 2.5";
    EXPECT_EQ(globalNumber(f.vm, "nbar"), 2.0) << "the bars at 0 and 2 s";
    EXPECT_EQ(globalNumber(f.vm, "ns"), 1.0);
    EXPECT_EQ(globalNumber(f.vm, "first"), 0.0);
    EXPECT_EQ(globalNumber(f.vm, "second"), 1.0);
    EXPECT_EQ(globalString(f.vm, "third"), "calm");
    EXPECT_NEAR(globalNumber(f.vm, "pos"), 2.6, 0.02);
    EXPECT_NEAR(globalNumber(f.vm, "beat"), 5.2, 0.05);
}

TEST(ScriptMusic, ATransitionHappensOnTheBarLine) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kScore) + R"(
        segments, ended = {}, {}
        hook.Add("MusicSegment", "t", function(e) segments[#segments + 1] = e.segment end)
        music.play("calm")
    )")) << lastError(f.vm);
    f.seconds(2.3);
    ASSERT_TRUE(f.run("ok2 = music.play('fight') waiting = music.pending() still = music.current()")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok2"));
    EXPECT_EQ(globalString(f.vm, "waiting"), "fight");
    EXPECT_EQ(globalString(f.vm, "still"), "calm");
    f.seconds(1.4); // 3.7: before the bar line at 4.0
    ASSERT_TRUE(f.run("a = music.current()"));
    EXPECT_EQ(globalString(f.vm, "a"), "calm");
    f.seconds(0.6); // 4.3
    ASSERT_TRUE(f.run("b = music.current() c = music.pending() list = table.concat(segments, ',')")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "b"), "fight");
    EXPECT_EQ(globalString(f.vm, "c"), "");
    EXPECT_EQ(globalString(f.vm, "list"), "calm,fight");
}

TEST(ScriptMusic, ParametersMoveTheLayers) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kScore) + R"(
        music.play("calm")
        i0 = music.intensity()
        music.intensity(0.7)
        i1 = music.intensity()
        music.param("danger", 3)
        d = music.param("danger")
        unset = music.param("nothing")
    )")) << lastError(f.vm);
    f.seconds(0.2);
    EXPECT_EQ(globalNumber(f.vm, "i0"), 0.0);
    EXPECT_NEAR(globalNumber(f.vm, "i1"), 0.7, 1e-6);
    EXPECT_EQ(globalNumber(f.vm, "d"), 3.0);
    EXPECT_EQ(globalNumber(f.vm, "unset"), 0.0);
    EXPECT_NEAR(f.music.director().layerGain(1), 1.0f, 1e-6f) << "the drums switch on at 0.5";
    EXPECT_FALSE(f.run("music.intensity(0 / 0)"));
    EXPECT_NE(lastError(f.vm).find("a number"), std::string::npos) << lastError(f.vm);
}

TEST(ScriptMusic, StingersLandOverTheMusic) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kScore) + R"(
        named = {}
        hook.Add("MusicStinger", "t", function(e) named[#named + 1] = e.name end)
        music.play("calm")
    )")) << lastError(f.vm);
    f.seconds(1.1);
    const size_t voicesBefore = f.mixer.voiceCount();
    ASSERT_TRUE(f.run("sting = music.stinger('hit') cur = music.current()")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "sting"));
    EXPECT_EQ(f.mixer.voiceCount(), voicesBefore + 1) << "one more voice, waiting for the beat";
    f.seconds(0.1);
    ASSERT_TRUE(f.run("n = #named first = named[1] cur2 = music.current()")) << lastError(f.vm);
    EXPECT_EQ(globalNumber(f.vm, "n"), 1.0);
    EXPECT_EQ(globalString(f.vm, "first"), "hit");
    EXPECT_EQ(globalString(f.vm, "cur2"), "calm");
    ASSERT_TRUE(f.run("bad, why = music.stinger('nothing')")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "bad"));
    EXPECT_NE(globalString(f.vm, "why").find("no stinger called 'nothing'"), std::string::npos);
}

TEST(ScriptMusic, StopAndPlayNow) {
    Fixture f;
    ASSERT_TRUE(f.run(std::string(kScore) + R"(
        ended = {}
        hook.Add("MusicEnded", "t", function(e) ended[#ended + 1] = e.segment end)
        music.play("calm")
    )")) << lastError(f.vm);
    f.seconds(0.5);
    ASSERT_TRUE(f.run("music.playNow('fight', 0.2) a = music.current()")) << lastError(f.vm);
    EXPECT_EQ(globalString(f.vm, "a"), "fight") << "at once, whatever the rules";
    f.seconds(0.2);
    ASSERT_TRUE(f.run("music.stop(0.5) p = music.playing() list = table.concat(ended, ',')")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "p"));
    f.seconds(0.1);
    ASSERT_TRUE(f.run("list2 = table.concat(ended, ',')"));
    EXPECT_EQ(globalString(f.vm, "list2"), "fight");
    f.seconds(1.0);
    EXPECT_EQ(f.mixer.voiceCount(), 0u) << "faded out and gone";
}

TEST(ScriptMusic, MistakesComeBackWithAReason) {
    Fixture f;
    ASSERT_TRUE(f.run(R"(
        bad1, why1 = music.define{ segments = { { name = "x", bpm = 1000, layers = { { sound = "a.wav" } } } } }
        bad2, why2 = music.define{ segments = { { name = "y", layers = {} } } }
        bad3, why3 = music.define{ transitions = { { when = "someday" } } }
        bad4, why4 = music.play("nowhere")
        music.define{ segments = { { name = "broken", layers = { { sound = "missing.wav" } } } } }
        bad5, why5 = music.play("broken")
        pl = music.playing()
    )")) << lastError(f.vm);
    EXPECT_FALSE(globalBool(f.vm, "bad1"));
    EXPECT_NE(globalString(f.vm, "why1").find("segments[0]: segment 'x': bpm is 20 to 400"), std::string::npos) << globalString(f.vm, "why1");
    EXPECT_NE(globalString(f.vm, "why2").find("has no layers"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "why3").find("transitions[0]: when is"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "why4").find("no music segment called 'nowhere'"), std::string::npos);
    EXPECT_FALSE(globalBool(f.vm, "bad5"));
    EXPECT_NE(globalString(f.vm, "why5").find("could not be played"), std::string::npos);
    EXPECT_FALSE(globalBool(f.vm, "pl"));
    EXPECT_FALSE(f.run("music.define(5)"));
}

TEST(ScriptMusic, LoadsAScoreFileInsideTheGamesFolderOnly) {
    const std::string dir = ::testing::TempDir() + "kke_music_load";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir + "/music");
    std::ofstream(dir + "/music/score.json") << R"({ "segments": [ { "name": "calm", "bars": 2, "layers": [ { "sound": "calm.wav" } ] } ] })";
    std::ofstream(dir + "/music/bad.json") << R"({ "segments": [ { "name": "x", "layers": [] } ] })";
    std::ofstream(dir + "/../kke_outside_music.json") << "{}";
    Fixture f(dir);
    ASSERT_TRUE(f.run(R"(
        ok = music.load("music/score.json")
        played = music.play("calm")
        bad, whyBad = music.load("music/bad.json")
        outside, whyOutside = music.load("../kke_outside_music.json")
        missing, whyMissing = music.load("music/nope.json")
    )")) << lastError(f.vm);
    EXPECT_TRUE(globalBool(f.vm, "ok"));
    EXPECT_TRUE(globalBool(f.vm, "played"));
    EXPECT_FALSE(globalBool(f.vm, "bad"));
    EXPECT_NE(globalString(f.vm, "whyBad").find("has no layers"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "whyOutside").find("inside the game's folder"), std::string::npos);
    EXPECT_NE(globalString(f.vm, "whyMissing").find("can't open"), std::string::npos);
    std::filesystem::remove_all(dir);
    std::filesystem::remove(::testing::TempDir() + "kke_outside_music.json");
}

TEST(ScriptMusic, TheMusicBelongsToTheScriptThatStartedIt) {
    Fixture f;
    ASSERT_TRUE(f.run(kScore, "level"));
    ASSERT_TRUE(f.run("music.play('calm')", "level"));
    f.seconds(0.2);
    EXPECT_TRUE(f.music.director().playing());
    f.vm.unload("level");
    EXPECT_FALSE(f.music.director().playing()) << "unloading the script stops its music";
    EXPECT_FALSE(f.music.director().hasSegment("calm")) << "and takes its score away";
    // another script's unloading does not touch it
    ASSERT_TRUE(f.run(kScore, "level2"));
    ASSERT_TRUE(f.run("music.play('calm')", "level2"));
    f.vm.unload("some other script");
    EXPECT_TRUE(f.music.director().playing());
}
