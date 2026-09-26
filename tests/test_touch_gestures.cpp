#include "kke/TouchGestures.h"

#include <gtest/gtest.h>

#include <cmath>

TEST(TouchGestures, OneFingerIsNotAGesture) {
    kke::TouchGestures g;
    g.fingerDown(1, { 100, 100 });
    g.fingerMove(1, { 200, 150 });
    EXPECT_EQ(g.fingers(), 1);
    EXPECT_FALSE(g.multiTouch());
    const auto f = g.take();
    EXPECT_FALSE(f.active);
    EXPECT_EQ(f.pan, glm::vec2(0.0f));
}

TEST(TouchGestures, TwoFingerDragPans) {
    kke::TouchGestures g;
    g.fingerDown(1, { 100, 100 });
    g.fingerDown(2, { 300, 100 });
    EXPECT_TRUE(g.multiTouch());
    g.fingerMove(1, { 110, 140 });
    g.fingerMove(2, { 310, 140 });
    const auto f = g.take();
    EXPECT_TRUE(f.active);
    EXPECT_NEAR(f.pan.x, 10.0f, 1e-4f);
    EXPECT_NEAR(f.pan.y, 40.0f, 1e-4f);
    EXPECT_NEAR(f.pinch, 1.0f, 1e-5f);
    EXPECT_NEAR(f.twist, 0.0f, 1e-5f);
    EXPECT_FALSE(g.take().active) << "take() resets";
}

TEST(TouchGestures, PinchAndTwist) {
    kke::TouchGestures g;
    g.fingerDown(7, { 100, 200 });
    g.fingerDown(9, { 300, 200 });
    g.fingerMove(9, { 500, 200 }); // spread to 2x
    auto f = g.take();
    EXPECT_NEAR(f.pinch, 2.0f, 1e-5f);
    g.fingerMove(7, { 300, 400 });  // the pair now points up-right: 45 deg counter-clockwise
    g.fingerMove(7, { 500, 400 });  // straight up on screen: 90 deg counter-clockwise from the start
    f = g.take();
    EXPECT_NEAR(f.twist, 1.5707963f, 1e-4f);
}

TEST(TouchGestures, ThirdFingerAndLiftingEndTheGestureCleanly) {
    kke::TouchGestures g;
    g.fingerDown(1, { 0, 0 });
    g.fingerDown(2, { 100, 0 });
    g.fingerDown(3, { 50, 50 });
    g.fingerMove(3, { 500, 500 }); // not one of the pair
    EXPECT_FALSE(g.take().active);
    g.fingerUp(1);
    EXPECT_TRUE(g.multiTouch()) << "still multi-touch until every finger is up";
    // Fingers 2 and 3 are the pair now; the change of pair makes no jump.
    g.fingerMove(2, { 110, 0 });
    const auto f = g.take();
    EXPECT_NEAR(f.pan.x, 5.0f, 1e-4f);
    g.fingerUp(2);
    g.fingerUp(3);
    g.fingerUp(42); // unknown: ignored
    EXPECT_EQ(g.fingers(), 0);
    EXPECT_FALSE(g.multiTouch());
    g.fingerMove(5, { 1, 1 }); // never went down: ignored
    EXPECT_FALSE(g.take().active);
}

TEST(TouchGestures, RepeatedDownAndClear) {
    kke::TouchGestures g;
    g.fingerDown(1, { 0, 0 });
    g.fingerDown(1, { 10, 0 });
    EXPECT_EQ(g.fingers(), 1);
    g.fingerDown(2, { 20, 0 });
    g.clear();
    EXPECT_EQ(g.fingers(), 0);
    EXPECT_FALSE(g.multiTouch());
}
