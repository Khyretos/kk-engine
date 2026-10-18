// More than 4 lights (ENG-G07): how far a point light reaches, which lights
// get the point-shadow slots, and the slots Lua scripts place lights in
// (light.* in docs/SCRIPTING.md).
#include "kke/Application.h"
#include "kke/ScriptLights.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using kke::Light;
using kke::Lighting;
using kke::ScriptLights;

namespace {

Light pointAt(const glm::vec3& p, float range) {
    Light l;
    l.enabled = true;
    l.isDirectional = false;
    l.position = p;
    l.range = range;
    return l;
}

} // namespace

TEST(Lights, SixteenSlots) {
    EXPECT_EQ(Lighting::kMaxLights, 16);
    // A new light reaches everywhere, like every light before ranges existed.
    EXPECT_EQ(Light{}.range, 0.0f);
}

TEST(Lights, FalloffIsOneAtTheLightAndZeroAtRange) {
    const Light l = pointAt(glm::vec3(1.0f, 2.0f, 3.0f), 10.0f);
    EXPECT_FLOAT_EQ(l.falloffAt(glm::vec3(1.0f, 2.0f, 3.0f)), 1.0f);
    EXPECT_FLOAT_EQ(l.falloffAt(glm::vec3(6.0f, 2.0f, 3.0f)), 0.87890625f); // (1 - 0.5^4)^2
    EXPECT_FLOAT_EQ(l.falloffAt(glm::vec3(1.0f, 12.0f, 3.0f)), 0.0f);
    EXPECT_FLOAT_EQ(l.falloffAt(glm::vec3(1.0f, 2.0f, 40.0f)), 0.0f);
}

TEST(Lights, NoRangeAndDirectionalReachEverywhere) {
    EXPECT_FLOAT_EQ(pointAt(glm::vec3(0.0f), 0.0f).falloffAt(glm::vec3(500.0f, 0.0f, 0.0f)), 1.0f);
    Light sun = pointAt(glm::vec3(0.0f), 5.0f);
    sun.isDirectional = true;
    EXPECT_FLOAT_EQ(sun.falloffAt(glm::vec3(500.0f, 0.0f, 0.0f)), 1.0f);
}

TEST(Lights, PointShadowSlotsGoToTheFirstCastingLights) {
    Lighting lighting;
    for (int i : {2, 5, 7, 9, 11}) {
        lighting.lights[i] = pointAt(glm::vec3(float(i), 0.0f, 0.0f), 0.0f);
        lighting.lights[i].castsShadows = true;
    }
    lighting.lights[3] = pointAt(glm::vec3(0.0f), 0.0f); // no shadows
    lighting.lights[0].castsShadows = true;              // the sun: cascades, never a point slot
    const auto slots = lighting.pointShadowSlots();
    for (int i = 0; i < Lighting::kMaxLights; ++i) {
        const int want = i == 2 ? 0 : i == 5 ? 1 : i == 7 ? 2 : i == 9 ? 3 : -1; // 11 is the fifth: no slot left
        EXPECT_EQ(slots[i], want) << "light " << i;
    }
    lighting.shadowsEnabled = false;
    for (int s : lighting.pointShadowSlots()) EXPECT_EQ(s, -1);
}

TEST(ScriptLights, AddTakesTheFirstFreeSlotAfterTheSun) {
    Lighting lighting;
    lighting.lights[1].enabled = true; // a game's own lamp
    ScriptLights sl;
    ScriptLights::Desc d;
    d.position = glm::vec3(1.0f, 2.0f, 3.0f);
    d.color = glm::vec3(1.0f, 0.5f, 0.25f);
    d.intensity = 3.0f;
    d.range = 8.0f;
    d.shadows = true;
    const int s = sl.add(lighting, "a.lua", d);
    ASSERT_EQ(s, 2);
    const Light& l = lighting.lights[2];
    EXPECT_TRUE(l.enabled);
    EXPECT_FALSE(l.isDirectional);
    EXPECT_EQ(l.position, d.position);
    EXPECT_EQ(l.color, d.color);
    EXPECT_FLOAT_EQ(l.intensity, 3.0f);
    EXPECT_FLOAT_EQ(l.range, 8.0f);
    EXPECT_TRUE(l.castsShadows);
    EXPECT_TRUE(sl.owns(2, "a.lua"));
    EXPECT_FALSE(sl.owns(2, "b.lua"));
    EXPECT_FALSE(sl.owns(1, "a.lua"));
}

TEST(ScriptLights, ASwitchedOffLightKeepsItsSlot) {
    Lighting lighting;
    ScriptLights sl;
    const int a = sl.add(lighting, "a.lua", {});
    lighting.lights[a].enabled = false; // light.set(id, {on = false})
    const int b = sl.add(lighting, "a.lua", {});
    EXPECT_NE(a, b);
    EXPECT_EQ(sl.count("a.lua"), 2u);
}

TEST(ScriptLights, FullThenFreedByRemove) {
    Lighting lighting;
    ScriptLights sl;
    for (int i = 1; i < Lighting::kMaxLights; ++i) EXPECT_EQ(sl.add(lighting, "a.lua", {}), i);
    ScriptLights::Desc shadowed;
    shadowed.shadows = true;
    EXPECT_EQ(sl.add(lighting, "a.lua", shadowed), -1); // the key slots are full: no room for a light with shadows
    EXPECT_TRUE(lighting.lights[0].enabled); // the sun stays
    sl.remove(lighting, 7);
    EXPECT_FALSE(lighting.lights[7].enabled);
    EXPECT_FALSE(sl.owns(7, "a.lua"));
    EXPECT_EQ(sl.count("a.lua"), size_t(Lighting::kMaxLights - 2));
    EXPECT_EQ(sl.add(lighting, "b.lua", {}), 7);
}

TEST(ScriptLights, PastTheKeySlotsLightsGoToTheManyLights) {
    // ENG-F08: with the 16 key slots full, a light without shadows becomes one of the many lights (slot 16 and up).
    Lighting lighting;
    ScriptLights sl;
    for (int i = 1; i < Lighting::kMaxLights; ++i) sl.add(lighting, "a.lua", {});
    ScriptLights::Desc lamp;
    lamp.position = glm::vec3(1.0f, 2.0f, 3.0f);
    lamp.range = 0.0f; // everywhere: many lights need a reach, so they get the default
    const int slot = sl.add(lighting, "a.lua", lamp);
    EXPECT_EQ(slot, Lighting::kMaxLights);
    ASSERT_EQ(lighting.manyLights.size(), 1u);
    EXPECT_TRUE(lighting.manyLights[0].enabled);
    EXPECT_FLOAT_EQ(lighting.manyLights[0].range, Lighting::kDefaultManyRange);
    EXPECT_EQ(lighting.at(slot).position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(sl.owns(slot, "a.lua"));
    sl.remove(lighting, slot);
    EXPECT_FALSE(lighting.manyLights[0].enabled);
    EXPECT_EQ(sl.add(lighting, "b.lua", {}), Lighting::kMaxLights); // a freed many light is reused
    EXPECT_EQ(lighting.manyLights.size(), 1u);
}

TEST(ScriptLights, ReleaseAllTakesOnlyThatScriptsLights) {
    Lighting lighting;
    ScriptLights sl;
    const int a1 = sl.add(lighting, "a.lua", {});
    const int b1 = sl.add(lighting, "b.lua", {});
    const int a2 = sl.add(lighting, "a.lua", {});
    sl.releaseAll(lighting, "a.lua");
    EXPECT_FALSE(lighting.lights[a1].enabled);
    EXPECT_FALSE(lighting.lights[a2].enabled);
    EXPECT_TRUE(lighting.lights[b1].enabled);
    EXPECT_EQ(sl.count("a.lua"), 0u);
    EXPECT_EQ(sl.count("b.lua"), 1u);
    EXPECT_TRUE(sl.owns(b1, "b.lua"));
}

TEST(ScriptLights, ClearTakesEveryScriptsLights) {
    Lighting lighting;
    lighting.lights[3].enabled = true; // a game's own lamp stays
    ScriptLights sl;
    const int a = sl.add(lighting, "a.lua", {});
    const int b = sl.add(lighting, "b.lua", {});
    sl.clear(lighting);
    EXPECT_FALSE(lighting.lights[a].enabled);
    EXPECT_FALSE(lighting.lights[b].enabled);
    EXPECT_TRUE(lighting.lights[3].enabled);
    EXPECT_TRUE(lighting.lights[0].enabled);
    EXPECT_EQ(sl.count("a.lua") + sl.count("b.lua"), 0u);
}

TEST(ScriptLights, AnotherScriptNeverTakesASwitchedOffLight) {
    Lighting lighting;
    ScriptLights sl;
    const int a = sl.add(lighting, "a.lua", {});
    lighting.lights[a].enabled = false; // a.lua switched it off; it is still a.lua's
    const int b = sl.add(lighting, "b.lua", {});
    EXPECT_NE(a, b);
    EXPECT_TRUE(sl.owns(a, "a.lua"));
    EXPECT_FALSE(sl.owns(a, "b.lua"));
}

// Shaders that declare their own copy of the lighting UBO's start must keep
// the light count of shaders/lighting_ubo.glsl, or every field after the
// lights (viewProj above all) is read from the wrong offset. ENG-G07 first
// missed model_instanced.vert this way: instanced models vanished.
TEST(Lights, EveryShaderCopyOfTheUboHasAllLights) {
    const std::filesystem::path dir = std::filesystem::path(KKE_SOURCE_DIR) / "shaders";
    const std::string want = "GPULight lights[" + std::to_string(Lighting::kMaxLights) + "];";
    int copies = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir)) {
        std::ifstream f(e.path());
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string text = ss.str();
        if (text.find("uniform LightingUBO") == std::string::npos || e.path().filename() == "lighting_ubo.glsl") continue;
        ++copies;
        EXPECT_NE(text.find(want), std::string::npos) << e.path().filename() << " must declare " << want;
        EXPECT_NE(text.find("vec4 params;"), std::string::npos) << e.path().filename() << ": GPULight needs its third vec4, params";
    }
    EXPECT_GE(copies, 6);
}
