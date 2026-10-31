// The game test harness (ENG-F69, kke/TestHarness.h): tapes, shot plans,
// fixed frame time and picture comparison.

#include "kke/TestHarness.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace kke::harness;

namespace {

SDL_Event keyEvent(bool down, SDL_Scancode sc, SDL_Keycode key) {
    SDL_Event e{};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.key = key;
    e.key.mod = SDL_KMOD_LSHIFT;
    e.key.down = down;
    return e;
}

} // namespace

TEST(TestHarness, EveryInputKindSurvivesATape) {
    InputTape tape;
    tape.record(3, keyEvent(true, SDL_SCANCODE_W, SDLK_W));
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "hi there\\x";
    tape.record(3, text);
    SDL_Event motion{};
    motion.type = SDL_EVENT_MOUSE_MOTION;
    motion.motion.x = 10.5f;
    motion.motion.y = 20.25f;
    motion.motion.xrel = -1.5f;
    motion.motion.yrel = 2.0f;
    motion.motion.state = SDL_BUTTON_LMASK;
    tape.record(4, motion);
    SDL_Event click{};
    click.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    click.button.button = SDL_BUTTON_RIGHT;
    click.button.clicks = 2;
    click.button.x = 7.0f;
    click.button.y = 8.0f;
    tape.record(5, click);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = -1.0f;
    tape.record(6, wheel);
    SDL_Event finger{};
    finger.type = SDL_EVENT_FINGER_MOTION;
    finger.tfinger.fingerID = 2;
    finger.tfinger.x = 0.25f;
    finger.tfinger.y = 0.75f;
    finger.tfinger.pressure = 1.0f;
    tape.record(7, finger);
    tape.record(8, keyEvent(false, SDL_SCANCODE_W, SDLK_W));
    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_RESIZED;
    tape.record(8, resize); // not input: left out
    ASSERT_EQ(tape.events().size(), 7u);

    InputTape back;
    std::string error;
    ASSERT_TRUE(back.parse(tape.serialize(), &error)) << error;
    ASSERT_EQ(back.events().size(), 7u);
    EXPECT_EQ(back.lastFrame(), 8u);
    const auto& ev = back.events();
    EXPECT_EQ(ev[0].event.type, SDL_EVENT_KEY_DOWN);
    EXPECT_EQ(ev[0].event.key.scancode, SDL_SCANCODE_W);
    EXPECT_EQ(ev[0].event.key.key, SDLK_W);
    EXPECT_EQ(ev[0].event.key.mod, SDL_KMOD_LSHIFT);
    EXPECT_TRUE(ev[0].event.key.down);
    EXPECT_EQ(ev[1].text, "hi there\\x");
    EXPECT_FLOAT_EQ(ev[2].event.motion.x, 10.5f);
    EXPECT_FLOAT_EQ(ev[2].event.motion.xrel, -1.5f);
    EXPECT_EQ(ev[2].event.motion.state, SDL_BUTTON_LMASK);
    EXPECT_EQ(ev[3].event.button.button, SDL_BUTTON_RIGHT);
    EXPECT_EQ(ev[3].event.button.clicks, 2);
    EXPECT_TRUE(ev[3].event.button.down);
    EXPECT_FLOAT_EQ(ev[4].event.wheel.y, -1.0f);
    EXPECT_EQ(ev[5].event.tfinger.fingerID, 2u);
    EXPECT_FLOAT_EQ(ev[5].event.tfinger.y, 0.75f);
    EXPECT_EQ(ev[6].event.type, SDL_EVENT_KEY_UP);
}

TEST(TestHarness, PlaybackHandsOutEachFrameOnce) {
    InputTape tape;
    std::string error;
    ASSERT_TRUE(tape.parse("kke-input-tape 1\n# a comment\n10 keyup 26 119 0 0\n2 keydown 26 119 0 0\n2 text w\n", &error)) << error;
    EXPECT_TRUE(tape.due(0).empty());
    auto two = tape.due(2);
    ASSERT_EQ(two.size(), 2u) << "hand-written tapes are sorted, same-frame order kept";
    EXPECT_EQ(two[0]->event.type, SDL_EVENT_KEY_DOWN);
    EXPECT_STREQ(two[1]->event.text.text, "w") << "text points at the tape's copy";
    EXPECT_TRUE(tape.due(2).empty());
    EXPECT_FALSE(tape.finished());
    EXPECT_EQ(tape.due(50).size(), 1u) << "late frames still get their events";
    EXPECT_TRUE(tape.finished());
}

TEST(TestHarness, BadTapesSayWhy) {
    InputTape tape;
    std::string error;
    EXPECT_FALSE(tape.parse("3 keydown 1 2 3 0\n", &error));
    EXPECT_NE(error.find("not an input tape"), std::string::npos);
    EXPECT_FALSE(tape.parse("kke-input-tape 1\n3 jump 1\n", &error));
    EXPECT_NE(error.find("line 2"), std::string::npos);
    EXPECT_NE(error.find("jump"), std::string::npos);
    EXPECT_FALSE(tape.parse("kke-input-tape 1\n3 keydown 1\n", &error));
    EXPECT_NE(error.find("missing fields"), std::string::npos);
}

TEST(TestHarness, TapeFilesRoundTrip) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "kke_test_harness.tape";
    InputTape tape;
    tape.record(1, keyEvent(true, SDL_SCANCODE_SPACE, SDLK_SPACE));
    ASSERT_TRUE(tape.save(path.string()));
    InputTape back;
    std::string error;
    ASSERT_TRUE(back.load(path.string(), &error)) << error;
    EXPECT_EQ(back.events().size(), 1u);
    std::filesystem::remove(path);
    EXPECT_FALSE(back.load(path.string(), &error));
}

TEST(TestHarness, ShotPlans) {
    std::string error;
    auto plan = parseShotPlan("300:b.png,120:shots/a.png,", &error);
    ASSERT_TRUE(plan) << error;
    ASSERT_EQ(plan->size(), 2u);
    EXPECT_EQ((*plan)[0].frame, 120u);
    EXPECT_EQ((*plan)[0].path, "shots/a.png");
    EXPECT_EQ((*plan)[1].frame, 300u);
    EXPECT_FALSE(parseShotPlan("a.png", &error));
    EXPECT_FALSE(parseShotPlan("12x:a.png", &error));
    EXPECT_FALSE(parseShotPlan("12:", &error));
    EXPECT_TRUE(parseShotPlan("")->empty());
}

TEST(TestHarness, FixedFrameTime) {
    EXPECT_FLOAT_EQ(*parseFixedDt("60"), 1.0f / 60.0f);
    EXPECT_FLOAT_EQ(*parseFixedDt("0.02"), 0.02f);
    EXPECT_FALSE(parseFixedDt("0"));
    EXPECT_FALSE(parseFixedDt("-5"));
    EXPECT_FALSE(parseFixedDt("fast"));
    EXPECT_FALSE(parseFixedDt("60fps"));
}

TEST(TestHarness, ComparingPictures) {
    const uint32_t w = 10, h = 10;
    std::vector<uint8_t> a(w * h * 4, 100), b = a;
    ImageDiff same = compareImages(a, w, h, b, w, h);
    EXPECT_TRUE(same.sameSize);
    EXPECT_EQ(same.maxAbs, 0);
    EXPECT_TRUE(std::isinf(same.psnr));
    EXPECT_TRUE(imagesMatch(same));

    for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<uint8_t>(100 + 5); // under the tolerance everywhere
    ImageDiff noise = compareImages(a, w, h, b, w, h, 8);
    EXPECT_EQ(noise.differingFraction, 0.0);
    EXPECT_NEAR(noise.meanAbs, 5.0, 1e-9);
    EXPECT_TRUE(imagesMatch(noise));

    b = a;
    b[(3 * w + 4) * 4 + 1] = 200; // one pixel's green way off
    ImageDiff one = compareImages(a, w, h, b, w, h, 8);
    EXPECT_DOUBLE_EQ(one.differingFraction, 0.01);
    EXPECT_EQ(one.maxAbs, 100);
    EXPECT_FALSE(imagesMatch(one, 0.002));
    EXPECT_TRUE(imagesMatch(one, 0.02));
    const std::vector<uint8_t> pic = diffImage(a, b, w, h);
    ASSERT_EQ(pic.size(), a.size());
    EXPECT_EQ(pic[(3 * w + 4) * 4 + 0], 255);
    EXPECT_EQ(pic[(3 * w + 4) * 4 + 1], 0);
    EXPECT_EQ(pic[0], 25) << "the rest is the reference, dimmed";

    ImageDiff sizes = compareImages(a, w, h, a, 5, 20);
    EXPECT_FALSE(sizes.sameSize);
    EXPECT_FALSE(imagesMatch(sizes));
}
