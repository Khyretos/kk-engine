#include "kke/ScriptFx.h"

#include <gtest/gtest.h>

// ENG-G18: fx.* in Lua. ScriptFx remembers which script started which
// particle emitter, so a script can only move or stop its own and its
// emitters stop when it is reloaded or removed.

TEST(ScriptFx, TracksWhoStartedWhat) {
    kke::ScriptFx fx;
    fx.track(7, "a.lua");
    fx.track(9, "b.lua");
    fx.track(12, "a.lua");
    EXPECT_TRUE(fx.owns(7, "a.lua"));
    EXPECT_FALSE(fx.owns(7, "b.lua"));
    EXPECT_FALSE(fx.owns(8, "a.lua"));
    EXPECT_EQ(fx.count("a.lua"), 2u);
    EXPECT_EQ(fx.count("b.lua"), 1u);
    EXPECT_EQ(fx.count("c.lua"), 0u);
}

TEST(ScriptFx, ForgetsWhatWasStopped) {
    kke::ScriptFx fx;
    fx.track(3, "a.lua");
    fx.forget(3);
    EXPECT_FALSE(fx.owns(3, "a.lua"));
    EXPECT_EQ(fx.count("a.lua"), 0u);
    fx.forget(3); // twice is harmless
}

TEST(ScriptFx, ReleasingAScriptHandsBackItsEmitters) {
    kke::ScriptFx fx;
    fx.track(1, "a.lua");
    fx.track(2, "b.lua");
    fx.track(3, "a.lua");
    EXPECT_EQ(fx.releaseAll("a.lua"), (std::vector<kke::ScriptFx::EmitterId>{ 1, 3 }));
    EXPECT_EQ(fx.count("a.lua"), 0u);
    EXPECT_TRUE(fx.owns(2, "b.lua"));
    EXPECT_EQ(fx.clear(), (std::vector<kke::ScriptFx::EmitterId>{ 2 }));
    EXPECT_EQ(fx.count("b.lua"), 0u);
}
