// Timelines (kke/Timeline.h, ENG-F27, docs/TIMELINE.md).
#include "kke/Timeline.h"

#include <glm/gtc/quaternion.hpp>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

using namespace kke;
using nlohmann::json;

namespace {

// Writes down everything a timeline tells it.
struct Log : TimelineSink {
    std::vector<std::string> lines;
    std::map<std::string, glm::vec4> last;
    int finishedCount = 0, loopCount = 0;

    static std::string num(float v) {
        std::ostringstream s;
        s.precision(4);
        s << v;
        return s.str();
    }
    void value(const std::string& target, const std::string& property, ValueKind, const glm::vec4& v) override {
        last[target + "." + property] = v;
        lines.push_back("value:" + target + "." + property + "=" + num(v.x));
    }
    void event(const TimelineEvent& e) override { lines.push_back("event:" + e.name + (e.data.empty() ? "" : "(" + e.data + ")")); }
    void marker(const TimelineMarker& m) override { lines.push_back("marker:" + m.name); }
    void active(const std::string& target, bool on) override { lines.push_back(std::string("active:") + target + (on ? ":on" : ":off")); }
    void clip(const ClipEvent& e) override {
        const char* k = e.kind == ClipEvent::Kind::Start ? "start" : e.kind == ClipEvent::Kind::Stop ? "stop" : "seek";
        lines.push_back(std::string("clip:") + k + ":" + e.target + ":" + e.clip + "@" + num(e.localTime));
    }
    void finished() override {
        ++finishedCount;
        lines.push_back("finished");
    }
    void looped(int n) override {
        loopCount = n;
        lines.push_back("looped:" + std::to_string(n));
    }
    size_t count(const std::string& prefix) const {
        return static_cast<size_t>(std::count_if(lines.begin(), lines.end(), [&](const std::string& l) { return l.rfind(prefix, 0) == 0; }));
    }
    bool has(const std::string& line) const { return std::find(lines.begin(), lines.end(), line) != lines.end(); }
    std::vector<std::string> only(const std::string& prefix) const {
        std::vector<std::string> out;
        for (const std::string& l : lines)
            if (l.rfind(prefix, 0) == 0) out.push_back(l);
        return out;
    }
    void clear() { lines.clear(); }
};

Timeline ramp() { // door.height 0 -> 10 over 2 s
    Timeline t;
    const size_t k = t.addTrack("door", "height");
    t.addScalarKey(k, 0.0f, 0.0f);
    t.addScalarKey(k, 2.0f, 10.0f);
    return t;
}

} // namespace

TEST(Ease, CurvesGoFromZeroToOne) {
    for (Ease e : { Ease::Linear, Ease::SmoothStep, Ease::EaseIn, Ease::EaseOut, Ease::EaseInOut }) {
        EXPECT_FLOAT_EQ(applyEase(e, 0.0f), 0.0f) << easeName(e);
        EXPECT_FLOAT_EQ(applyEase(e, 1.0f), 1.0f) << easeName(e);
    }
    EXPECT_FLOAT_EQ(applyEase(Ease::Linear, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(applyEase(Ease::SmoothStep, 0.5f), 0.5f);
    EXPECT_FLOAT_EQ(applyEase(Ease::SmoothStep, 0.25f), 0.15625f);
    EXPECT_FLOAT_EQ(applyEase(Ease::EaseIn, 0.5f), 0.25f);
    EXPECT_FLOAT_EQ(applyEase(Ease::EaseOut, 0.5f), 0.75f);
    EXPECT_FLOAT_EQ(applyEase(Ease::EaseInOut, 0.5f), 0.5f);
    EXPECT_LT(applyEase(Ease::EaseInOut, 0.25f), 0.25f);
    EXPECT_FLOAT_EQ(applyEase(Ease::Step, 0.99f), 0.0f) << "a step holds until the next key";
    EXPECT_FLOAT_EQ(applyEase(Ease::Linear, -1.0f), 0.0f);
    EXPECT_FLOAT_EQ(applyEase(Ease::Linear, 2.0f), 1.0f);
    Ease e;
    EXPECT_TRUE(easeFromName("inout", e));
    EXPECT_EQ(e, Ease::EaseInOut);
    EXPECT_FALSE(easeFromName("bouncy", e));
    WrapMode w;
    EXPECT_TRUE(wrapModeFromName("ping-pong", w));
    EXPECT_EQ(w, WrapMode::PingPong);
    ValueKind k;
    EXPECT_TRUE(valueKindFromName("number", k));
    EXPECT_EQ(k, ValueKind::Scalar);
}

TEST(Timeline, TracksInterpolateBetweenKeys) {
    Timeline t = ramp();
    Log log;
    t.evaluate(1.0f, log);
    EXPECT_FLOAT_EQ(log.last["door.height"].x, 5.0f);
    log.clear();
    t.evaluate(-3.0f, log);
    EXPECT_FLOAT_EQ(log.last["door.height"].x, 0.0f) << "before the start";
    t.evaluate(99.0f, log);
    EXPECT_FLOAT_EQ(log.last["door.height"].x, 10.0f) << "after the end";

    const size_t p = t.addTrack("lamp", "position", ValueKind::Vec3);
    t.addVec3Key(p, 0.0f, glm::vec3(0, 0, 0));
    t.addVec3Key(p, 2.0f, glm::vec3(4, 2, -2));
    const size_t c = t.addTrack("lamp", "color", ValueKind::Color);
    t.addKey(c, 0.0f, glm::vec4(1, 0, 0, 1));
    t.addKey(c, 2.0f, glm::vec4(0, 0, 1, 0));
    t.evaluate(0.5f, log);
    EXPECT_EQ(log.last["lamp.position"], glm::vec4(1, 0.5f, -0.5f, 0));
    EXPECT_EQ(log.last["lamp.color"], glm::vec4(0.75f, 0, 0.25f, 0.75f));
}

TEST(Timeline, RotationsSlerpTheShortWay) {
    Timeline t;
    const size_t r = t.addTrack("hero", "rotation", ValueKind::Quat);
    t.addQuatKey(r, 0.0f, glm::angleAxis(0.0f, glm::vec3(0, 1, 0)));
    t.addQuatKey(r, 2.0f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)));
    Log log;
    t.evaluate(1.0f, log);
    const glm::vec4 v = log.last["hero.rotation"];
    const glm::quat q(v.w, v.x, v.y, v.z);
    EXPECT_NEAR(glm::degrees(glm::angle(q)), 45.0f, 0.01f);
    EXPECT_NEAR(glm::length(q), 1.0f, 1e-5f);
    // q and -q are one rotation: the way round is still the short one
    Timeline flip;
    const size_t f = flip.addTrack("hero", "rotation", ValueKind::Quat);
    flip.addQuatKey(f, 0.0f, glm::angleAxis(glm::radians(10.0f), glm::vec3(0, 1, 0)));
    const glm::quat negative = -glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0));
    flip.addKey(f, 2.0f, glm::vec4(negative.x, negative.y, negative.z, negative.w));
    flip.evaluate(1.0f, log);
    const glm::vec4 w = log.last["hero.rotation"];
    EXPECT_NEAR(glm::degrees(glm::angle(glm::quat(w.w, w.x, w.y, w.z))), 20.0f, 0.1f);
}

TEST(Timeline, EasesAndStepsApplyToTheSegmentThatStartsAtAKey) {
    Timeline t;
    const size_t k = t.addTrack("a", "x");
    t.addScalarKey(k, 0.0f, 0.0f, Ease::Step);
    t.addScalarKey(k, 1.0f, 10.0f, Ease::SmoothStep);
    t.addScalarKey(k, 2.0f, 20.0f);
    Log log;
    t.evaluate(0.75f, log);
    EXPECT_FLOAT_EQ(log.last["a.x"].x, 0.0f) << "a step holds";
    t.evaluate(1.25f, log);
    EXPECT_NEAR(log.last["a.x"].x, 10.0f + 10.0f * 0.15625f, 1e-4f);
    t.evaluate(1.0f, log);
    EXPECT_FLOAT_EQ(log.last["a.x"].x, 10.0f);
}

TEST(Timeline, KeysAreKeptInOrderAndReplaceTheirTime) {
    Timeline t;
    const size_t k = t.addTrack("a", "x");
    t.addScalarKey(k, 2.0f, 20.0f);
    t.addScalarKey(k, 0.0f, 0.0f);
    t.addScalarKey(k, 1.0f, 10.0f);
    t.addScalarKey(k, 1.0f, 15.0f);
    t.addScalarKey(k, std::nanf(""), 1.0f);
    t.addScalarKey(99, 1.0f, 1.0f); // no such track
    ASSERT_EQ(t.tracks()[k].keys.size(), 3u);
    EXPECT_FLOAT_EQ(t.tracks()[k].keys[1].value.x, 15.0f);
    EXPECT_LT(t.tracks()[k].keys[0].time, t.tracks()[k].keys[1].time);
    EXPECT_LT(t.tracks()[k].keys[1].time, t.tracks()[k].keys[2].time);
}

TEST(Timeline, DurationIsWhereTheLastThingEnds) {
    Timeline t;
    EXPECT_FLOAT_EQ(t.duration(), 0.0f);
    t.addEvent(3.0f, "e");
    EXPECT_FLOAT_EQ(t.duration(), 3.0f);
    t.addSpan("x", 1.0f, 5.0f);
    EXPECT_FLOAT_EQ(t.duration(), 5.0f);
    TimelineClip c;
    c.target = "h";
    c.clip = "Walk";
    c.start = 2.0f;
    c.end = 7.0f;
    t.addClip(c);
    EXPECT_FLOAT_EQ(t.duration(), 7.0f);
    auto sub = std::make_shared<Timeline>(ramp()); // 2 s, played at double speed from 6 s: ends at 7
    ASSERT_TRUE(t.addSub(6.0f, sub, 2.0f));
    EXPECT_FLOAT_EQ(t.duration(), 7.0f);
    ASSERT_TRUE(t.addSub(8.0f, sub, 1.0f));
    EXPECT_FLOAT_EQ(t.duration(), 10.0f);
    t.setDuration(20.0f);
    EXPECT_FLOAT_EQ(t.duration(), 20.0f) << "a length set wins";
}

TEST(Timeline, APlayThroughFiresEveryEventOnceInOrder) {
    Timeline t = ramp();
    t.addEvent(0.0f, "start");
    t.addEvent(1.0f, "middle", "payload");
    t.addEvent(0.5f, "early");
    t.addEvent(2.0f, "end");
    Log log;
    t.play();
    for (int i = 0; i < 12; ++i) t.update(0.25f, log);
    EXPECT_EQ(log.only("event:"), (std::vector<std::string>{ "event:start", "event:early", "event:middle(payload)", "event:end" }));
    EXPECT_EQ(log.finishedCount, 1);
    EXPECT_TRUE(t.finished());
    EXPECT_FALSE(t.playing());
    EXPECT_FLOAT_EQ(t.time(), 2.0f);
    EXPECT_FLOAT_EQ(log.last["door.height"].x, 10.0f) << "the last values are applied";
}

TEST(Timeline, ValuesAreOnlySentWhenTheyChange) {
    Timeline t = ramp();
    const size_t still = t.addTrack("lamp", "intensity");
    t.addScalarKey(still, 0.0f, 3.0f);
    t.addScalarKey(still, 2.0f, 3.0f);
    Log log;
    t.play();
    t.update(0.5f, log);
    t.update(0.5f, log);
    t.update(0.5f, log);
    EXPECT_EQ(log.count("value:door.height"), 3u);
    EXPECT_EQ(log.count("value:lamp.intensity"), 1u) << "a value that does not move is sent once, at the start";
}

TEST(Timeline, EventsAtExactlyTheBoundaryFireOnceNotTwice) {
    Timeline t = ramp();
    t.addEvent(0.5f, "half");
    Log log;
    t.play();
    t.update(0.25f, log);
    t.update(0.25f, log); // lands on 0.5
    t.update(0.25f, log);
    EXPECT_EQ(log.count("event:half"), 1u);
}

TEST(Timeline, LoopingFiresEventsEveryLapAndCountsLoops) {
    Timeline t;
    t.addEvent(0.0f, "top");
    t.addEvent(1.0f, "bottom");
    t.setDuration(2.0f);
    t.wrap = WrapMode::Loop;
    Log log;
    t.play();
    for (int i = 0; i < 16; ++i) t.update(0.5f, log); // 8 s: four laps
    EXPECT_EQ(log.count("event:top"), 4u);
    EXPECT_EQ(log.count("event:bottom"), 4u);
    EXPECT_EQ(t.loops(), 3) << "three wraps in four laps (the fourth ends exactly on the end)";
    EXPECT_EQ(log.loopCount, t.loops());
    EXPECT_TRUE(t.playing());
    EXPECT_EQ(log.finishedCount, 0);
    // one huge step crosses several laps and still fires them all
    Timeline big;
    big.addEvent(0.5f, "tick");
    big.setDuration(1.0f);
    big.wrap = WrapMode::Loop;
    Log bl;
    big.play();
    big.update(3.25f, bl);
    EXPECT_EQ(bl.count("event:tick"), 3u) << "0.5, 1.5 and 2.5 of 3.25 s; 3.5 is not reached";
}

TEST(Timeline, PingPongTurnsAroundAndOnlyForwardLegsFireEvents) {
    Timeline t = ramp();
    t.addEvent(1.0f, "mid");
    t.wrap = WrapMode::PingPong;
    Log log;
    t.play();
    t.update(1.5f, log);
    EXPECT_FLOAT_EQ(t.time(), 1.5f);
    t.update(1.0f, log); // reaches 2.0 and comes back 0.5
    EXPECT_FLOAT_EQ(t.time(), 1.5f) << "turned at the end";
    EXPECT_NEAR(log.last["door.height"].x, 7.5f, 1e-4f);
    t.update(1.0f, log); // back past the middle: nothing fires
    EXPECT_FLOAT_EQ(t.time(), 0.5f);
    EXPECT_EQ(log.count("event:mid"), 1u);
    t.update(1.0f, log); // turns at 0 and goes forward again past 0.5
    EXPECT_FLOAT_EQ(t.time(), 0.5f);
    t.update(1.0f, log); // forward through the middle
    EXPECT_EQ(log.count("event:mid"), 2u);
    EXPECT_GE(t.loops(), 2);
}

TEST(Timeline, PlayingBackwardsFiresNothingAndStopsAtTheStart) {
    Timeline t = ramp();
    t.addEvent(1.0f, "mid");
    Log log;
    t.speed = -1.0f;
    t.play(2.0f);
    t.update(0.5f, log);
    EXPECT_FLOAT_EQ(t.time(), 1.5f);
    t.update(1.0f, log);
    EXPECT_FLOAT_EQ(t.time(), 0.5f);
    EXPECT_EQ(log.count("event:"), 0u);
    EXPECT_NEAR(log.last["door.height"].x, 2.5f, 1e-4f);
    t.update(1.0f, log);
    EXPECT_FLOAT_EQ(t.time(), 0.0f);
    EXPECT_TRUE(t.finished());
    EXPECT_EQ(log.finishedCount, 1);
    // played again, a finished reverse timeline starts from its end
    t.play();
    EXPECT_FLOAT_EQ(t.time(), 2.0f);
}

TEST(Timeline, SpeedScalesTime) {
    Timeline t = ramp();
    t.speed = 2.0f;
    Log log;
    t.play();
    t.update(0.5f, log);
    EXPECT_FLOAT_EQ(t.time(), 1.0f);
    t.speed = 0.5f;
    t.update(1.0f, log);
    EXPECT_FLOAT_EQ(t.time(), 1.5f);
}

TEST(Timeline, SeekAppliesEverythingAndFiresNothingUnlessAsked) {
    Timeline t = ramp();
    t.addEvent(0.5f, "a");
    t.addEvent(1.5f, "b");
    Log log;
    t.seek(1.0f, log);
    EXPECT_EQ(log.count("event:"), 0u);
    EXPECT_FLOAT_EQ(t.time(), 1.0f);
    EXPECT_FLOAT_EQ(log.last["door.height"].x, 5.0f);
    // seeking to where it already is sends the values again (a fresh view of the scene)
    log.clear();
    t.seek(1.0f, log);
    EXPECT_EQ(log.count("value:door.height"), 1u);
    // forward with events: the ones crossed
    log.clear();
    t.seek(2.0f, log, true);
    EXPECT_EQ(log.only("event:"), (std::vector<std::string>{ "event:b" }));
    // backwards never fires
    log.clear();
    t.seek(0.0f, log, true);
    EXPECT_EQ(log.count("event:"), 0u);
    // out of range is clamped
    t.seek(99.0f, log);
    EXPECT_FLOAT_EQ(t.time(), 2.0f);
    t.seek(-5.0f, log);
    EXPECT_FLOAT_EQ(t.time(), 0.0f);
}

TEST(Timeline, MarkersFireAndCanBeJumpedTo) {
    Timeline t = ramp();
    t.addMarker(1.5f, "act2");
    t.addMarker(0.5f, "act1");
    Log log;
    t.play();
    for (int i = 0; i < 8; ++i) t.update(0.25f, log);
    EXPECT_EQ(log.only("marker:"), (std::vector<std::string>{ "marker:act1", "marker:act2" }));
    EXPECT_TRUE(t.seekToMarker("act1", log));
    EXPECT_FLOAT_EQ(t.time(), 0.5f);
    EXPECT_FALSE(t.seekToMarker("nowhere", log));
    EXPECT_FLOAT_EQ(t.time(), 0.5f);
    log.clear();
    t.seekToMarker("act2", log, true);
    EXPECT_EQ(log.count("marker:act2"), 1u) << "jumping forward with events fires what it crosses";
}

TEST(Timeline, SpansSwitchTargetsOnAndOff) {
    Timeline t;
    t.addSpan("torch", 0.0f, 1.0f);
    t.addSpan("crate", 0.5f, 1.5f);
    t.addSpan("fog", 1.0f); // to the end
    t.setDuration(2.0f);
    Log log;
    t.play();
    t.update(0.25f, log);
    EXPECT_EQ(log.only("active:"), (std::vector<std::string>{ "active:torch:on" })) << "what is on at the start is announced";
    log.clear();
    t.update(0.5f, log);
    EXPECT_EQ(log.only("active:"), (std::vector<std::string>{ "active:crate:on" }));
    log.clear();
    t.update(0.5f, log); // 1.25
    EXPECT_TRUE(log.has("active:torch:off"));
    EXPECT_TRUE(log.has("active:fog:on"));
    log.clear();
    t.update(0.5f, log); // 1.75
    EXPECT_EQ(log.only("active:"), (std::vector<std::string>{ "active:crate:off" }));
    log.clear();
    t.seek(0.75f, log); // a scrub: everything that is on is sent again, what is off goes off
    EXPECT_TRUE(log.has("active:torch:on"));
    EXPECT_TRUE(log.has("active:crate:on"));
    EXPECT_TRUE(log.has("active:fog:off"));
}

TEST(Timeline, ClipsStartStopAndTellScrubbingWhereTheyAre) {
    Timeline t;
    TimelineClip walk;
    walk.target = "hero";
    walk.clip = "Walk";
    walk.start = 1.0f;
    walk.end = 3.0f;
    walk.loop = true;
    walk.speed = 2.0f;
    t.addClip(walk);
    t.setDuration(4.0f);
    Log log;
    t.play();
    t.update(0.5f, log);
    EXPECT_EQ(log.count("clip:"), 0u);
    t.update(1.0f, log); // 1.5: 0.5 s into the clip at double speed: 1 s of the animation
    EXPECT_EQ(log.only("clip:"), (std::vector<std::string>{ "clip:start:hero:Walk@1" }));
    t.update(1.0f, log); // 2.5: no new events while it plays
    EXPECT_EQ(log.count("clip:"), 1u);
    t.update(1.0f, log); // 3.5: over
    EXPECT_TRUE(log.has("clip:stop:hero:Walk@3"));
    log.clear();
    t.seek(2.0f, log); // a scrub into the middle: Start (it was not playing), then Seek on later scrubs
    EXPECT_EQ(log.only("clip:"), (std::vector<std::string>{ "clip:start:hero:Walk@2" }));
    log.clear();
    t.seek(2.25f, log);
    EXPECT_EQ(log.only("clip:"), (std::vector<std::string>{ "clip:seek:hero:Walk@2.5" }));
    log.clear();
    t.seek(0.0f, log);
    EXPECT_EQ(log.only("clip:"), (std::vector<std::string>{ "clip:stop:hero:Walk@2.5" }));
}

TEST(Timeline, NestedTimelinesPlayInsideAtTheirOwnSpeed) {
    auto inner = std::make_shared<Timeline>();
    const size_t k = inner->addTrack("door", "height");
    inner->addScalarKey(k, 0.0f, 0.0f);
    inner->addScalarKey(k, 2.0f, 10.0f);
    inner->addEvent(1.0f, "inner_mid");
    inner->addSpan("spark", 0.0f, 1.0f);
    Timeline outer;
    outer.addEvent(0.5f, "outer");
    ASSERT_TRUE(outer.addSub(2.0f, inner, 2.0f, "opening")); // from 2 s, twice as fast: lasts 1 s
    outer.setDuration(4.0f);
    EXPECT_EQ(outer.subs()[0].name, "opening");
    Log log;
    outer.play();
    outer.update(1.0f, log);
    EXPECT_EQ(log.count("value:door.height"), 0u) << "nothing before the nested one starts";
    outer.update(1.0f, log); // 2.0: it starts
    EXPECT_TRUE(log.has("active:spark:on"));
    EXPECT_NEAR(log.last["door.height"].x, 0.0f, 1e-4f);
    outer.update(0.5f, log); // 2.5: a half second in, a second of its own time
    EXPECT_NEAR(log.last["door.height"].x, 5.0f, 1e-4f);
    EXPECT_TRUE(log.has("active:spark:off")) << "its span ended at its own 1 s: now";
    EXPECT_EQ(log.only("event:"), (std::vector<std::string>{ "event:outer", "event:inner_mid" }));
    outer.update(1.0f, log); // 3.5: it ended at 3.0 and holds its last value
    EXPECT_EQ(log.count("event:inner_mid"), 1u);
    EXPECT_NEAR(log.last["door.height"].x, 10.0f, 1e-4f) << "a nested timeline that is over holds where it ended";
    // scrubbing into it shows its values
    Log scrub;
    outer.seek(2.25f, scrub);
    EXPECT_NEAR(scrub.last["door.height"].x, 2.5f, 1e-4f);
}

TEST(Timeline, ATimelineCannotContainItself) {
    auto a = std::make_shared<Timeline>();
    auto b = std::make_shared<Timeline>();
    ASSERT_TRUE(a->addSub(0.0f, b));
    EXPECT_FALSE(b->addSub(0.0f, a)) << "a contains b: b cannot contain a";
    EXPECT_FALSE(a->addSub(0.0f, a));
    EXPECT_FALSE(a->addSub(0.0f, nullptr));
    EXPECT_FALSE(a->addSub(0.0f, std::make_shared<Timeline>(), 0.0f)) << "speed above 0";
}

TEST(Timeline, AdditiveTracksAddToWhatIsThere) {
    Timeline t;
    const size_t base = t.addTrack("lamp", "position", ValueKind::Vec3);
    t.addVec3Key(base, 0.0f, glm::vec3(1, 0, 0));
    t.addVec3Key(base, 1.0f, glm::vec3(1, 0, 0));
    const size_t wobble = t.addTrack("lamp", "position", ValueKind::Vec3, true);
    t.addVec3Key(wobble, 0.0f, glm::vec3(0, 0, 0));
    t.addVec3Key(wobble, 1.0f, glm::vec3(0, 2, 0));
    const size_t turn = t.addTrack("lamp", "rotation", ValueKind::Quat);
    t.addQuatKey(turn, 0.0f, glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0)));
    const size_t extra = t.addTrack("lamp", "rotation", ValueKind::Quat, true);
    t.addQuatKey(extra, 0.0f, glm::angleAxis(glm::radians(15.0f), glm::vec3(0, 1, 0)));
    Log log;
    t.evaluate(0.5f, log);
    EXPECT_EQ(log.last["lamp.position"], glm::vec4(1, 1, 0, 0));
    const glm::vec4 r = log.last["lamp.rotation"];
    EXPECT_NEAR(glm::degrees(glm::angle(glm::quat(r.w, r.x, r.y, r.z))), 45.0f, 0.01f);
    // muting a track takes it out
    t.track(wobble)->muted = true;
    t.evaluate(0.5f, log);
    EXPECT_EQ(log.last["lamp.position"], glm::vec4(1, 0, 0, 0));
}

TEST(Timeline, StopGoesBackToTheStartAndStopsWhatWasOn) {
    Timeline t;
    t.addSpan("torch", 0.5f, 1.5f);
    TimelineClip c;
    c.target = "hero";
    c.clip = "Idle";
    c.start = 0.5f;
    t.addClip(c);
    t.setDuration(2.0f);
    Log log;
    t.play();
    t.update(1.0f, log);
    ASSERT_TRUE(log.has("active:torch:on"));
    log.clear();
    t.stop(&log);
    EXPECT_FALSE(t.playing());
    EXPECT_FLOAT_EQ(t.time(), 0.0f);
    EXPECT_TRUE(log.has("active:torch:off"));
    EXPECT_TRUE(log.has("clip:stop:hero:Idle@0.5"));
    EXPECT_EQ(t.loops(), 0);
}

TEST(Timeline, PauseResumeAndReplay) {
    Timeline t = ramp();
    Log log;
    t.play();
    t.update(0.5f, log);
    t.pause();
    t.update(1.0f, log);
    EXPECT_FLOAT_EQ(t.time(), 0.5f) << "paused";
    t.resume();
    t.update(0.5f, log);
    EXPECT_FLOAT_EQ(t.time(), 1.0f);
    t.update(5.0f, log);
    EXPECT_TRUE(t.finished());
    t.play(); // a finished one starts over
    EXPECT_FLOAT_EQ(t.time(), 0.0f);
    EXPECT_TRUE(t.playing());
    t.play(1.5f);
    EXPECT_FLOAT_EQ(t.time(), 1.5f);
}

TEST(Timeline, AnEmptyOrInstantTimelineFinishesAtOnce) {
    Timeline t;
    t.addEvent(0.0f, "now");
    Log log;
    t.play();
    t.update(0.016f, log);
    EXPECT_EQ(log.only("event:"), (std::vector<std::string>{ "event:now" }));
    EXPECT_TRUE(t.finished());
    Timeline empty;
    empty.play();
    empty.update(0.016f, log);
    EXPECT_TRUE(empty.finished());
}

TEST(Timeline, JsonRoundTripsAndKeepsTheShape) {
    Timeline t = ramp();
    t.wrap = WrapMode::PingPong;
    t.speed = 1.5f;
    t.setDuration(3.0f);
    const size_t c = t.addTrack("lamp", "color", ValueKind::Color, true);
    t.addKey(c, 0.0f, glm::vec4(1, 0.5f, 0, 1), Ease::SmoothStep);
    t.addKey(c, 1.0f, glm::vec4(0, 0, 1, 0.5f));
    t.addEvent(1.0f, "ping", "data");
    t.addMarker(0.5f, "mark");
    t.addSpan("torch", 0.5f, 1.5f);
    t.addSpan("fog", 1.0f);
    TimelineClip clip;
    clip.target = "hero";
    clip.clip = "Walk";
    clip.start = 1.0f;
    clip.end = 2.0f;
    clip.loop = true;
    clip.speed = 2.0f;
    clip.blend = 0.5f;
    t.addClip(clip);
    t.addSub(1.0f, std::make_shared<Timeline>(ramp()), 2.0f, "inner");
    const json j = t.toJson();
    Timeline back;
    std::string error;
    ASSERT_TRUE(Timeline::fromJson(j, back, &error)) << error;
    EXPECT_EQ(back.toJson(), j);
    EXPECT_EQ(back.wrap, WrapMode::PingPong);
    EXPECT_FLOAT_EQ(back.speed, 1.5f);
    EXPECT_FLOAT_EQ(back.duration(), 3.0f);
    EXPECT_EQ(back.subs().size(), 1u);
    EXPECT_EQ(back.subs()[0].name, "inner");
    // and plays the same
    Log a, b;
    t.evaluate(1.25f, a);
    back.evaluate(1.25f, b);
    EXPECT_EQ(a.lines, b.lines);
}

TEST(Timeline, JsonErrorsPointAtTheMistake) {
    Timeline t;
    std::string error;
    auto bad = [&](const char* text, const std::string& expect) {
        EXPECT_FALSE(Timeline::fromJson(json::parse(text), t, &error)) << text;
        EXPECT_NE(error.find(expect), std::string::npos) << "'" << error << "' should mention '" << expect << "'";
    };
    bad("[]", "a timeline is an object");
    bad(R"({ "wrap": "sideways" })", "wrap is");
    bad(R"({ "duration": "long" })", "duration must be a number");
    bad(R"({ "tracks": 5 })", "tracks is a list");
    bad(R"({ "tracks": [ 5 ] })", "tracks[0]: a track is an object");
    bad(R"({ "tracks": [ { "property": "x", "keys": [] } ] })", "tracks[0]: needs target");
    bad(R"({ "tracks": [ { "target": "a", "property": "x", "kind": "matrix", "keys": [] } ] })", "kind is scalar");
    bad(R"({ "tracks": [ { "target": "a", "property": "x" } ] })", "needs keys");
    bad(R"({ "tracks": [ { "target": "a", "property": "x", "keys": [ { "v": 1 } ] } ] })", "tracks[0].keys[0]: needs t");
    bad(R"({ "tracks": [ { "target": "a", "property": "x", "keys": [ { "t": 0 } ] } ] })", "needs v");
    bad(R"({ "tracks": [ { "target": "a", "property": "p", "kind": "vec3", "keys": [ { "t": 0, "v": [1, 2] } ] } ] })", "a vec3 value is a list of 3 numbers");
    bad(R"({ "tracks": [ { "target": "a", "property": "x", "keys": [ { "t": 0, "v": 1, "ease": "wobble" } ] } ] })", "ease is");
    bad(R"({ "events": [ { "name": "x" } ] })", "events[0]: needs t");
    bad(R"({ "events": [ { "t": 1 } ] })", "needs name");
    bad(R"({ "spans": [ { "start": 0 } ] })", "needs target");
    bad(R"({ "clips": [ { "target": "a", "clip": "b", "start": 0, "speed": 0 } ] })", "speed must be above 0");
    bad(R"({ "subs": [ { "start": 0 } ] })", "needs timeline");
    bad(R"({ "subs": [ { "start": 0, "timeline": { "wrap": "no" } } ] })", "subs[0].timeline: wrap is");
    // nested too deep
    json deep = json::object();
    for (int i = 0; i < 12; ++i) deep = { { "subs", json::array({ { { "start", 0 }, { "timeline", deep } } }) } };
    bad(deep.dump().c_str(), "nested too deep");
    // a good one still loads after all that
    EXPECT_TRUE(Timeline::fromJson(json::parse(R"({ "events": [ { "t": 1, "name": "x" } ] })"), t, &error));
    EXPECT_EQ(t.events().size(), 1u);
    EXPECT_TRUE(error.empty());
}

TEST(Timeline, CopiesAreIndependentIncludingTheNestedOnes) {
    auto inner = std::make_shared<Timeline>(ramp());
    Timeline a;
    a.addSub(0.0f, inner);
    Timeline b = a;
    b.subs()[0].timeline->addEvent(1.0f, "only_in_b");
    EXPECT_EQ(a.subs()[0].timeline->events().size(), 0u);
    EXPECT_EQ(b.subs()[0].timeline->events().size(), 1u);
    Timeline c;
    c = b;
    EXPECT_EQ(c.subs()[0].timeline->events().size(), 1u);
    EXPECT_NE(c.subs()[0].timeline.get(), b.subs()[0].timeline.get());
}
