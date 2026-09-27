# C++

Lua is for the game; C++ is for the engine and for anything that needs
to be fast or to talk to the engine directly. A KKE game in C++ is a list
of **modules**, and this page shows how to write one, give Lua a way to
use it, and test it.

## Your own module

A module is a class with a name, the modules it depends on, and the
lifecycle methods it needs. This is the whole of the cookbook's
`CookbookPlayer` header, minus its private members:

```cpp
class CookbookPlayer : public kke::Module {
public:
    const char* name() const override { return "CookbookPlayer"; }
    std::vector<kke::ModuleDependency> dependencies() const override;
    void init(kke::Application& app) override;             // once, in dependency order
    void update(const kke::UpdateContext& ctx) override;   // every frame (ctx.dt)
    void render(const kke::RenderContext& ctx) override;   // draw
    void renderShadow(const kke::ShadowRenderContext& ctx) override;
    void onEvent(const SDL_Event& event) override;         // raw input, window events
};
```

Dependencies are how a module finds the others, and they decide the
order of `init`:

```cpp
std::vector<kke::ModuleDependency> CookbookPlayer::dependencies() const {
    return { { std::type_index(typeid(kke::RigidBodyModule)), true, "the character controller and collision" },
             { std::type_index(typeid(kke::InputModule)), true, "move, look and camera actions" },
             { std::type_index(typeid(kke::ScriptModule)), false, "the view.* and player.* Lua bindings" } };
}
```

`true` is required (the game refuses to start without it, with that
reason in the log); `false` is optional, and `app.getModule<T>()` returns
null when it's missing. Then `main.cpp` lists the modules:

```cpp
kke::Application app("KKE Cookbook", 1280, 720);
app.addModule<kke::SettingsModule>("settings.json");
app.addModule<kke::InputModule>("input.json");
app.addModule<kke::RigidBodyModule>();
app.addModule<kke::ModelModule>();
app.addModule<kke::UiModule>();
app.addModule<kke::ScriptModule>("scripts");
app.addModule<cookbook::CookbookPlayer>();
app.run();
```

Other lifecycle methods, when you need them: `fixedUpdate` (60 Hz, for
physics-rate logic), `frameStart`/`frameEnd`, `compute` (GPU work before
rendering), `renderUi` (a Dear ImGui panel in developer builds),
`shutdown`. A module that throws in any of them is disabled and logged,
and the rest of the game goes on. `engine/include/kke/Module.h` has them
all; `games/template/` is the smallest complete game, and `tools/new_game
NAME` copies it for you ([Make your own game](../tutorials/getting-started.md)).

## Your own Lua bindings

A binding is a table name, a function name and a C++ function. It reads
arguments from the Lua stack, pushes results, and returns how many. The
cookbook's `view` table, which the [cameras](cameras.md) page uses from
Lua, is all of these:

```cpp title="games/cookbook/CookbookPlayer.cpp"
--8<-- "games/cookbook/CookbookPlayer.cpp:lua"
```

- `luaL_checkstring(L, 1)`, `luaL_optnumber(L, 1, 0.5)`, `lua_toboolean`:
  Lua's own API reads arguments by position. `kke::ScriptVM::toVec3(L, i)`
  reads a `Vec`, `fieldVec3(L, table, "pos", fallback)` a named field of a
  table argument, and `pushVec3` returns one.
- `luaL_error(L, ...)` reports a mistake with the script's file and line,
  and stops that call, not the game.
- Anything registered with a `kke::ApiFunction` (label, doc, typed
  parameters) instead of a bare name also becomes a block in the node
  graph and a row in the [Lua API reference](../reference/lua-api.md).

`kke::ScriptVM` runs scripts on their own coroutines with an instruction
budget and a memory cap, so a script can't hang or exhaust the game;
[Scripting in Lua](../SCRIPTING.md#safety-and-limits) has the limits.

## Testing your code

The engine's tests are GoogleTest, in `tests/`, and run as `kke_tests`.
Pure logic (which is most game logic once it's separated from drawing)
tests in a few lines; the cookbook's tests are in `tests/test_cookbook.cpp`:

```cpp
TEST(Cookbook, TurnTowardsStopsAtTheLimit) {
    const glm::vec3 ahead(0, 0, 1), side(1, 0, 0);
    const glm::quat limited = cookbook::turnTowards(ahead, side, 30.0f);
    EXPECT_NEAR(glm::degrees(std::acos(glm::dot(limited * ahead, ahead))), 30.0f, 0.01f);
}
```

Input, physics and Lua all test without a window: `kke::InputMap` reads
from any `kke::InputState` (a fake keyboard in the test), a
`kke::RigidWorld` steps by itself, and a `kke::ScriptVM` runs Lua with
whatever bindings you give it. The [input](input.md#binding-in-c-pads-holds-chords-and-axes)
and [physics](physics.md#physics-from-c) pages show one of each.

Run them with `./build/bin/kke_tests`, or one suite:
`./build/bin/kke_tests --gtest_filter='Cookbook*'`.

## Going further

- **Drawing**: `kke::DynamicMeshRenderer` draws a mesh you build from
  vertices (the cookbook's player body); `kke::ModelModule` loads FBX and
  OBJ and draws instances with skinning.
- **Warnings are errors**: the engine builds with
  `-DKKE_WARNINGS_AS_ERRORS=ON` in CI, and a Release build with any
  warning at all fails. Fix them as they come.
- **Reading the engine**: every `engine/include/kke/*.h` starts with a
  comment saying what it's for and why it's built that way, and
  [`AI_GUIDE.md`](../../AI_GUIDE.md) is the map of the codebase.
- **Where the design lives**: the [guides](../SCRIPTING.md) are one page
  per system; the [roadmap](../../ROADMAP.md) says what's solid and what
  isn't yet.
