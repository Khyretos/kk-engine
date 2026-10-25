#include "kke/AnimEvents.h"
#include "kke/Animator.h"

#include <gtest/gtest.h>
#include <glm/gtc/matrix_transform.hpp>

namespace {

std::vector<std::string> passed(const std::vector<kke::AnimEvent>& ev, float before, float advance, float length, bool loop, bool fromStart = false) {
    std::vector<const kke::AnimEvent*> out;
    kke::passedEvents(ev, before, advance, length, loop, fromStart, out);
    std::vector<std::string> names;
    for (const kke::AnimEvent* e : out) names.push_back(e->name);
    return names;
}

using Names = std::vector<std::string>;

// One bone, one 1-second clip and one 2-second clip.
kke::ModelData model() {
    kke::ModelData m;
    m.bones.push_back(kke::ModelBone{});
    m.bones[0].name = "root";
    for (float d : { 1.0f, 2.0f }) {
        kke::ModelAnimation a;
        a.name = d == 1.0f ? "Walk_Loop" : "Swing";
        a.duration = d;
        for (int f = 0; f <= int(d * 30); ++f) a.frames.push_back({ glm::mat4(1.0f) });
        m.animations.push_back(a);
    }
    return m;
}

} // namespace

TEST(AnimEvents, KeptInTimeOrder) {
    std::vector<kke::AnimEvent> ev;
    kke::addAnimEvent(ev, 0.75f, "right");
    kke::addAnimEvent(ev, 0.25f, "left");
    kke::addAnimEvent(ev, 0.75f, "dust"); // same time: after the first
    ASSERT_EQ(ev.size(), 3u);
    EXPECT_EQ(ev[0].name, "left");
    EXPECT_EQ(ev[1].name, "right");
    EXPECT_EQ(ev[2].name, "dust");
}

TEST(AnimEvents, FiresWhatTheTimePassedOnce) {
    std::vector<kke::AnimEvent> ev;
    kke::addAnimEvent(ev, 0.25f, "left");
    kke::addAnimEvent(ev, 0.75f, "right");
    EXPECT_EQ(passed(ev, 0.0f, 0.2f, 1.0f, true), Names{});
    EXPECT_EQ(passed(ev, 0.2f, 0.05f, 1.0f, true), Names{ "left" }); // lands exactly on it
    EXPECT_EQ(passed(ev, 0.25f, 0.1f, 1.0f, true), Names{});         // not twice
    EXPECT_EQ(passed(ev, 0.1f, 0.8f, 1.0f, true), (Names{ "left", "right" }));
}

TEST(AnimEvents, LoopsFireAgainEachCycle) {
    std::vector<kke::AnimEvent> ev;
    kke::addAnimEvent(ev, 0.0f, "start");
    kke::addAnimEvent(ev, 0.5f, "mid");
    // 0.9 -> 1.1 wraps: the cycle's start is passed.
    EXPECT_EQ(passed(ev, 0.9f, 0.2f, 1.0f, true), Names{ "start" });
    // A huge step: two and a bit cycles.
    EXPECT_EQ(passed(ev, 0.6f, 2.0f, 1.0f, true), (Names{ "start", "mid", "start", "mid" }));
    // Times already past a cycle are wrapped first.
    EXPECT_EQ(passed(ev, 3.4f, 0.2f, 1.0f, true), Names{ "mid" });
    // Capped at 4 cycles for an absurd dt: mid, then start + mid three times.
    EXPECT_EQ(passed(ev, 0.0f, 100.0f, 1.0f, true).size(), 7u);
}

TEST(AnimEvents, StartAndEndOfAOneShot) {
    std::vector<kke::AnimEvent> ev;
    kke::addAnimEvent(ev, 0.0f, "begin");
    kke::addAnimEvent(ev, 2.0f, "end");
    kke::addAnimEvent(ev, 3.0f, "never"); // past the clip
    EXPECT_EQ(passed(ev, 0.0f, 0.1f, 2.0f, false), Names{});                 // 0 only from the start
    EXPECT_EQ(passed(ev, 0.0f, 0.1f, 2.0f, false, true), Names{ "begin" });
    EXPECT_EQ(passed(ev, 1.9f, 5.0f, 2.0f, false), Names{ "end" });          // stops at the end
    EXPECT_EQ(passed(ev, 2.0f, 1.0f, 2.0f, false), Names{});                 // stays there
}

TEST(AnimEvents, IgnoresNonsense) {
    std::vector<kke::AnimEvent> ev;
    kke::addAnimEvent(ev, 0.5f, "x");
    EXPECT_EQ(passed(ev, 0.0f, -1.0f, 1.0f, true), Names{});
    EXPECT_EQ(passed(ev, 0.0f, 1.0f, 0.0f, true), Names{});
}

TEST(AnimEvents, AnimatorReportsTheCurrentStatesEvents) {
    const kke::ModelData m = model();
    const kke::AnimationSet set(m);
    kke::Animator a(set);
    const int walk = a.addClipState("walk", 0, true);
    const int swing = a.addClipState("swing", 1, false);
    a.addEvent(walk, 0.25f, "left");
    a.addEvent(walk, 0.75f, "right");
    a.addEvent(swing, 0.0f, "wind_up");
    a.addEvent(swing, 0.5f, "hit");
    a.play(walk);
    auto names = [&] {
        Names n;
        for (const auto& e : a.events()) n.push_back(e.name);
        return n;
    };
    a.update(0.2f);
    EXPECT_EQ(names(), Names{});
    a.update(0.1f);
    EXPECT_EQ(names(), Names{ "left" });
    a.update(0.5f);
    EXPECT_EQ(names(), Names{ "right" });
    a.update(0.5f); // 0.8 -> 1.3 (wraps) -> left again
    EXPECT_EQ(names(), Names{ "left" });
    EXPECT_EQ(a.events()[0].state, walk);

    // The swing: its event at 0 on the first update, the hit at 1 s of 2 s.
    // The walk fades out and reports nothing.
    a.play(swing, 0.3f);
    a.update(0.1f);
    EXPECT_EQ(names(), Names{ "wind_up" });
    a.update(0.95f);
    EXPECT_EQ(names(), Names{ "hit" });
    a.update(5.0f);
    EXPECT_EQ(names(), Names{});
    EXPECT_TRUE(a.finished());
    a.update(1.0f);
    EXPECT_EQ(names(), Names{});

    // setProgress forward reports what it skipped; backward nothing.
    a.play(swing, 0.0f, true);
    a.update(0.0f);
    EXPECT_EQ(names(), Names{ "wind_up" });
    a.setProgress(0.6f);
    a.update(0.0f);
    EXPECT_EQ(names(), Names{ "hit" });
    a.setProgress(0.1f);
    a.update(0.0f);
    EXPECT_EQ(names(), Names{});
}

// Events on a blend space are fractions of the shared cycle, whatever its
// length in seconds.
TEST(AnimEvents, BlendSpaceEventsFollowThePhase) {
    const kke::ModelData m = model();
    const kke::AnimationSet set(m);
    kke::Animator a(set);
    kke::BlendSpace1D space;
    space.points = { { 0, 0.0f }, { 1, 2.0f } }; // 1 s cycle at 0, 2 s at 2
    const int move = a.addBlendState("move", space);
    a.addEvent(move, 0.5f, "step");
    a.play(move);
    a.setParameter(2.0f); // 2 s cycle: the step comes at 1 s
    a.update(0.9f);
    EXPECT_TRUE(a.events().empty());
    a.update(0.2f);
    ASSERT_EQ(a.events().size(), 1u);
    EXPECT_EQ(a.events()[0].name, "step");
}
