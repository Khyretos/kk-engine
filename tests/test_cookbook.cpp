// The cookbook (docs/cookbook/): the code the pages quote is the code these
// tests run. Lua recipes must compile, the play-to-make ones run on a fake
// world, and the cookbook game's C++ helpers (games/cookbook/*.h) behave
// the way the pages say.

#include "Bindings.h"
#include "PhysicsRecipes.h"
#include "Procedural.h"

#include "FakePlayWorld.h"
#include "cookbook_recipes.h"

#include "kke/InputMap.h"
#include "kke/PlayScript.h"
#include "kke/ScriptVM.h"

#include <gtest/gtest.h>
#include <lauxlib.h>
#include <lua.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <tuple>

namespace {

const char* recipe(const std::string& path) {
    for (const CookbookRecipe& r : kCookbookRecipes)
        if (path == r.path) return r.lua;
    return nullptr;
}

class FakeInput : public kke::InputState {
public:
    std::map<std::pair<int, int>, float> values;
    void set(kke::SourceKind k, int code, float v) { values[{ int(k), code }] = v; }
    float value(const kke::InputSource& s, const std::vector<uint32_t>*) const override {
        auto it = values.find({ int(s.kind), s.code });
        return it == values.end() ? 0.0f : it->second;
    }
};

} // namespace

TEST(Cookbook, EveryLuaRecipeCompiles) {
    ASSERT_GT(std::size(kCookbookRecipes), 20u);
    kke::ScriptVM vm;
    lua_State* L = vm.state();
    for (const CookbookRecipe& r : kCookbookRecipes) {
        const std::string name = std::string("@") + r.path;
        const int status = luaL_loadbufferx(L, r.lua, std::strlen(r.lua), name.c_str(), "t");
        EXPECT_EQ(status, LUA_OK) << r.path << ": " << (lua_isstring(L, -1) ? lua_tostring(L, -1) : "");
        lua_pop(L, 1);
    }
}

TEST(Cookbook, PlayRecipeKnocksOverScoresAndStandsBackUp) {
    kke::ScriptVM vm;
    FakePlayWorld world;
    kke::bindPlayBlocks(vm, world);
    const uint32_t person = world.add("person");
    const char* lua = recipe("play/ouch.lua");
    ASSERT_NE(lua, nullptr);
    ASSERT_TRUE(vm.runString(lua, "ouch.lua")) << (vm.errors().empty() ? "" : vm.errors().back().message);

    kke::firePlayHit(vm, { person, "ball", glm::vec3(0.0f), glm::vec3(1.0f), "person" }); // not the bat
    EXPECT_TRUE(world.ragdolls.empty());
    kke::firePlayHit(vm, { person, "bat", glm::vec3(1.0f), glm::vec3(3, 1, 0), "person" });
    ASSERT_EQ(world.ragdolls.size(), 1u);
    EXPECT_EQ(world.points, 1.0);
    ASSERT_FALSE(world.said.empty());
    EXPECT_EQ(world.said.back(), "Ouch!");
    EXPECT_TRUE(world.isDown(person));

    vm.updateTimers(vm.now() + 3.1);
    EXPECT_FALSE(world.isDown(person));
    EXPECT_TRUE(vm.errors().empty());
}

TEST(Cookbook, BatLuaDoesWhatTheBatGraphDoes) {
    kke::ScriptVM vm;
    FakePlayWorld world;
    kke::bindPlayBlocks(vm, world);
    const uint32_t person = world.add("person");
    ASSERT_TRUE(vm.runString(recipe("play/bat.lua"), "bat.lua"));
    kke::firePlayHit(vm, { person, "bat", glm::vec3(1, 1, 1), glm::vec3(4, 1, 0), "person" });
    ASSERT_EQ(world.ragdolls.size(), 1u);
    EXPECT_EQ(world.ragdolls[0].second, glm::vec3(4, 1, 0));
    ASSERT_EQ(world.sounds.size(), 1u);
    EXPECT_EQ(world.sounds[0], "wood");
}

TEST(Cookbook, SpringArrivesWithoutOvershootAtAnyFrameRate) {
    for (float dt : { 1.0f / 30.0f, 1.0f / 240.0f }) {
        float x = 0.0f, v = 0.0f, highest = 0.0f;
        for (float t = 0.0f; t < 2.0f; t += dt) {
            cookbook::springTowards(x, v, 10.0f, 0.2f, dt);
            highest = std::max(highest, x);
        }
        EXPECT_NEAR(x, 10.0f, 0.01f) << dt;
        EXPECT_LE(highest, 10.0f + 1e-3f) << dt;
    }
    // Halfway after one half-life (from rest, it lags a little: critically
    // damped, not exponential), and the same at both frame rates.
    float a = 0.0f, av = 0.0f, b = 0.0f, bv = 0.0f;
    for (int i = 0; i < 30; ++i) cookbook::springTowards(a, av, 1.0f, 0.5f, 1.0f / 60.0f);
    for (int i = 0; i < 120; ++i) cookbook::springTowards(b, bv, 1.0f, 0.5f, 1.0f / 240.0f);
    EXPECT_NEAR(a, b, 1e-4f);
    EXPECT_GT(a, 0.3f);
    EXPECT_LT(a, 0.7f);
}

TEST(Cookbook, ShakeGrowsWithTraumaSquared) {
    EXPECT_EQ(cookbook::shakeOffset(0.0f, 1.7f), glm::vec3(0.0f));
    float small = 0.0f, big = 0.0f;
    for (int i = 0; i < 200; ++i) {
        const float t = i * 0.01f;
        small = std::max(small, glm::length(cookbook::shakeOffset(0.5f, t)));
        big = std::max(big, glm::length(cookbook::shakeOffset(1.0f, t)));
    }
    EXPECT_NEAR(small / big, 0.25f, 0.02f); // (0.5)^2
    EXPECT_LE(big, 6.0f * std::sqrt(3.0f));
}

TEST(Cookbook, TurnTowardsStopsAtTheLimit) {
    const glm::vec3 ahead(0, 0, 1), side(1, 0, 0);
    const glm::quat full = cookbook::turnTowards(ahead, side, 180.0f);
    EXPECT_NEAR(glm::dot(full * ahead, side), 1.0f, 1e-4f);
    const glm::quat limited = cookbook::turnTowards(ahead, side, 30.0f);
    EXPECT_NEAR(glm::degrees(std::acos(glm::dot(limited * ahead, ahead))), 30.0f, 0.01f);
    const glm::quat none = cookbook::turnTowards(ahead, ahead, 30.0f);
    EXPECT_NEAR(glm::dot(none * ahead, ahead), 1.0f, 1e-6f);
    EXPECT_NEAR(glm::dot(cookbook::turnTowards(ahead, -ahead, 180.0f) * ahead, -ahead), 1.0f, 1e-4f);
}

TEST(Cookbook, BindingsDoWhatTheInputPageSays) {
    kke::InputMap in;
    cookbook::addCookbookBindings(in);
    FakeInput keys;
    double now = 0.0;
    auto frame = [&] { in.update(keys, now += 1.0 / 60.0); };

    keys.set(kke::SourceKind::GamepadButton, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 1.0f);
    frame();
    EXPECT_TRUE(in.pressed("camera.next"));
    keys.set(kke::SourceKind::GamepadButton, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 0.0f);
    frame();

    // Hold Q: the charge starts after the hold time; letting go throws.
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_Q, 1.0f);
    frame();
    EXPECT_FALSE(in.held("throw.charge"));
    for (int i = 0; i < 30; ++i) frame();
    EXPECT_TRUE(in.held("throw.charge"));
    EXPECT_FALSE(in.pressed("throw"));
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_Q, 0.0f);
    frame();
    EXPECT_TRUE(in.pressed("throw"));

    // R alone does nothing; Ctrl+R resets.
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_R, 1.0f);
    frame();
    EXPECT_FALSE(in.pressed("level.reset"));
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_R, 0.0f);
    frame();
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_LCTRL, 1.0f);
    frame();
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_R, 1.0f);
    frame();
    EXPECT_TRUE(in.pressed("level.reset"));
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_R, 0.0f);
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_LCTRL, 0.0f);
    frame();

    // Zoom: X in, Z out, a trigger half-pulled is part-way.
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_X, 1.0f);
    frame();
    EXPECT_FLOAT_EQ(in.axis("zoom"), 1.0f);
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_X, 0.0f);
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_Z, 1.0f);
    frame();
    EXPECT_FLOAT_EQ(in.axis("zoom"), -1.0f);
    keys.set(kke::SourceKind::Key, SDL_SCANCODE_Z, 0.0f);
    keys.set(kke::SourceKind::GamepadAxis, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0.55f);
    frame();
    EXPECT_NEAR(in.axis("zoom"), 0.5f, 0.01f); // (0.55 - 0.1 deadzone) / 0.9
}

#if KKE_ENABLE_JOLT
TEST(Cookbook, CrateLandsWhereTheRaySaysTheGroundIs) {
    kke::RigidWorld::Settings s;
    s.threads = 0;
    kke::RigidWorld world(s);
    kke::RigidWorld::BodyDesc floor;
    floor.shape = kke::RigidWorld::Shape::Box;
    floor.motion = kke::RigidWorld::Motion::Static;
    floor.halfExtents = glm::vec3(10.0f, 0.5f, 10.0f);
    floor.position = glm::vec3(0.0f, 1.5f, 0.0f); // top at 2 m
    world.add(floor);

    float height = 0.0f;
    ASSERT_TRUE(cookbook::groundBelow(world, glm::vec3(0.0f, 10.0f, 0.0f), height));
    EXPECT_NEAR(height, 2.0f, 1e-3f);
    EXPECT_FALSE(cookbook::groundBelow(world, glm::vec3(50.0f, 10.0f, 0.0f), height));

    const auto crate = cookbook::dropCrate(world, glm::vec3(0.0f, 6.0f, 0.0f));
    for (int i = 0; i < 240; ++i) world.step(1.0f / 60.0f);
    EXPECT_NEAR(world.position(crate).y, 2.3f, 0.05f); // resting on the floor
}
#endif
