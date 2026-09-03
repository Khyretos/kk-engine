# Kreative Kompas Engine (KKE)

A bare-bones, modular Vulkan game engine built from "lego pieces" — each
library does one job, and every gameplay/rendering system beyond the core
frame loop is a **Module** that plugs into an `Application`, not a
special case baked into the engine.

> **This engine is AI-coded.** Same disclosure as my earlier `3dco+`
> project: the code here was written by Claude (Anthropic) working
> directly in this repo, iterating with me over a series of Vulkan/CMake
> build-and-run cycles, not hand-written line by line. Read it, question
> it, and treat it the way you'd treat any other codebase you didn't
> personally write every line of — that's true regardless of who or what
> wrote it, but it's especially worth saying plainly here.

**Icon / branding:** drop your icon file into `assets/` (see
`assets/README.md`). Nothing in the build wires it up automatically yet —
that's platform-specific (window/taskbar icon via SDL3 on Linux/Windows,
an `.icns` + bundle `Info.plist` on macOS) and hasn't been done. Flagging
it as a known gap rather than silently skipping it.

**For AI agents working on this codebase** (this one, a fork, a future
model): read `AI_GUIDE.md` first. It's a short, model-agnostic orientation
doc — this README is where the actual depth lives, but the guide tells
you the non-negotiable rules (verify before claiming done, small slices,
update the Roadmap below, no closed-source dependencies) before you start.

**This is built iteratively, in small verified slices, across many
sessions — not in one pass.** Every increment below was built, actually
compiled, actually run (headlessly against `lavapipe`, screenshotted or
log-inspected), before being called done. The Roadmap section near the
bottom is the living, honest list of what that process hasn't reached
yet — treat it as this project's memory, not aspirational marketing.

## What's actually in this milestone

- A window (SDL3) with a Vulkan swapchain, depth buffer, and render pass
- A **module system** (`kke::Module` + `kke::Application`) that drives
  everything else — see "Architecture" below
- A spinning cube (`CubeModule`) — the original milestone, ported into
  the module shape
- A depth-correct reference grid (`GridModule`) so you have a fixed sense
  of scale and perspective in the scene
- Mouse-driven camera control (`OrbitCameraModule`) — left-drag to orbit,
  right-drag to pan, scroll to zoom, plus an auto-orbit checkbox
- A GPU compute-driven particle system (`ParticleModule`) — 20,000
  particles simulated entirely on the GPU, no CPU-side particle array
- A performance overlay (`StatsModule`) — FPS, CPU frame time, and *GPU*
  frame time (via Vulkan timestamp queries), each with a rolling graph
- A cross-module communication system: typed dependencies (`getModule<T>()`),
  capability-based discovery (`findCapability<T>()`), and a fixed-timestep
  tick for deterministic simulation — demonstrated end-to-end by
  `DestructionModule` (seed-based, tick-deterministic) and `NetworkModule`
  (discovers it via capability, with zero concrete-type coupling)
- A real test suite (`tests/`, GoogleTest, 25 tests passing) with
  measured coverage — 99.1% on pure-logic code, 94.0% across the whole
  engine including Vulkan — and a CI pipeline
  (`.github/workflows/ci.yml`) enforcing an 85% floor on every push. See
  "Test suite & coverage."
- Async, non-blocking, formatted/colored logging via spdlog (a
  representative subset of call sites migrated so far — see "Logging"),
  and a developer-declared hardware-requirements check (`game.json`'s
  `requirements` section vs. real queried hardware, warn-only, verified
  both passing and failing on real output — see "Estimated hardware
  requirements").
- The engine/game directory split is real: `games/kke_demo_game/` is a
  self-contained "game folder" with its own `game.json` manifest,
  `kke::MarketplaceIndex` (verified by `tests/marketplace_test.cpp`,
  which passes) scans and imports such folders idempotently, and
  `MarketplaceUiModule` renders the result as a real, on-screen RmlUi
  card. Non-interactive, scans once at startup, no sandboxing — see
  "Game folder convention & marketplace" for exactly what this does and
  does not mean yet.
- Dear ImGui wired in for all of the above
- **RmlUi with a real Vulkan render backend, including textures and text**
  (`UiModule` + `RmlVulkanRenderInterface`) — RmlUi's actual layout engine
  computing real positions/sizes and shaping real text via FreeType,
  compiled into this engine's own `Buffer`/`Pipeline` primitives (plus a
  VMA-backed image/sampler/descriptor path for glyph atlases), drawn via
  genuine Vulkan draw calls. Verified by an on-screen test document with
  actual anti-aliased, word-wrapped text in a bundled default font over
  alpha-blended colored panels. Image-file textures (`<img>`,
  `background-image`) are still stubbed — that needs stb_image, tracked
  separately in the Roadmap.

Verified by an actual build-and-run pass, including headlessly against
Mesa's `lavapipe` software Vulkan device under Xvfb — screenshots, not
just "it compiles."

## Stack

| Piece        | Library                                      | Job                                   |
|--------------|-----------------------------------------------|----------------------------------------|
| Windowing/input | [SDL3](https://github.com/libsdl-org/SDL)  | window, events, Vulkan surface creation |
| Vulkan loader | [volk](https://github.com/zeux/volk)        | loads Vulkan function pointers, no libvulkan linkage needed |
| GPU memory   | [VMA](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | buffer/image allocation |
| Math         | [GLM](https://github.com/g-truc/glm)        | vectors, matrices, transforms          |
| Debug UI     | [Dear ImGui](https://github.com/ocornut/imgui) v1.91.6 | performance overlay, per-module debug panels |
| Game UI      | [RmlUi](https://github.com/mikke89/RmlUi) 6.3 + FreeType | HTML/CSS-inspired UI; real Vulkan rendering incl. text/glyph textures — image-file textures (`<img>`) still stubbed |
| Default fonts | [Noto Sans + Noto Color Emoji](https://github.com/googlei18n/noto-fonts) (OFL-1.1), bundled at `assets/fonts/` | fallback every game gets even without loading its own, including verified-working color emoji; see "Default fonts" section |
| Data/manifests | [nlohmann/json](https://github.com/nlohmann/json) 3.11.3 | `game.json` manifests, the marketplace index — see "Game folder convention & marketplace" |
| Testing | [GoogleTest](https://github.com/google/googletest) 1.15.2 | unit tests for pure-logic code — see "Test suite & coverage" |
| Logging | [spdlog](https://github.com/gabime/spdlog) 1.14.1 | async, non-blocking, colored, formatted logging — see "Logging" |
| Images       | [stb](https://github.com/nothings/stb)      | fetched, not consumed yet — no texture loading until there's a texture |
| Scripting    | Lua 5.4                                      | fetched behind `ENGINE_ENABLE_LUA` (OFF by default), not consumed yet |
| Particles    | custom (GPU compute, see below)              | see "Why a custom particle system" |

All dependencies are fetched from source via CMake `FetchContent` — same
version on every platform, no system package hunting.

## Build

Requires: CMake ≥ 3.24, a C++20 compiler, the Vulkan SDK/loader headers
(`libvulkan-dev` on Linux), and either `glslc` or `glslangValidator` on
`PATH` for shader compilation.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/bin/kke_demo
```

Run it from `build/bin/` (or copy the `shaders/` and `assets/` folders
next to the executable) — shader `.spv` files and the bundled font are
placed next to the binary at build time, and the app currently looks for
them via a relative path (see "Known simplifications").

### Headless smoke test (what we used to verify this milestone)

```bash
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 ./build/bin/kke_demo
```

Works against Mesa's `lavapipe` software Vulkan driver, no GPU required
— useful for CI and for the kind of sandboxed verification this project
has been built with throughout.

## Architecture: the module system

Everything that isn't core frame plumbing (window, swapchain, sync
objects, command buffers) is a **`kke::Module`**:

```cpp
class Module {
public:
    virtual const char* name() const = 0;
    virtual void init(Application& app) {}          // create GPU resources
    virtual void update(const UpdateContext& ctx) {} // CPU simulation
    virtual void compute(VkCommandBuffer cmd) {}     // compute dispatches, pre-render-pass
    virtual void render(const RenderContext& ctx) {} // draw calls, render pass active
    virtual void renderUi() {}                       // ImGui panels
    virtual void shutdown() {}                       // destroy GPU resources
};
```

`kke::Application` owns the window, the `Renderer`, the debug UI, and a
list of `Module`s, and drives them all through one loop:

```
poll events
for each module: update(dt)
build ImGui panels (renderUi)
if renderer.beginFrame():           // acquire swapchain image, start recording
    for each module: compute(cmd)   // compute dispatches happen here, before...
    renderer.beginRenderPass()      // ...the render pass starts
    for each module: render(ctx)    // draw calls, render pass is active
    debug UI draws on top
    renderer.endFrame()             // end render pass, submit, present
```

A module that doesn't render anything — a physics step, a networking
client polling sockets — just never overrides `render()`/`compute()`.
Partial implementations are the normal case, not a workaround.

`main.cpp` is deliberately almost empty:

```cpp
kke::Application app("Kreative Kompas Engine - Demo", 1280, 720);
app.addModule<kke_demo::CubeModule>();
app.addModule<kke::GridModule>();
app.addModule<kke::ParticleModule>(20000);
app.addModule<kke::StatsModule>();
app.run();
```

That's the "simple game loop / demo" in its entirety — a real game's
`main.cpp` should look almost exactly like this, with your own modules
in place of `CubeModule`.

## Where the rendering code actually lives

| If you want to see... | Look at |
|---|---|
| The frame lifecycle itself (acquire/record/submit/present, GPU timing) | `engine/src/Renderer.cpp` |
| How a render pass, framebuffers, and the depth buffer are set up | `engine/src/SwapChain.cpp` |
| How a graphics pipeline is configured (cull mode, blend, depth, push constants) | `engine/include/kke/Pipeline.h`'s `PipelineConfig` struct |
| The **simplest possible** draw — no vertex buffer, no mesh, just a pipeline and a `vkCmdDraw` | `engine/src/modules/GridModule.cpp` |
| A conventional draw — vertex/index buffer, push-constant MVP matrix | `games/kke_demo_game/CubeModule.cpp` |
| A compute shader feeding a graphics pipeline through a shared buffer | `engine/src/modules/ParticleModule.cpp` |
| GPU-side profiling (Vulkan timestamp queries) | `Renderer::createQueryPools`/`beginFrame` in `engine/src/Renderer.cpp` |

The actual "how do I draw something" recipe, concretely:

1. In your module's `init(Application& app)`, build a `kke::Pipeline` with
   a `PipelineConfig` describing your fixed-function state, pointing at
   compiled `.spv` shaders.
2. In `render(const RenderContext& ctx)`, call `pipeline.bind(ctx.cmd)`,
   push whatever constants your shader needs (`ctx.view`/`ctx.proj`/
   `ctx.cameraPos` are already computed for you), bind any vertex/index
   buffers or descriptor sets, and issue your `vkCmdDraw*` call.
3. `Application` calls this for every module, every frame, inside the one
   shared render pass — you never open or close a render pass yourself.

## Adding a module

This is the concrete "how do I extend the engine" answer for things like
a **3D destruction sim** or a **networking client**.

1. **Decide what it touches.** Pure simulation (networking, destruction
   physics) only needs `update()`. Anything that draws needs `init()` (to
   build pipelines/buffers) and `render()`. Anything GPU-compute-driven
   (destruction fragments simulated on the GPU, like the particle system)
   needs `compute()` too.
2. **Write the class:**

   ```cpp
   class DestructionModule : public kke::Module {
   public:
       const char* name() const override { return "Destruction"; }
       void init(kke::Application& app) override {
           // build fragment mesh/buffers, a Pipeline for drawing them
       }
       void update(const kke::UpdateContext& ctx) override {
           // CPU-side: which objects fractured this frame, impulse data, etc.
       }
       void compute(VkCommandBuffer cmd) override {
           // GPU-side fragment simulation, same shape as ParticleModule
       }
       void render(const kke::RenderContext& ctx) override {
           // draw the fragments
       }
   };
   ```

   ```cpp
   class DestructionModule : public kke::Module, public kke::INetworkReplicable {
   public:
       const char* name() const override { return "Destruction"; }
       void init(kke::Application& app) override {
           // build fragment mesh/buffers, a Pipeline for drawing them
       }
       void fixedUpdate(const kke::FixedUpdateContext& ctx) override {
           // deterministic — tick-indexed, not frame-rate-dependent
       }
       void render(const kke::RenderContext& ctx) override {
           // draw the fragments
       }
       // kke::INetworkReplicable — see below
       std::string replicationChannelName() const override { return "destruction.demo"; }
       std::vector<uint8_t> serializeReplicatedState() override { /* seed + trigger tick, not geometry */ }
       void deserializeReplicatedState(const std::vector<uint8_t>& data) override { /* ... */ }
   };
   ```

   ```cpp
   class NetworkModule : public kke::Module {
   public:
       const char* name() const override { return "Network"; }
       void init(kke::Application& app) override {
           // discovers replicable modules without knowing their concrete types
           for (auto* r : app.findCapability<kke::INetworkReplicable>()) { /* ... */ }
       }
       void update(const kke::UpdateContext& ctx) override {
           // poll socket, apply incoming state, send outgoing state
       }
       // no render()/compute() overrides needed at all
   };
   ```

3. **Register it** in `main.cpp`: `app.addModule<DestructionModule>();`

This is a real, working example, not a sketch — `games/kke_demo_game/DestructionModule.*` and
`games/kke_demo_game/NetworkModule.*` in this repo are exactly the two classes above, and
`main.cpp` adds both. Comment out the `NetworkModule` line and
`DestructionModule` behaves identically — it never references
`NetworkModule` or even knows the concept "network" exists.

Copy `games/kke_demo_game/CubeModule.h`/`.cpp` as the template for a
straightforward render-only module,
`games/kke_demo_game/DestructionModule.h`/`.cpp` for a
capability-implementing one, or `engine/src/modules/ParticleModule.cpp`
for a compute-driven one.

## Game folder convention & marketplace

The engine/game split is a real directory boundary, not just a coding
convention: `engine/` is the "lego pieces" (this repo, forked as-is),
and everything that makes a specific game a specific game lives in its
own folder under `games/`. `games/kke_demo_game/` is that pattern's own
first example — it's not special-cased by the build; it's just the one
game folder `CMakeLists.txt` currently builds (see the Roadmap for
building/discovering more than one).

**A game folder is:** a directory containing a `game.json` manifest plus
whatever source/assets that game needs. The manifest is what makes the
folder recognizable and importable — everything else about the folder's
internal layout is up to the game.

**`game.json` schema** (see `kke::GameManifest` /
`engine/include/kke/GameManifest.h` for the authoritative parser):

| Field | Required | Meaning |
|---|---|---|
| `id` | **yes** | Unique dedup key, e.g. `"com.yourname.gamename"`. This is what a marketplace uses to recognize "this is the same game," not the folder name or path — see below. |
| `title` | **yes** | Human-readable name. |
| `description` | no | Free text. |
| `version` | no | Defaults to `"0.1.0"`. |
| `icon` | no | Path relative to the game folder. |
| `banner` | no | Path relative to the game folder. |
| `tags` | no | Array of strings, for marketplace filtering/search once that exists. |
| `engine_version` | no | Informational only right now — see Roadmap on actually checking compatibility. |
| `modules` | no | Array of module names this game uses/declares — informational/documentation today, not yet used to auto-resolve dependencies against what the engine build actually provides. |
| `requirements` | no | Developer-declared hardware requirements — see "Estimated hardware requirements" below. Not automatically inferred from the game's code. |

## Estimated hardware requirements

You raised this as "runs on anything, as long as it can render at all —
think 'runs on a toaster'" with a genuine question of whether estimating
requirements from a game is even calculable. Being direct about the
honest answer before describing what's built: **fully automatic
inference of hardware requirements from arbitrary game code isn't
realistic.** There's no static analysis of a `Module`'s `render()` that
tells you real-world VRAM or performance needs without actually running
and profiling it — draw call counts, texture memory, and shader
complexity all depend on runtime data (how many particles are alive
right now, what resolution the window is, what's actually on screen),
not just what code exists. Claiming otherwise would be presenting a
guess as a measurement.

**The realistic version, built and verified**: `game.json` gets an
optional `requirements` object, declared by the *developer* — the same
way a Steam store page's "minimum/recommended specs" are written by the
publisher, not computed from the executable:

```json
"requirements": {
    "minimum": { "vram_mb": 128, "vulkan_api_version": "1.0" },
    "recommended": {
        "vram_mb": 512,
        "vulkan_api_version": "1.2",
        "required_device_features": ["largePoints"]
    },
    "notes": "Free text explaining the numbers, e.g. what actually drives them."
}
```

At startup, `kke::checkHardwareRequirements()`
(`engine/include/kke/HardwareCheck.h`) compares this against the
**actual running hardware** — real Vulkan API version, real available
VRAM (via VMA's heap budget query), a small set of queryable device
features — and logs warnings for anything unmet. Verified both ways,
not just the happy path: temporarily set the demo's own recommended
VRAM to an absurd 999999 MB and confirmed the exact expected warning
fired —

```
[HardwareCheck][KKE Engine Demo][warning]: Recommended requirements ask for
~999999 MB VRAM; this device reports approximately 3198 MB available.
```

— then reverted it and confirmed the passing case logs cleanly instead:

```
[HardwareCheck][KKE Engine Demo][info]: Hardware check: this device meets
'KKE Engine Demo' recommended requirements.
```

(3198 MB is lavapipe's real reported budget in this environment — a
software Vulkan device sharing host RAM, not a discrete GPU's dedicated
VRAM, which is an honest thing to know about the number rather than
treat it as universally precise.)

**This only ever warns — it never blocks.** Exactly per your framing:
if someone wants to run a game below its stated minimum and get 2 FPS
or a crash, that's their call, not the engine's to make for them. A
game that wants to actually refuse to launch below its minimum can do
that itself with `HardwareCheckResult::meetsMinimum` — that's the
game's decision, not something this function imposes.

**Recognized `required_device_features` values**: currently just
`"largePoints"` (used by `ParticleModule`'s point-sprite sizing). An
unrecognized feature name is treated as *unmet* (with a warning saying
so), not silently ignored — a manifest declaring something the engine
can't verify shouldn't silently pass. Extend the recognized set in
`HardwareCheck.cpp` alongside whatever new capability `VulkanDevice`
learns to query.

**Importing into a marketplace** is exactly "copy the game folder into
the marketplace's directory." `kke::MarketplaceIndex`
(`engine/include/kke/MarketplaceIndex.h`) is the scanner: point
`scanDirectory()` at a folder full of game folders, and it imports every
one with a valid `game.json`, keyed by that manifest's `id` — **not** by
folder name. This is specifically what makes "importing a game twice
doesn't get stuck" true: copy the same game folder in again, under any
name, and `MarketplaceIndex` recognizes the `id` and updates the
existing entry in place instead of duplicating it. A folder without a
`game.json` is silently skipped (not every subfolder needs to be a
game); a folder with a broken one is skipped with a logged warning
rather than aborting the whole scan. `MarketplaceIndex::save()`/`load()`
persist the list *and its order* to a `marketplace.json` file — "the
marketplace's own ordering" the request asked for, independent of
whatever order the filesystem happens to return subdirectories in.

All of this is verified by an actual test
(`tests/marketplace_test.cpp`, run via `./build/bin/kke_marketplace_test`)
that creates real folders on disk — including a deliberate same-id
duplicate under a different folder name, a broken manifest, and a
non-game folder — and asserts on the real scan result. It passes.

**What this slice does NOT do yet** — and this matters more than
anything else in this section:

- ~~There is no marketplace UI~~ — **done, minimally.**
  `MarketplaceUiModule` (`engine/include/kke/modules/MarketplaceUiModule.h`)
  depends on `UiModule`, scans a marketplace directory with
  `MarketplaceIndex`, and renders each game as a real RmlUi card — title,
  id, description, tags — verified on screen with the demo's own
  `game.json`. Every manifest field is passed through
  `kke::escapeRmlText` (`engine/include/kke/RmlTextSafety.h`) before
  being placed in the generated markup, since a game folder's manifest
  is exactly the "not this engine's own code" content that function
  exists for. **This is non-interactive** (no click-to-launch — RmlUi
  has no input wiring yet) and **scans once at startup** (no live
  refresh). Building this also caught a real bug worth naming: the
  first version escaped quotes as the named XML entities `&quot;`/`&apos;`,
  and RmlUi's parser rendered `&apos;` as five literal characters instead
  of decoding it — fixed by switching to numeric character references
  (`&#34;`/`&#39;`), which are more universally supported. Exactly the
  kind of thing a real test would catch mechanically instead of needing
  a screenshot — see "Test suite" below.
- **There is no compatibility checking.** `engine_version` and `modules`
  are recorded but never validated against what the running engine
  build actually provides — a game folder declaring modules the engine
  doesn't have will currently just fail at `add_subdirectory`/link time
  with a normal CMake/linker error, not a friendly marketplace message.
- **There is no dynamic multi-game build.** The top-level `CMakeLists.txt`
  hardcodes one `add_subdirectory(games/kke_demo_game)`. Building a
  second game folder today means adding a second explicit
  `add_subdirectory` line by hand — `MarketplaceIndex` scanning a
  directory and CMake actually building whatever it finds are two
  different pieces of work, and only the first one exists.
- **This is the big one: there is no sandbox, at all.** A "game folder"
  in this engine is native C++ that gets compiled directly into the
  engine binary. It has full, unrestricted access to the machine it
  runs on — the filesystem, the network, everything — the same as any
  C++ you'd write yourself, because it *is* C++ you'd write yourself.
  This is a fundamentally different trust model from Roblox, where
  every game is sandboxed Luau script with no native code execution at
  all. **A marketplace where anyone can upload a "game folder" and have
  it compiled and run on someone else's machine, in this engine's
  current state, is a way to distribute arbitrary native code with no
  isolation whatsoever.** That is not a small caveat to fix later; it's
  the difference between "a marketplace" and "a way to run untrusted
  code." The Roadmap's Lua-scripting item is the actual path to
  something Roblox-comparable — a marketplace built on *scripted* game
  folders (sandboxed Lua, no native compilation of third-party code)
  is a plausible, buildable goal; a marketplace built on compiling
  arbitrary third-party C++ is not, without a much larger
  sandboxing/isolation effort (a separate process at minimum, likely a
  restricted execution environment, closer to how browsers isolate web
  pages than how this engine currently works). Stating this plainly now,
  before more is built on top of the current native-code assumption,
  rather than discovering it after a marketplace exists.

## Cross-module communication

Two separate mechanisms, deliberately not merged into one, because they
answer different questions:

**`Application::getModule<T>()`** — "give me the one module of concrete
type `T`, if it was added." For when a module genuinely can't function
without a specific other one (a gameplay module that needs the physics
module's results this frame). Pair it with declaring the dependency:

```cpp
std::vector<kke::ModuleDependency> dependencies() const override {
    return { { std::type_index(typeid(PhysicsModule)), /*required=*/true, "reads collision results" } };
}
```

`Application::run()` topologically sorts `init()` order from every
module's declared dependencies (Kahn's algorithm) and throws a clear
error naming the missing module if a *required* dependency isn't present
— you find out at startup, not three frames into a null-pointer crash.
`fixedUpdate`/`update`/`render`/etc. all run in that same dependency
order every frame, too.

**`Application::findCapability<Capability>()`** — "give me every module,
whatever its concrete type, that implements interface `Capability`." This
is the "recognize each other if both present, work fine standalone if
not" mechanism you asked for. Neither side declares a `ModuleDependency`
on the other, and neither side's header includes the other's — they only
share a small interface header (`kke/Capabilities.h`).

This repo's worked example is exactly your destruction/networking case:

- `kke::INetworkReplicable` (`engine/include/kke/Capabilities.h`) is the
  shared interface: `replicationChannelName()`,
  `serializeReplicatedState()`, `deserializeReplicatedState()`.
- `DestructionModule` implements it. It "shatters" into fragments whose
  transforms are a **pure function** of `(seed, fragment index, elapsed
  ticks since trigger)` — not integrated frame-by-frame, not stored
  per-fragment — so its entire replicated state is 17 bytes: an 8-byte
  seed, a 1-byte triggered flag, an 8-byte trigger tick. That's true
  whether it's idle or mid-explosion with fragments flying everywhere —
  verified by actually triggering it and watching the reported payload
  size stay at 17 bytes.
- `NetworkModule` never includes `DestructionModule.h`. In `init()` it
  calls `app.findCapability<kke::INetworkReplicable>()`, and — if and
  only if a `DestructionModule` (or anything else implementing the
  interface) happens to be in the same `Application` — gets a pointer to
  it back and can call `serializeReplicatedState()` on it every second.
  Remove the `app.addModule<NetworkModule>()` line from `main.cpp` and
  `DestructionModule` runs exactly the same; it just never gets asked.

`NetworkModule` here is honestly a stub — it measures payload sizes and
shows them in an ImGui panel, but never opens a socket. That's the right
scope for demonstrating discovery; a real transport is a separate, larger
piece of work (which peer is authoritative, reconciliation on conflicting
edits, packet loss/reordering) that deserves its own design pass rather
than being bolted onto this example.

**Why the fixed-timestep tick matters for this:** `FixedUpdateContext::tickIndex`
is a monotonically increasing counter driven by `Application`'s fixed-rate
loop (60 Hz by default), not wall-clock time. Two machines running the same
seed and reacting to the same trigger tick compute the *same* fragment
positions at the same tick, regardless of each machine's actual frame
rate — which is what makes sending a tick number (instead of a timestamp,
instead of positions) a coherent thing to replicate at all. Physics and
animation should generally live in `fixedUpdate()` for the same reason,
even before networking enters the picture: reproducible replays,
consistent behavior independent of display frame rate, and a shared
"which simulation step are we on" number that a networking module can
reason about.

**What's still not solved:** a real transport (see above), and anything
resembling authority/reconciliation for two peers whose `INetworkReplicable`
state disagrees. `deserializeReplicatedState()` exists on the interface
and `DestructionModule` implements it correctly, but nothing in this repo
calls it yet — that's the natural next piece once an actual `NetworkModule`
opens a socket.

## Why a custom particle system (not Effekseer/SparkEngine/etc.)

You asked me to look at Effekseer, SparkEngine, Momentous, MonoGame.Particles,
Three.proton, and Sparkle, and pick the most robust — robustness over ease
of use. Here's the reasoning:

- **Effekseer** and **SparkEngine** are *authoring tools* (Effekseer ships
  an editor and exports a proprietary effect format; SparkEngine is
  similarly a content pipeline) — they solve "let an artist build an
  effect," not "give this Vulkan engine a particle simulation primitive."
  Adopting either means adopting their file format and (for Effekseer) a
  separate runtime library with its own renderer abstraction to bridge
  into ours.
- **MonoGame.Particles** and **Three.proton** are tied to MonoGame (C#/XNA-style)
  and Three.js (WebGL) respectively — wrong language/API entirely for a
  native Vulkan C++ engine.
- **Momentous** (Fabian Giesen's DirectX Compute particle system) is the
  closest in spirit — GPU-compute-driven, no CPU particle array — but
  it's a demo/reference implementation for DirectX, with community OpenGL
  ports; there's no maintained Vulkan port, and adapting DX-compute-shader
  idioms through an OpenGL port to Vulkan's descriptor/barrier model would
  likely mean rewriting most of it anyway.
- **Sparkle** (C++14, OpenGL + GLSL compute) is architecturally the right
  shape — GPU simulation, compute shaders — but again OpenGL, not Vulkan;
  same rewrite cost as Momentous.

Given that every option requiring the least adaptation still meant either
adopting a foreign asset pipeline or rewriting an OpenGL/DirectX compute
system's Vulkan descriptor/barrier/pipeline plumbing from scratch, the
most *robust* fit — matching this engine's actual synchronization model
(explicit barriers, `volk`-loaded function pointers, VMA-managed buffers)
rather than adapting someone else's — was to write the compute→graphics
particle pipeline directly against this engine's own primitives. That's
what `ParticleModule` is: a storage buffer holding particle state, a
compute shader (`shaders/particle.comp`) integrating and respawning
particles in place every frame with no CPU readback, and a graphics
pipeline (`particle.vert`/`.frag`) reading that same buffer to draw point
sprites. It's the same architectural shape as Momentous/Sparkle, just
Vulkan-native instead of ported.

This isn't a closed decision — if a Vulkan-native, GPU-driven FOSS
particle library surfaces later, swapping it in only touches
`ParticleModule`; nothing else in the engine knows particles exist.

## Logging

Uses [spdlog](https://github.com/gabime/spdlog) (MIT licensed) — **not**
plain `std::cout`/`std::cerr`, which is what most of this engine used
until this was added. `Application`'s constructor calls `kke::log::init()`
automatically (using the title you pass it as the game name), and its
destructor calls `kke::log::shutdown()`; a module just does:

```cpp
kke::log::get("MyModuleName")->info("something happened: {}", value);
kke::log::get("MyModuleName")->warn("something's off: {}", reason);
```

**Format**: `[timestamp][engine][module][game name][level]: message`,
with the level and message colored by severity in a real terminal (info
green, warning yellow, error red — spdlog's own sane default, correctly
suppressed automatically when output is piped to a file rather than a
TTY, verified with both a piped run and a `script`-wrapped pseudo-TTY
run rather than assumed).

**Non-blocking by design**: every logger shares one background thread
and one bounded queue (`spdlog::create_async_nb`, `async_overflow_
policy::overrun_oldest`). A log call pushes a formatted message onto
that queue and returns immediately — the actual stdout write happens on
the background thread. Under sustained log spam the queue drops the
*oldest* buffered message rather than ever blocking the calling thread,
so a burst of logging can never stall a frame. This was a specific,
deliberate design goal, not an incidental spdlog feature we happened to
use.

**Migration status, honestly**: only a representative few call sites
(`VulkanDevice`, `MarketplaceIndex`) have been migrated from the old
`std::cout`/`std::cerr` calls scattered through the rest of the engine —
enough to prove the format, the async behavior, and the color output
all actually work, not a claim that every log line has been converted.
Migrating the rest is mechanical but real work — tracked in the Roadmap.

## Default fonts / font fallback chain

Every game gets two fonts loaded automatically by `UiModule` before any
document loads, both bundled directly in this repo at `assets/fonts/`
(OFL-1.1 licensed — see `assets/fonts/NOTO-LICENSE.txt`), not fetched at
build time, so a fresh checkout always has working defaults with no
network dependency:

- **`NotoSans-Regular.ttf`** — the primary text font. Noto specifically
  because its whole design goal ("no tofu") is broad Unicode coverage
  across many scripts, a reasonable universal fallback rather than a
  Latin-only choice.
- **`NotoColorEmoji.ttf`** — loaded as a *fallback face*
  (`Rml::LoadFontFace(path, /*fallback_face=*/true)`), so RmlUi reaches
  for it automatically for any character missing from Noto Sans. No
  `font-family` fiddling needed in a document to get emoji — they just
  work alongside normal text.

**On emoji, verified rather than assumed:** color emoji glyphs use
bitmap strike formats (CBDT/CBLC), and it genuinely wasn't clear whether
RmlUi's default font engine would render those correctly or just tint
them like regular glyph coverage masks — so before bundling anything,
this was actually tested: loaded `NotoColorEmoji.ttf`, put 🎮 and 😀 in a
real document, rendered it, and sampled the actual output pixels with
Python/PIL rather than trusting a screenshot glance. The result was
unambiguous — genuine saturated colors came back (e.g. `(255, 220, 34)`,
a real yellow, not a grayscale value tinted by the text color) — so
color emoji is bundled as a real, working feature, not a "should work in
theory" claim. **One honest tradeoff**: the file is ~10.8 MB (vs. ~0.5 MB
for Noto Sans) because it embeds bitmap strikes at multiple resolutions
— a smaller/subset variant is a reasonable future optimization, tracked
in the Roadmap rather than done preemptively.

**A game that wants its own typography** just calls `Rml::LoadFontFace()`
again with its own font file and family name — RmlUi keeps every loaded
face registered simultaneously, and CSS `font-family` on any element
picks between them normally. Nothing needs to be disabled or overridden;
the bundled fonts are a floor, not a lock-in.

## Where the RmlUi Vulkan backend lives

`RmlVulkanRenderInterface` (`engine/include/kke/RmlVulkanRenderInterface.h`
+ `.cpp`) is the whole thing: geometry compiles into the engine's own
`Buffer` objects, textures (currently: glyph atlases only) get a real
VMA-backed `VkImage` + `VkSampler` + descriptor set, and untextured draws
share the exact same pipeline via a persistent 1×1 white texture rather
than a separate shader variant. `shaders/rml_ui.vert`/`.frag` do the
actual pixel-to-NDC transform and texture-times-vertex-color shading.

## Test suite & coverage

**Two different kinds of code get verified two different ways, on
purpose** — applying one blanket coverage number to both would either be
trivially gameable or force mocking the entire Vulkan API before writing
a single new feature:

1. **Pure logic** — `GameManifest` parsing, `MarketplaceIndex` dedup,
   `escapeRmlText`. No GPU, no window, deterministic. Real unit tests
   (`tests/`, GoogleTest), an **enforced 85% line-coverage floor**, and
   CI fails the build below it.
2. **Vulkan/GPU-touching code** — `Renderer`, `Pipeline`, every module's
   `render()`. Verified the way this whole project has been built from
   the start: actually compile it, actually run it headlessly against
   Mesa's `lavapipe` software Vulkan device, actually look at the
   resulting pixels or log output. That's a legitimate, different
   strategy — not a lesser one — and it's what CI's headless smoke-test
   step does on every push.

**Real measured numbers, not a claimed target** — from an actual run
of the pipeline below, not asserted:

- Pure-logic files, unit tests only: **99.1%** (114/115 lines,
  `tests/test_*.cpp`, 25 tests, all passing). The one "uncovered" line
  is a closing brace — a standard gcov line-attribution artifact, not a
  missed code path; every real statement is covered.
- Whole engine (`engine/src/`, including every Vulkan file), combining
  the unit tests *and* an actual headless run of `kke_demo` (with mouse
  interaction — the destruction trigger button, a camera drag — exactly
  like the manual verification this project has done throughout):
  **94.0%** (1505/1601 lines). Per-file: most render/logic files hit
  100% (`Pipeline.cpp`, `Mesh.cpp`, `GameManifest.cpp`,
  `MarketplaceIndex.cpp`, `ParticleModule.cpp`, others); the lower end
  (`Window.cpp` 75.9%, `Buffer.cpp` 84.6%, `UiModule.cpp` 82.6%) is
  mostly error-handling branches and paths that need a real GPU/resize
  event/validation-layer warning to trigger, not gaps in what's been
  exercised on the happy path.

These numbers will drift as code changes — CI is the source of truth
going forward, not this paragraph. Re-run the pipeline below to get a
current number rather than trusting this one indefinitely.

### Running it yourself

```bash
# Unit tests (fast, no GPU needed)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
./build/bin/kke_tests

# Coverage (separate build dir — don't mix instrumented and
# non-instrumented objects, it corrupts gcov data)
cmake -B build-coverage -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKKE_ENABLE_COVERAGE=ON
cmake --build build-coverage -j
./build-coverage/bin/kke_tests
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 timeout 8 ./build-coverage/bin/kke_demo

lcov --capture --directory build-coverage --output-file coverage_full.info \
    --ignore-errors mismatch,unused,negative,inconsistent,gcov,source
lcov --extract coverage_full.info "*/engine/src/*" --output-file coverage_engine.info
lcov --summary coverage_engine.info
genhtml coverage_engine.info --output-directory coverage_html  # open coverage_html/index.html
```

**One tooling note worth recording**: this lcov version's `--list`
command showed per-file percentages wildly inconsistent with its own
`--summary` output on the exact same `.info` file (a few percent vs.
99%+) — a real anomaly, not a typo. Cross-checked against `genhtml`'s
independent recomputation (which agreed with `--summary` exactly) and
the raw per-line hit records in the `.info` file itself before trusting
any number here. Use `--summary`/`genhtml`, not `--list`, in this setup.

### CI pipeline

`.github/workflows/ci.yml` runs on every push/PR: plain build → unit
tests → headless smoke test (does `kke_demo` run 8 seconds under Xvfb +
lavapipe without crashing or logging a fatal error) → coverage-
instrumented build → unit tests again → headless demo run again (so
GPU code paths that only execute at runtime count) → `lcov`/`genhtml` →
**fail the build if line coverage drops below 85%**. Not run in this
sandboxed session (no GitHub Actions runner here) — validated by running
every individual command above by hand and confirming the exact parsing
logic CI uses against real output before writing it into the workflow
file, but the workflow itself hasn't executed in GitHub's infrastructure
yet. Worth confirming on the first real push.

### What this doesn't cover yet

- **Injection/MITM-specific test scenarios beyond `escapeRmlText`'s unit
  tests.** The escaping function itself has real adversarial test cases
  (element injection, attribute breakout — see `test_rml_text_safety.cpp`).
  There is no man-in-the-middle test because there is no network
  transport yet to intercept (see Roadmap) — that's a test suite for a
  system that doesn't exist yet, not a gap in testing one that does.
  When a real `NetworkModule` transport lands, it needs its own
  adversarial tests (tampered packets, replay, a fuzzed
  `deserializeReplicatedState()`) before "man-in-the-middle" is a
  meaningful test category rather than a placeholder.
- **Fighting the two-tier split itself.** As more pure-logic modules
  appear (a real physics module's math, procedural generation seeds),
  they should get the same unit-test treatment and count toward the 85%
  floor; as more Vulkan modules appear, they extend the headless
  smoke-test's coverage contribution the same way `kke_demo` already
  does. Neither tier is finished — both grow with the engine.

## Performance / profiling tools

`StatsModule` is the "how performant is this actually" panel: FPS, CPU
frame time, and *GPU* frame time (measured via `vkCmdWriteTimestamp`
bracketing the render pass in `Renderer`), each with a 240-frame rolling
graph so spikes are visible, not just an instantaneous number that flickers
past. This is the base to build real profiling on top of — natural next
additions, all following the same "keep a small history buffer, plot it"
shape:

- Per-module CPU time (wrap each `update()`/`render()` call in a timer)
- GPU memory usage (VMA exposes budget/usage stats via `vmaGetHeapBudgets`)
- Draw call / dispatch counts per frame

## Known simplifications (intentional, still worth fixing)

- **Shader paths are relative to the working directory**, not the
  executable path — fine for `./build/bin/kke_demo` run from within
  `build/bin/`, but will need `SDL_GetBasePath()` before this ships
  anywhere else.
- **Mouse orbit doesn't use locked/relative cursor mode** — dragging past
  the window edge stalls the drag (release and re-drag to continue)
  rather than wrapping infinitely. Simpler and avoids fighting with
  ImGui's own mouse handling, at the cost of that edge case.
- **No real network transport** — `NetworkModule` measures and displays
  what would be sent (see "Cross-module communication"); nothing
  actually leaves the process yet.
- **RmlUi's `LoadTexture` (image files) is still a stub** — `<img>` and
  `background-image: url(...)` won't render; text (glyph atlases, via
  `GenerateTexture`) works.
- **RmlUi has input wiring but no resize handling yet** — mouse/keyboard
  work (see Roadmap "RmlUi slice 4"); its `Rml::Context` size is still
  set once at creation (see Roadmap "RmlUi slice 5").
- **No authority/reconciliation model** — `deserializeReplicatedState()`
  is implemented but unused; two peers disagreeing about state isn't
  handled.
- **Lua and stb are fetched but not called from any code yet.**
- **No icon wiring** — see "Branding" at the top.
- **Depth format isn't probed for support** — `VK_FORMAT_D32_SFLOAT` is
  assumed available (true on effectively every Vulkan-conformant device,
  including lavapipe, which is what's been verified here) rather than
  checked via `vkGetPhysicalDeviceFormatProperties` with a fallback.
- **20,000 particles is an arbitrary default**, not a measured "this is
  the performance ceiling" number — `StatsModule`'s GPU timing is exactly
  the tool to find that ceiling on your actual target hardware.

## Roadmap

This is the project's memory across sessions — every gap, every deferred
piece, every "this works but X is a simplification" gets written here as
part of the change that introduces it, not as a follow-up. If you're an
AI (or human) picking this project back up, this section plus "Known
simplifications" above is the fastest way to find out what's actually
true right now versus what's aspirational.

### Immediate next slices (each independently buildable/runnable)

- **Confirm the CI workflow actually runs on GitHub's infrastructure.**
  `.github/workflows/ci.yml` was written and its coverage-threshold
  parsing logic verified locally against real output, but the workflow
  file itself has never executed inside GitHub Actions — first real
  push should confirm it, since a sandboxed environment and a GitHub
  runner aren't guaranteed identical (package availability, etc.).
- **RmlUi input wiring** would also unlock testing the marketplace
  card's eventual "launch this game" interaction, once that exists.
- **Migrate remaining `std::cout`/`std::cerr` call sites to spdlog** —
  see "Logging"; only a representative few are converted so far.
- **`checkHardwareRequirements()` has no compatibility with dynamic
  content** — a game whose particle count, resolution, or texture
  budget changes at runtime (most real games) will drift away from
  whatever static numbers its `game.json` declared. The check is a
  startup-time snapshot against a fixed declaration, not a live monitor.
- **CMake's `POST_BUILD` copy of `game.json` only fires when the target
  actually rebuilds** — editing only `game.json` without touching any
  source file won't trigger the copy on the next `cmake --build`; found
  this while testing the hardware check (had to copy the file by hand to
  verify the warning path). Worth a proper fix (e.g. depending on the
  manifest file explicitly) rather than working around it by hand again.
- ~~RmlUi slice 2: a real Vulkan `RenderInterface`~~ — **done.**
  `RmlVulkanRenderInterface` compiles RmlUi's geometry into `Buffer`s and
  draws it through a real pipeline (`shaders/rml_ui.{vert,frag}`),
  verified with an on-screen test document.
- ~~RmlUi slice 3: textured rendering~~ — **done, for glyph atlases.**
  `GenerateTexture` (the path text rendering uses) creates a real
  VMA-backed image + `VkSampler` + descriptor set; untextured draws share
  the same pipeline via a persistent 1×1 white default texture. Verified
  with real anti-aliased, word-wrapped text on screen.
- **`LoadTexture` (image files) is still a stub.** This is the path
  `<img>` and `background-image: url(...)` come through — needs
  stb_image decoding a real file into pixels before it can reuse the
  same `GenerateTexture`-style upload path. Independent of text working.
- ~~RmlUi slice 4: input wiring~~ — **done.** `Module::onEvent()` is a
  new generic lifecycle hook (any module can use it, not just RmlUi);
  `UiModule` forwards mouse (move/buttons/wheel), a common-keys keyboard
  mapping, and text input into `Rml::Context`. Verified on screen with a
  `:hover` style change (a box turning white under the cursor and back).
  Building this surfaced a real, non-obvious bug worth recording: every
  hit-test was resolving to `body`/`#root` regardless of cursor
  position, traced (by instrumenting `Context::GetHoverElement()`, not
  by guessing) to `MarketplaceUiModule`'s full-screen transparent body
  swallowing hit-tests across the *entire* window for every document
  sharing that `Rml::Context` — fixed with `pointer-events:none` on that
  body. Worth remembering as more panels share one context.
- **RmlUi slice 5: resize handling.** `UiModule` sets the `Rml::Context`
  size once at creation and never updates it — resizing the window will
  make RmlUi's layout stale relative to the actual framebuffer size.
- **RmlUi + security**: once slice 3 exists and a chat system is even
  contemplated, establish the rule in code (not just convention) that any
  user-generated text is inserted as an RmlUi text node, never parsed as
  markup — this is the actual injection-risk boundary HTML-based UI
  introduces, and it's worth a real safeguard (e.g. a wrapper function
  that's the *only* sanctioned way to insert untrusted strings) rather
  than "remember not to do that."
- **Windows and macOS (MoltenVK) build verification** — Linux-first per
  the original plan; nothing built so far is known to be Linux-only, but
  nothing has been verified elsewhere either.
- **Texture loading via stb_image** — stb is fetched, unused; likely
  shares plumbing with RmlUi slice 3 above (both need a VMA image +
  sampler path) once either one is built.
- **A real network transport (sockets) behind `NetworkModule`** —
  currently measures payload sizes only, sends nothing.
- **Icon integration per platform.**
- ~~Marketplace UI~~ — **done, minimally.** See "Game folder convention
  & marketplace" above for exactly what this does and doesn't cover yet
  (non-interactive, scans once at startup, no compatibility checking).
- **Marketplace compatibility checking** — `engine_version`/`modules`
  fields in `game.json` are recorded but not validated against what the
  running engine build actually provides.
- **Dynamic multi-game builds** — CMake currently hardcodes one
  `add_subdirectory(games/kke_demo_game)`; scanning a `games/` directory
  and building whatever's found is separate, unstarted work from
  `MarketplaceIndex` scanning it at runtime.
- **Marketplace sandboxing — the most important open item on this whole
  list.** See "Game folder convention & marketplace" above in full; the
  short version is that native-C++ game folders have zero isolation,
  and a real marketplace needs either a sandboxed scripting layer (Lua,
  see below) or actual process/OS-level isolation before it can safely
  run third-party content.

### Foundational systems (needed by nearly everything below)

- **Physics.** Every game genre discussed for this engine needs it.
  Whichever library or from-scratch approach is chosen, determinism
  should be a design constraint from the start if rollback netcode is
  ever a goal (see below) — retrofitting determinism into physics code
  written without it in mind is much harder than designing for it.
- **Animation (skeletal).** Needed for fighting games, MMORPGs,
  cutscenes, and a shooter with any character models at all.
- **Input abstraction beyond raw SDL events.** Needed for rebindable
  controls and — cheaply, once it exists — split-screen local
  multiplayer (N input devices, N cameras/viewports from one swapchain;
  `Renderer` already separates "begin render pass" from "draw," which is
  the right split for multiple viewports).
- **Audio.** Not started at all.
- **General save/serialization.** Only exists narrowly today as
  `INetworkReplicable` (built for network replication, not save games,
  though the two overlap conceptually — worth revisiting once both
  exist whether they should share a mechanism).
- **A real asset pipeline.** Nothing currently loads external
  textures/models/audio.
- **Determinism discipline + a state snapshot/restore mechanism.**
  Prerequisite for rollback netcode specifically (see below) — no
  floating-point divergence across platforms in gameplay code, no
  reliance on unordered-container iteration order, everything
  gameplay-relevant in `fixedUpdate()`, and a cheap way for every
  participating module to snapshot/restore its entire state on demand.

### Networking — not one system, at least two

*Read [Gaffer On Games](https://gafferongames.com/) and study
[GGPO](https://github.com/pond3r/ggpo) before implementing either of the
two items below — see `AI_GUIDE.md`'s "Further reading" for why each is
relevant to which one.*

- **Client-server, authoritative** (MMORPG, chat, one shared destructible
  world, a shooter): server owns truth, clients predict and reconcile.
  This is the netcode style to build *first* — simpler to reason about
  than rollback, and needed for the MMORPG/chat/shared-world demos before
  rollback is touched at all.
- **P2P rollback** (fighting game): needs the determinism/snapshot
  prerequisite above. Doesn't scale past a handful of peers, so it is
  *not* the same system as the client-server case, even though both will
  likely share serialization/transport plumbing underneath. This is the
  single hardest item on the whole roadmap — general-purpose-hard, not
  just hard for this engine specifically.
- **Chat system** — needs a transport to exist first (either networking
  style); the UI side depends on the RmlUi slices above plus the text-
  insertion safety rule mentioned there.
- **Authority/reconciliation for conflicting `INetworkReplicable`
  state.** The interface supports it (`deserializeReplicatedState()`
  exists); nothing calls it yet because there's no real transport to
  receive conflicting state from.
- **Running multiplayer demos on one machine** — for testing
  client-server/P2P, this means the eventual transport supporting
  multiple local instances on `127.0.0.1` with different ports; a testing
  convenience to design for, not a separate feature.

### Procedural generation — two different problems sharing one pattern

*[Procedural Content Generation in Games](http://pcgbook.com/) (free
book — see `AI_GUIDE.md`'s "Further reading") covers the actual
algorithm families for both items below in real depth.*

- **Streaming/open-world (survival game)**: noise/heightmap-based
  terrain, generated once from a seed, with chunks streamed in/out by
  player position. Needs a chunk-streaming system; no generation
  algorithms exist yet.
- **Discrete/on-demand (dungeon crawler)**: algorithmic layout generation
  (room graphs, BSP, wave function collapse, etc.), generated per-floor
  on demand rather than streamed. Different algorithms, same underlying
  primitive as `DestructionModule` already demonstrates: seed in,
  deterministic content out.

### Genre-specific systems

- **Cutscene/timeline/QTE system** (movie-like game): trigger events at
  specific times, blend camera cuts, wait for player input within a
  window. Fairly self-contained once animation and input abstraction
  exist; not started.
- **Slow-motion physics bullets** (shooter): a good architectural fit
  already — `FixedUpdateContext::fixedDt` could be scaled down during a
  slow-mo window without touching render frame rate at all, since
  render-rate `update()` and simulation-rate `fixedUpdate()` are already
  separate. The actual gap is physics itself; a bullet is just a fast
  physics body with a collision callback.
- **Flying sim**: mostly stresses render distance/LOD and needs its own
  free-fly camera module (same `Module` pattern as `OrbitCameraModule`,
  not a modification of it).
- **Split-screen**: see "input abstraction" above — this is the item on
  the whole list that's closest to "just needs the input+viewport work,"
  not a new rendering or networking system.

### The Roblox-like ambition (scripting for non-programmers)

This is also the actual path to a marketplace that's safe for
third-party content — see "Game folder convention & marketplace"'s
sandboxing caveat above; scripted (not compiled) game folders are the
realistic route to that, not a smaller version of what exists today.

Lua is fetched by CMake and called by zero lines of code today. Getting
from here to "non-programmers can script games" needs, at minimum: (1)
actually binding the `Module` lifecycle to Lua, not just C++; (2) a
sandboxing story — embedded Lua can call into anything exposed to it, so
"safe to run untrusted/marketplace scripts" is a security design
question, not a given, and matters a lot for a Roblox-marketplace-like
goal; (3) probably a visual scripting layer or at minimum a much
friendlier API than the raw C++ `Module` interface translated 1:1. This
is a project-sized effort on its own, not a module.

### Combining systems into one game

Whether physics + networking + destruction + procedural generation can
all coexist in one game is mostly a **module-discipline** question, not
a rendering or networking one: if each is built talking through
capability interfaces (as `DestructionModule`/`NetworkModule` already
demonstrate) rather than hard-coding assumptions ("physics owns the only
rigid bodies," "networking assumes exactly 2 players"), combining them
stays additive. The module system makes that *possible*; it doesn't make
it *automatic* — whoever writes each future module has to keep to the
pattern.
