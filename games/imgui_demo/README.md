# Dear ImGui demo

A grid on the ground, an orbit camera, and Dear ImGui's own demo window:
every widget the library has (buttons, sliders, colour pickers, drag and
drop, tables, trees, tabs, menus, popups, text input, plots) in one big
window, plus a small "ImGui Showcase" note that tells you the big window
is the showcase. The engine's own developer panels ("Performance",
"Debug Control", "Camera") sit next to it.

This demo teaches the engine's **developer UI**: the panels you build
for yourself while making a game (tuning sliders, debug toggles, stats,
editors). It is the reference for "which ImGui widget do I want and what
does its code look like". It is not the starting point for menus or HUDs
that players see; that is [RmlUi](../rmlui_demo/README.md) (see "ImGui or
RmlUi?" below).

No screenshot of this demo is in `website/static/media/`.

## Run it

The executable is `imgui_demo` ([CMakeLists.txt](CMakeLists.txt)). It is
always built: no `KKE_ENABLE_*` option guards it, and the `default`
preset includes it ([docs/BUILDING.md](../../docs/BUILDING.md)).

```bash
cmake --workflow --preset default     # or any preset
cd build/bin
./imgui_demo
```

`KKE_SKIP_INTRO=1 ./imgui_demo` skips the logo intro (developer builds).

ImGui remembers window positions and sizes in `imgui.ini` in the folder
you run from. Delete it to get the first-run layout back (AI_GUIDE.md
warns that a stale `imgui.ini` silently overrides layout changes).

In a shipping build (`-DKKE_SHIPPING=ON`) this demo shows only the grid:
the engine compiles out every module's ImGui panel (see "Design
decisions").

## Controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Use any widget | Mouse (click, drag, scroll inside windows), keyboard to type in text fields | no controller binding yet |
| Move / resize / collapse a window | Drag the title bar / drag the corner / click the arrow | no controller binding yet |
| Orbit the camera | Left-drag on empty space | no controller binding yet |
| Pan the camera | Right-drag on empty space | no controller binding yet |
| Zoom | Scroll wheel on empty space | no controller binding yet |
| Auto-orbit on/off, speed | "Camera" panel | no controller binding yet |
| Pause / step one frame | "Debug Control" panel | no controller binding yet |
| Quit | Esc (the window's default) or close the window | |

The camera ignores the mouse while it is over an ImGui window
(`ImGui::GetIO().WantCaptureMouse` in `OrbitCameraModule::update()`), so
dragging a slider never turns the view.

The engine does not turn on ImGui's keyboard or gamepad navigation
(no `ImGuiConfigFlags_NavEnable*` in `engine/src/DebugUi.cpp`), so the
panels are mouse-driven. That fits their purpose: they are for the
developer at a desk. Player-facing UI that must work on a controller is
RmlUi's job.

## How it plays

Nothing to win. Open the sections of "Dear ImGui Demo" (the big window)
to try each widget; the "Help" section and the "Tools" menu inside it
include ImGui's own metrics, style editor and ID stack tool. The demo
window cannot be closed: its close button is wired to a local `bool`
that is set back to `true` every frame.

## How it works

### Startup and the frame

[main.cpp](main.cpp):

```cpp
kke::Application app("Kreative Kompas Engine - ImGui Demo", 1280, 720);
app.setMood("studio");

app.addModule<kke::GridModule>();
app.addModule<kke::OrbitCameraModule>();
app.addModule<kke_demo::ImGuiShowcaseModule>();
app.addModule<kke::DebugControlModule>();
app.addModule<kke::StatsModule>();
```

- `GridModule` draws a fading grid on the Y=0 plane (a pipeline with no
  vertex buffers at all), so the UI is not floating over an empty void.
- `OrbitCameraModule` is the mouse camera, with its "Camera" panel.
- `ImGuiShowcaseModule` is this demo's only code.
- `DebugControlModule` adds "Debug Control" (pause, single step) and the
  red "Emergency Log" that appears only when a module has thrown and been
  disabled.
- `StatsModule` adds "Performance": FPS, CPU and GPU frame time with
  history graphs.
- The `studio` mood (`assets/moods/studio.yaml`) gives calm, neutral
  light.

Each frame, `Application` starts an ImGui frame (`DebugUi::beginFrame()`)
only once it knows the frame will be drawn. Then it calls `renderUi()` on
every module whose panels are visible, and later records ImGui's draw
data into the overlay pass. The comment in `Application.cpp` explains the
ordering: starting the ImGui frame before knowing the swapchain frame
would succeed caused ImGui's "Forgot to call Render()" assertion when the
window was resized.

### The showcase module

[ImGuiShowcaseModule.cpp](ImGuiShowcaseModule.cpp) is the whole demo:

```cpp
void ImGuiShowcaseModule::renderUi() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340, 100), ImGuiCond_FirstUseEver);
    ImGui::Begin("ImGui Showcase");
    ImGui::TextWrapped(
        "The big window elsewhere on screen is Dear ImGui's own built-in "
        "demo (ImGui::ShowDemoWindow()) -- every widget type this UI "
        "library supports, in one place, maintained by ImGui itself.");
    ImGui::End();

    bool alwaysOpen = true;
    ImGui::ShowDemoWindow(&alwaysOpen);
}
```

This is the pattern every ImGui panel in the engine follows:

1. Override `renderUi()` in your module (declared in
   [kke/Module.h](../../engine/include/kke/Module.h): "build any ImGui::
   panels this module wants on screen").
2. Optionally place the window the first time only
   (`ImGuiCond_FirstUseEver`), so the player's own moves, saved in
   `imgui.ini`, win afterwards.
3. `ImGui::Begin("Title")`, widgets, `ImGui::End()`. ImGui is immediate
   mode: there is no retained UI tree. You call the widgets every frame
   and they return whether they were used this frame, for example
   `if (ImGui::Checkbox("Auto-orbit", &m_autoOrbit))`.

`ShowDemoWindow()` lives in `imgui_demo.cpp`, which the root
`CMakeLists.txt` compiles into the `imgui` library. Its source is the
best reference for how to write each widget: find the widget in the
running demo, then search for its label in `imgui_demo.cpp` (fetched by
CMake into the build folder's `_deps`).

### How ImGui is wired into the engine

- [kke/DebugUi.h](../../engine/include/kke/DebugUi.h) wraps ImGui's SDL3
  and Vulkan backends. Its comment states the rule: "Nothing outside this
  file touches an ImGui header", so ImGui stays swappable. Modules that
  draw panels include `<imgui.h>` only in their `.cpp` and only inside
  `renderUi()`. (One exception exists: `OrbitCameraModule::update()` reads
  `ImGui::GetIO()` to know when the mouse belongs to a panel.)
- ImGui's colours are converted to linear once at startup, because the
  swapchain is sRGB (BUGS.md BUG-021).
- `DebugUi::setVisible(false)` keeps the ImGui frame running (so every
  `renderUi()` stays valid) but draws nothing and ignores the mouse, so a
  hidden overlay cannot block clicks meant for the game or RmlUi. In
  games with a `SettingsModule`, the "Developer overlay" setting drives
  this (F1 in the RmlUi showcase). This demo has no `SettingsModule`, so
  the overlay is always visible.
- `Module::setUiVisible(false)` hides one module's panels only, without
  that module needing its own "show panel" flag (the sandbox uses it).
- `KKE_HIDE_UI=1` starts with every module's panels hidden, for clean
  screenshots.

## ImGui or RmlUi?

The repository is consistent on this:

| | Dear ImGui | RmlUi |
|---|---|---|
| Who it is for | You, the developer | The player |
| What the docs call it | "Developer debug panels" ([DEPENDENCIES.md](../../docs/DEPENDENCIES.md)); "Debug UI ... performance overlay, per-module debug panels" ([HISTORY.md](../../docs/HISTORY.md)); "`renderUi` (developer ImGui panel)" (the cpp-module skill) | "Game UI ... HTML/CSS-inspired" ([HISTORY.md](../../docs/HISTORY.md)); "RmlUi (HTML/CSS-style game UI)" ([README.md](../../README.md)) |
| In a shipping build | **Gone.** `-DKKE_SHIPPING=ON` compiles out every `renderUi()` call and forces the overlay hidden ([kke/DevTools.h](../../engine/include/kke/DevTools.h), [ANTI_CHEAT.md](../../docs/ANTI_CHEAT.md) "Shipping builds") | Always there |
| Where it lives | `renderUi()` in any module, code only | `.rml` / `.rcss` files plus a data model in C++, or `ui.load` from Lua |
| Styling | ImGui's dark style, the same for every panel | Full RCSS: gradients, animations, transitions, `dp` scaling with the window and a UI-scale setting |
| Controller | Not enabled; mouse and keyboard | Built in: D-pad / stick focus, A to press, button prompts ([INPUT.md](../../docs/INPUT.md)) |
| Iteration | Rebuild the C++ | Edit RCSS, press F5 |
| Cost | Immediate mode, cheap for dense tool panels (the sandbox's 3,000-asset list uses `ImGuiListClipper`, [OPTIMIZATION.md](../../docs/OPTIMIZATION.md)) | Retained layout; the RmlUi and ImGui budget together is 1 ms ([OPTIMIZATION.md](../../docs/OPTIMIZATION.md)) |

So the rule is:

- **Use ImGui** for anything only a developer, tester or modder with a
  developer build should see: tuning sliders, debug toggles, stats,
  inspectors, cheat and level-skip buttons, level editors, asset
  browsers. It is fast to write and cannot leak into a release.
- **Use RmlUi** for everything a player sees: title and pause menus,
  options, HUD, inventory, dialogue, lobbies, loading screens. Never put
  a feature the player needs in an ImGui panel: the shipping build will
  not have it, and it cannot be used with a controller.

The anti-cheat doc gives the reason for compiling ImGui out rather than
hiding it: "a switch is a byte in a file or in memory, and flipping bytes
is what cheat tools do." Code that is not in the binary cannot be turned
back on. For your own developer-only code outside `renderUi()`, use the
same switch: `if constexpr (kke::dev::kEnabled) { ... }`.

## Design decisions

- **Wrap `ShowDemoWindow()` instead of a hand-made widget list.** The
  header comment: it is "the canonical 'showcase everything this UI
  library can do' ... maintained by ImGui itself rather than hand-curated
  here." The comment and HISTORY.md say this was checked first:
  `imgui_demo.cpp` is compiled into the engine's `imgui` target and
  nothing defines `IMGUI_DISABLE_DEMO_WINDOWS`.
- **Add an orientation panel.** The comment in the `.cpp`: the demo
  window "is huge and dense ... without any context a viewer might not
  realize that big window *is* the showcase, not something to dismiss."
- **Keep the demo window open.** `alwaysOpen` is a fresh `true` every
  frame, so the close button has no lasting effect.
- **The module lives in the game folder, not in `engine/`.** The header:
  "this isn't a building block a real game would want; it's explicitly a
  'look what the UI library can do' tech demo."
- **A grid and orbit camera as the backdrop.** The comment in
  `main.cpp`: they "provide minimal 3D backdrop/context so the UI isn't
  floating over a flat void".
- **ImGui is compiled out of shipping builds, not hidden.** From
  `kke/DevTools.h`, quoted above: a setting can be flipped by a cheat
  tool, missing code cannot.

## Tuning

| What | Where | Value | Effect |
|---|---|---|---|
| Orientation panel position and size | `ImGuiShowcaseModule::renderUi()` | (10, 10), 340 x 100, first use only | Where the note appears on a fresh `imgui.ini`. |
| Camera start | `OrbitCameraModule` defaults ([kke/modules/OrbitCameraModule.h](../../engine/include/kke/modules/OrbitCameraModule.h)) | distance 3.5, pitch 0.5, yaw -0.6, target at the origin | Pass your own values to the constructor, as the RmlUi demo does. |
| Auto-orbit | "Camera" panel | off by default here, -180 to 180 deg/s | Spins the view. |
| Window layout | `imgui.ini` in the run folder | | Delete to reset. |
| Panels shown | `Module::setUiVisible()`, `KKE_HIDE_UI=1`, `DebugUi::setVisible()` | | Hide one module's panels, all panels at start, or the whole overlay. |

## Engine features it uses

| Feature | Header / module | Doc |
|---|---|---|
| ImGui backend (SDL3 + Vulkan), overlay visibility | [kke/DebugUi.h](../../engine/include/kke/DebugUi.h) | [DEPENDENCIES.md](../../docs/DEPENDENCIES.md) (Dear ImGui v1.91.6, MIT) |
| `renderUi()` panels, `setUiVisible()` | [kke/Module.h](../../engine/include/kke/Module.h) | [skills/cpp-module](../../skills/cpp-module/SKILL.md) |
| Developer tools compiled out of shipping builds | [kke/DevTools.h](../../engine/include/kke/DevTools.h) | [ANTI_CHEAT.md](../../docs/ANTI_CHEAT.md) |
| Reference grid | `GridModule` ([kke/modules/GridModule.h](../../engine/include/kke/modules/GridModule.h)) | |
| Orbit camera with UI mouse capture | `OrbitCameraModule` ([kke/modules/OrbitCameraModule.h](../../engine/include/kke/modules/OrbitCameraModule.h)) | |
| Pause, single step, emergency log | `DebugControlModule` ([kke/modules/DebugControlModule.h](../../engine/include/kke/modules/DebugControlModule.h)) | |
| FPS, CPU and GPU frame time | `StatsModule` ([kke/modules/StatsModule.h](../../engine/include/kke/modules/StatsModule.h)) | [OPTIMIZATION.md](../../docs/OPTIMIZATION.md) |
| Moods | `Application::setMood` | [MOODS.md](../../docs/MOODS.md) |

## Assets

- **No Synty packs** and no models or textures.
- ImGui uses its built-in font; no font files are copied.
- Shaders: `grid.vert` / `grid.frag` ([CMakeLists.txt](CMakeLists.txt)).
- Mood: `assets/moods/studio.yaml`.
- Dear ImGui itself is fetched by CMake (v1.91.6, MIT licence, keep the
  copyright notice; [docs/DEPENDENCIES.md](../../docs/DEPENDENCIES.md)).

## Make a game like this

You will rarely make a game "like" this one; you will add ImGui panels to
your own game while you build it.

1. **Start your game** from the template (`tools/new_game my_game`) or
   from the demo closest to it.
2. **Add a panel** to one of your C++ modules: override `renderUi()`,
   copy the shape of `ImGuiShowcaseModule::renderUi()`, and bind widgets
   straight to your module's members (`ImGui::SliderFloat("Jump height",
   &m_jumpHeight, 0.5f, 5.0f)`). Include `<imgui.h>` in the `.cpp` only.
3. **Find the widget** you need in the running `imgui_demo`, then copy its
   code from `imgui_demo.cpp`.
4. **Add the engine's panels** you want: `StatsModule` for frame times,
   `DebugControlModule` for pause and the emergency log.
5. **Give players a way to hide them** if the game has a settings menu:
   the "Developer overlay" setting in `SettingsModule` does it (see the
   RmlUi showcase's F1).
6. **Keep developer-only logic behind `kke::dev::kEnabled`** and read
   debug environment variables with `kke::dev::env` / `kke::dev::flag`,
   so a shipping build drops them.
7. **Build the player's UI in RmlUi**, starting from
   [../rmlui_demo/README.md](../rmlui_demo/README.md).

Pitfalls:

- Anything in an ImGui panel is missing from a shipping build. Test a
  `-DKKE_SHIPPING=ON` build before you rely on a panel.
- Two windows with the same title are the same window. Use `##suffix`
  in labels to make IDs unique (ImGui's own "ID Stack" tool in the demo
  window shows why).
- A stale `imgui.ini` overrides the positions you set with
  `ImGuiCond_FirstUseEver`.
- If your own code reads the mouse, check
  `ImGui::GetIO().WantCaptureMouse` (and `Application::uiCapturesMouse()`
  for RmlUi) like `OrbitCameraModule` does, or dragging a slider will
  also move your game.

## Files

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | Creates the application, sets the mood, adds the grid, camera, showcase, debug-control and stats modules. |
| [ImGuiShowcaseModule.h](ImGuiShowcaseModule.h) | The module declaration and why it wraps `ShowDemoWindow()`. |
| [ImGuiShowcaseModule.cpp](ImGuiShowcaseModule.cpp) | The orientation panel and the `ShowDemoWindow()` call. |
| [CMakeLists.txt](CMakeLists.txt) | The executable, the manifest copy, the grid shaders. |
| [game.json](game.json) | Marketplace manifest. |
