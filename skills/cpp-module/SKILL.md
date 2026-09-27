---
name: cpp-module
description: Write C++ for a KKE game or the engine - a new kke::Module, new Lua functions for scripts, unit tests - with zero warnings. Use when Lua can't do it or the task is engine work.
---

# C++ in KKE: modules, Lua bindings, tests

Try Lua first (lua-scripting skill). C++ is for new kinds of systems and
for work too heavy for Lua. Engine changes (in `engine/`) also follow
`AI_GUIDE.md`.

## A module

A game is a list of modules in its `main.cpp`. A module is a class:

```cpp
// games/my_game/Boat.h
#pragma once
#include <kke/Module.h>
#include <glm/glm.hpp>

namespace my_game {
class Boat : public kke::Module {
public:
    const char* name() const override { return "Boat"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;              // once, after its dependencies
    void update(const kke::UpdateContext& ctx) override;    // every frame; ctx.dt in seconds
private:
    kke::Application* m_app = nullptr;
    float m_speed = 0.0f;
    glm::vec3 m_target{0.0f};
    bool m_ready = false;
};
}
```

```cpp
// games/my_game/Boat.cpp
#include "Boat.h"
#include <kke/Application.h>
#include <kke/modules/RigidBodyModule.h>
#include <kke/modules/ScriptModule.h>
#include <lauxlib.h>   // luaL_error, luaL_check*
#include <lua.h>       // lua_push*
#include <typeindex>

namespace my_game {
std::vector<kke::ModuleDependency> Boat::dependencies() const {
    // true = required (the game won't start without it, and says why); false = optional
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the boat is a physics body" },
             { std::type_index(typeid(kke::ScriptModule)), false, "the boat.* Lua functions" } };
}

void Boat::init(kke::Application& app) {
    m_app = &app;
    auto* rigid = app.getModule<kke::RigidBodyModule>();   // null only if optional and missing
    (void)rigid;
}

void Boat::update(const kke::UpdateContext& ctx) {
    (void)ctx;
}
}
```

Then: add `Boat.cpp` to the sources in `games/my_game/CMakeLists.txt`,
`#include "Boat.h"` and `app.addModule<my_game::Boat>();` in `main.cpp`,
and `cmake --build build --target my_game`.

Other methods to override when needed: `fixedUpdate` (60 Hz),
`render`, `renderShadow`, `renderUi` (developer ImGui panel),
`onEvent(const SDL_Event&)`, `shutdown`. All are in
`engine/include/kke/Module.h`. A module that throws is disabled and
logged; the game carries on.

The working example of all of this: `games/cookbook/CookbookPlayer.*`
(explained in `docs/cookbook/cpp.md`); the smallest: `games/template/PlayerModule.*`.

## Lua functions from C++

Register them at the end of `Boat::init` (scripts can call them from
hooks: they appear after the scripts first load):

```cpp
if (auto* scripts = app.getModule<kke::ScriptModule>()) {
    kke::ScriptVM& vm = scripts->vm();
    vm.registerFunction("boat", "speed", [this](lua_State* L) {        // boat.speed() -> number
        lua_pushnumber(L, m_speed);
        return 1;                                                     // how many results
    });
    vm.registerFunction("boat", "moveTo", [this](lua_State* L) {       // boat.moveTo(Vec)
        m_target = kke::ScriptVM::toVec3(L, 1);
        if (!m_ready) return luaL_error(L, "boat.moveTo: the boat isn't in the water yet");
        return 0;
    });
}
```

- Arguments: `luaL_checkstring/checknumber(L, i)`, `luaL_optnumber(L, i, def)`,
  `ScriptVM::toVec3(L, i)`, `ScriptVM::fieldVec3(L, table, "pos", def)`.
- Results: `lua_pushnumber`, `lua_pushboolean`, `ScriptVM::pushVec3`.
- `luaL_error` reports a mistake with the script's file and line and
  stops that call, not the game.
- Registering with a `kke::ApiFunction` (label, doc, typed parameters;
  `kke/LuaApi.h`) also makes it a node-graph block and a row in the
  generated Lua API reference.
- Document new functions in `docs/SCRIPTING.md` (engine) or the game's
  README (game).

## Tests

Pure logic goes in a header or `.cpp` with no GPU or window, and gets a
GoogleTest in `tests/` (add the file to `tests/CMakeLists.txt`):

```cpp
#include <gtest/gtest.h>
TEST(Boat, StopsAtTheDock) {
    my_game::BoatPath path({0, 0, 0}, {10, 0, 0});   // your own class: pure logic, no window
    for (int i = 0; i < 600; ++i) path.step(1.0f / 60.0f);
    EXPECT_NEAR(path.position().x, 10.0f, 0.01f);
}
```

```bash
cmake --build build --target kke_tests && ./build/bin/kke_tests --gtest_filter='Boat*'
tools/check_game my_game      # and the game itself still runs clean
```

## Rules

- **Zero warnings.** CI builds with warnings as errors. Fix the cause
  (initialise the variable, use the right type); never silence it with a
  pragma, a flag or a cast that hides a real problem.
- C++20, `namespace kke` for engine code, your own namespace for a game.
- A new third-party library needs a row in `docs/DEPENDENCIES.md` (CI
  checks) and a permissive licence. Prefer a proven library to writing
  your own.
- Config and data files load as JSON or YAML interchangeably, through
  the engine's shared loader.
- Don't claim it works until it built and ran (check-and-debug skill).
