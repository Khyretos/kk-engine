#include "kke/Footsteps.h"
#include "kke/ImpactSynth.h"

#include <gtest/gtest.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

using namespace kke;

namespace {

// A walk cycle: each foot lifts `lift` m for half of each `period`, the
// two feet half a period apart, ankle resting 0.08 m above the ground.
int walk(FootstepDetector& d, float seconds, float period, float lift, float speed, std::vector<FootstepEvent>& out,
         bool grounded = true) {
    const float dt = 1.0f / 60.0f;
    const size_t before = out.size();
    for (float t = 0.0f; t < seconds; t += dt) {
        for (int foot = 0; foot < 2; ++foot) {
            const float phase = std::fmod(t / period + 0.5f * float(foot), 1.0f);
            const float h = 0.08f + (phase < 0.5f ? lift * std::sin(phase * 2.0f * glm::pi<float>()) : 0.0f);
            d.update(foot, glm::vec3(0.0f), h, grounded, speed, dt, out);
        }
    }
    return int(out.size() - before);
}

double rms(const SoundBuffer& b) {
    double s = 0.0;
    for (float x : b.samples) s += double(x) * x;
    return b.samples.empty() ? 0.0 : std::sqrt(s / double(b.samples.size()));
}

} // namespace

TEST(Footsteps, OneStepPerFootPerCycle) {
    FootstepDetector d;
    std::vector<FootstepEvent> ev;
    // 1 s cycles for 10 s: 10 steps per foot (give or take the first).
    walk(d, 10.0f, 1.0f, 0.15f, 1.5f, ev);
    int left = 0, right = 0;
    for (const auto& e : ev) (e.foot == 0 ? left : right)++;
    EXPECT_GE(left, 9);
    EXPECT_LE(left, 10);
    EXPECT_GE(right, 9);
    EXPECT_LE(right, 10);
}

TEST(Footsteps, StandingStillIsSilent) {
    FootstepDetector d;
    std::vector<FootstepEvent> ev;
    // Idle sway: the feet move a centimetre, never a lift.
    walk(d, 5.0f, 2.0f, 0.01f, 0.0f, ev);
    EXPECT_TRUE(ev.empty());
}

TEST(Footsteps, RunningIsLouderThanWalking) {
    FootstepDetector walker, runner;
    std::vector<FootstepEvent> w, r;
    walk(walker, 4.0f, 1.0f, 0.12f, 1.5f, w);
    walk(runner, 4.0f, 0.6f, 0.25f, 6.0f, r);
    ASSERT_FALSE(w.empty());
    ASSERT_FALSE(r.empty());
    EXPECT_GT(r.back().intensity, w.back().intensity + 0.3f);
    EXPECT_LE(r.back().intensity, 1.0f);
}

TEST(Footsteps, MinIntervalStopsJitterFromDoubling) {
    FootstepDetector d;
    std::vector<FootstepEvent> ev;
    // A foot that shakes 10 cm up and down 20 times a second: at most one
    // step per minInterval, not one per shake.
    const float dt = 1.0f / 120.0f;
    for (int i = 0; i < 120; ++i) {
        const float h = 0.08f + ((i / 3) % 2 ? 0.1f : 0.0f);
        d.update(0, glm::vec3(0.0f), h, true, 2.0f, dt, ev);
    }
    EXPECT_LE(int(ev.size()), int(1.0f / d.settings.minInterval) + 1);
}

TEST(Footsteps, LandingIsOneLoudStepPerFoot) {
    FootstepDetector d;
    std::vector<FootstepEvent> ev;
    walk(d, 2.0f, 1.0f, 0.15f, 1.0f, ev);
    ev.clear();
    // A 0.8 s fall: no steps in the air.
    walk(d, 0.8f, 1.0f, 0.15f, 1.0f, ev, false);
    EXPECT_TRUE(ev.empty());
    // Touch down standing: both feet land, loud.
    const float dt = 1.0f / 60.0f;
    for (int foot = 0; foot < 2; ++foot) d.update(foot, glm::vec3(0.0f), 0.08f, true, 0.0f, dt, ev);
    ASSERT_EQ(ev.size(), 2u);
    EXPECT_GE(ev[0].intensity, 0.9f);
}

TEST(Footsteps, LearnsRestHeightOfAnyRig) {
    // A rig whose ankle rests 0.3 m up (a big character, or a model
    // origin that isn't at the sole): steps are still found.
    FootstepDetector d;
    std::vector<FootstepEvent> ev;
    const float dt = 1.0f / 60.0f;
    for (float t = 0.0f; t < 6.0f; t += dt) {
        const float phase = std::fmod(t, 1.0f);
        const float h = 0.3f + (phase < 0.5f ? 0.2f * std::sin(phase * 2.0f * glm::pi<float>()) : 0.0f);
        d.update(0, glm::vec3(0.0f), h, true, 1.5f, dt, ev);
    }
    EXPECT_NEAR(d.restHeight(0), 0.3f, 0.02f);
    EXPECT_GE(ev.size(), 5u);
}

TEST(FootstepSynth, DeterministicQuietAndMaterialSpecific) {
    AudioMaterialTable t;
    const SoundBuffer a = synthesizeFootstep(t.get(AudioMaterialTable::Stone), {0.6f, 7});
    const SoundBuffer b = synthesizeFootstep(t.get(AudioMaterialTable::Stone), {0.6f, 7});
    const SoundBuffer dirt = synthesizeFootstep(t.get(AudioMaterialTable::Dirt), {0.6f, 7});
    ASSERT_FALSE(a.samples.empty());
    EXPECT_EQ(a.samples, b.samples);
    EXPECT_NE(a.samples, dirt.samples);
    // Short (a step, not a ring) and never clipping.
    EXPECT_LT(a.seconds(), 0.6f);
    for (float x : a.samples) ASSERT_LE(std::fabs(x), 1.0f);
    // Quieter than a hard impact of the same material.
    const SoundBuffer hit = synthesizeImpact(t.get(AudioMaterialTable::Stone), {1.0f, 1.0f, 7});
    EXPECT_LT(rms(a), rms(hit));
    // Softer steps are quieter.
    const SoundBuffer soft = synthesizeFootstep(t.get(AudioMaterialTable::Stone), {0.1f, 7});
    EXPECT_LT(rms(soft), rms(a));
}

TEST(FootstepSynth, BankCachesFootstepsApartFromImpacts) {
    AudioMaterialTable t;
    ImpactBank bank(t);
    SoundHandle step = bank.getFootstep(AudioMaterialTable::Wood, 0.5f, 3);
    SoundHandle hit = bank.get(AudioMaterialTable::Wood, 0.5f, 3);
    ASSERT_TRUE(step && hit);
    EXPECT_NE(step.get(), hit.get());
    EXPECT_EQ(bank.getFootstep(AudioMaterialTable::Wood, 0.5f, 3).get(), step.get());
}

TEST(Earcons, AllDistinctAndShort) {
    std::vector<SoundBuffer> all;
    for (int i = 0; i < int(Earcon::Count); ++i) {
        SoundBuffer b = synthesizeEarcon(Earcon(i));
        ASSERT_FALSE(b.samples.empty()) << earconName(Earcon(i));
        EXPECT_LT(b.seconds(), 0.5f) << earconName(Earcon(i));
        for (float x : b.samples) ASSERT_LE(std::fabs(x), 1.0f);
        for (const SoundBuffer& o : all) EXPECT_NE(o.samples, b.samples) << earconName(Earcon(i));
        all.push_back(std::move(b));
    }
}

namespace {
// Zero crossings per second: a rough pitch.
double crossingRate(const SoundBuffer& b) {
    int n = 0;
    for (size_t i = 1; i < b.samples.size(); ++i) n += (b.samples[i - 1] < 0.0f) != (b.samples[i] < 0.0f);
    return double(n) / double(b.seconds());
}
} // namespace

TEST(Pings, CloserIsHigherAndOpenSoundsDifferent) {
    AudioMaterialTable t;
    const SoundBuffer nearWall = synthesizePing(1.0f, 12.0f, false, t.get(AudioMaterialTable::Stone));
    const SoundBuffer farWall = synthesizePing(10.0f, 12.0f, false, t.get(AudioMaterialTable::Stone));
    const SoundBuffer open = synthesizePing(12.0f, 12.0f, true, t.get(AudioMaterialTable::Stone));
    ASSERT_FALSE(nearWall.samples.empty());
    ASSERT_FALSE(open.samples.empty());
    EXPECT_GT(crossingRate(nearWall), crossingRate(farWall) * 1.3);
    EXPECT_NE(open.samples, farWall.samples);
}
