// Tests for kke::EngineError — the structured error type that lets a
// module (and eventually the Lua scripting layer) report a plain-
// language message and a location alongside the usual technical
// exception text. See kke/EngineError.h for the full reasoning.

#include "kke/EngineError.h"
#include <gtest/gtest.h>

using kke::EngineError;
using kke::ErrorSource;

TEST(EngineError, WhatReturnsTechnicalMessageNotFriendlyOne) {
    EngineError err("The recipe needs more sugar", "std::out_of_range: index 5 >= size 3");
    // Deliberate: what() must return the TECHNICAL text so EngineError
    // behaves exactly like any other std::exception for code that
    // doesn't know it's special (generic catch blocks, plain logging).
    EXPECT_STREQ(err.what(), "std::out_of_range: index 5 >= size 3");
    EXPECT_EQ(err.friendlyMessage(), "The recipe needs more sugar");
}

TEST(EngineError, DefaultSourceIsEngine) {
    EngineError err("friendly", "technical");
    EXPECT_EQ(err.source(), ErrorSource::Engine);
}

TEST(EngineError, SourceCanBeExplicitlyScript) {
    EngineError err("friendly", "technical", ErrorSource::Script);
    EXPECT_EQ(err.source(), ErrorSource::Script);
}

TEST(EngineError, NoLocationByDefault) {
    EngineError err("friendly", "technical");
    EXPECT_FALSE(err.hasLocation());
    EXPECT_EQ(err.file(), "");
    EXPECT_EQ(err.line(), 0);
}

TEST(EngineError, LocationWhenProvided) {
    EngineError err("friendly", "technical", ErrorSource::Script, "quest.lua", 42);
    EXPECT_TRUE(err.hasLocation());
    EXPECT_EQ(err.file(), "quest.lua");
    EXPECT_EQ(err.line(), 42);
}

TEST(EngineError, IsARealStdException) {
    // Must be catchable as std::exception — this is the whole point:
    // Application::safeInvoke's existing `catch (const std::exception&)`
    // path must still work for code that doesn't know EngineError exists,
    // and EngineError-aware code catches the more specific type first.
    bool caught = false;
    try {
        throw EngineError("friendly", "technical");
    } catch (const std::exception& e) {
        caught = true;
        EXPECT_STREQ(e.what(), "technical");
    }
    EXPECT_TRUE(caught);
}

TEST(EngineError, ScriptErrorMacroSetsScriptSourceAndRealLocation) {
    try {
        throw KKE_SCRIPT_ERROR("The recipe needs more sugar", "index out of range");
    } catch (const EngineError& e) {
        EXPECT_EQ(e.source(), ErrorSource::Script);
        EXPECT_TRUE(e.hasLocation());
        // The macro splices in this test file's own __FILE__/__LINE__ —
        // just confirming it actually points somewhere real, not a
        // fixed/hardcoded value the macro forgot to substitute.
        EXPECT_NE(e.file().find("test_engine_error.cpp"), std::string::npos);
        EXPECT_GT(e.line(), 0);
    }
}

TEST(EngineError, EngineErrorMacroSetsEngineSource) {
    try {
        throw KKE_ENGINE_ERROR("Something the engine itself got wrong", "null pointer dereference");
    } catch (const EngineError& e) {
        EXPECT_EQ(e.source(), ErrorSource::Engine);
        EXPECT_TRUE(e.hasLocation());
    }
}
