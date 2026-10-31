#include "kke/Subtitles.h"

#include <gtest/gtest.h>

using kke::SubtitleQueue;

TEST(Subtitles, ReadingTimeFollowsTheTextAndIsBounded) {
    const float fades = SubtitleQueue::kFadeIn + SubtitleQueue::kFadeOut;
    EXPECT_NEAR(SubtitleQueue::readingSeconds("Hi"), 1.5f + fades, 1e-4f) << "short lines stay at least a second and a half";
    EXPECT_NEAR(SubtitleQueue::readingSeconds(std::string(34, 'a')), 2.0f + fades, 1e-4f) << "17 characters a second";
    EXPECT_NEAR(SubtitleQueue::readingSeconds(std::string(1000, 'a')), 10.0f + fades, 1e-4f) << "and never longer than ten";
    // UTF-8 counts characters: 17 two-byte letters read in one second, not two.
    std::string dutch;
    for (int i = 0; i < 34; ++i) dutch += "\xC3\xA9";
    EXPECT_NEAR(SubtitleQueue::readingSeconds(dutch), 2.0f + fades, 1e-4f);
}

TEST(Subtitles, LinesAppearAtOnceUpToTheLimitAndTheRestWait) {
    SubtitleQueue q;
    q.setMaxLines(2);
    q.say("one");
    q.say("two");
    q.say("three");
    q.update(0.0f);
    EXPECT_EQ(q.shown(), 2u);
    EXPECT_EQ(q.pending(), 1u);
    const auto v = q.visible();
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].text, "one");
    EXPECT_EQ(v[1].text, "two");
    // After "one" has been read, "three" takes its place and starts its own clock.
    q.update(SubtitleQueue::readingSeconds("one") + 0.01f);
    const auto later = q.visible();
    ASSERT_EQ(later.size(), 1u) << "'two' was queued with the same length, so it is gone too, and 'three' has just come up";
    EXPECT_EQ(later[0].text, "three");
    EXPECT_NEAR(later[0].alpha, 0.0f, 1e-4f) << "starts faded out and fades in";
}

TEST(Subtitles, FadeInHoldAndFadeOut) {
    SubtitleQueue q;
    q.say("hello there", "Kees", 2.0f);
    q.update(0.0f);
    EXPECT_NEAR(q.visible()[0].alpha, 0.0f, 1e-4f);
    q.update(SubtitleQueue::kFadeIn / 2);
    EXPECT_NEAR(q.visible()[0].alpha, 0.5f, 1e-3f);
    q.update(SubtitleQueue::kFadeIn / 2 + 0.5f);
    EXPECT_NEAR(q.visible()[0].alpha, 1.0f, 1e-4f);
    EXPECT_EQ(q.visible()[0].speaker, "Kees");
    q.update(1.6f); // 2.0 s of text plus the fades is 2.5 s: now 2.25 s in, inside the fade out
    ASSERT_EQ(q.visible().size(), 1u);
    EXPECT_LT(q.visible()[0].alpha, 1.0f);
    q.update(1.0f);
    EXPECT_TRUE(q.visible().empty());
    EXPECT_TRUE(q.empty());
}

TEST(Subtitles, AnEmptyTextIsNotALineAndLinesCanBeRemovedOrCleared) {
    SubtitleQueue q;
    EXPECT_EQ(q.say(""), 0u);
    const uint32_t a = q.say("a"), b = q.say("b");
    EXPECT_NE(a, b);
    q.update(0.0f);
    q.remove(a);
    ASSERT_EQ(q.visible().size(), 1u);
    EXPECT_EQ(q.visible()[0].id, b);
    q.clear();
    EXPECT_TRUE(q.empty());
    q.update(1.0f); // updating nothing is fine
}
