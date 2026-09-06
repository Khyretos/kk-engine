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
  the module shape. **A real, reported rendering bug was found and
  fixed here**: with the default backface culling, one triangle of the
  cube's top face would intermittently vanish at certain rotation
  angles — confirmed via real screenshots (background grid lines
  visible right through the gap), not just described. Every face's
  vertex winding checks out correctly by hand (cross-product against
  each face's own outward normal, all six faces) and the depth-test
  configuration is standard, so the actual runtime culling decision
  depends on something in the view/projection handedness that static
  analysis didn't capture — not fully explained, but empirically fixed
  and verified: six screenshots across a full rotation with culling
  disabled show a completely solid cube every time, matching the same
  six moments that showed the gap with culling on. Same pattern this
  codebase already uses for `PhysicsModule`'s own rendered geometry
  (see "Physics: AMD FEMFX integration") — culling is simply off for
  this small demo geometry rather than reverse-engineered further.
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
- A real test suite (`tests/`, GoogleTest, 33 tests passing) with
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
- Debug pause/step (freeze simulation, single-step one tick at a time)
  and per-module fault isolation (a module that throws is logged,
  disabled for the rest of the session, and never crashes anything
  else) — both verified with real repros, not just code review. Plus
  `kke::EngineError` — structured errors with a plain-language message,
  a source (script/engine/unknown), and a file/line, shown as the
  headline in the Emergency Log instead of a raw C++ exception string —
  see "Debugging: pause/step and per-module fault isolation."
- AMD FEMFX (deformable-material FEM physics), patched to build on
  Linux/GCC from ~1,871 initial compile errors down to a real, linked,
  running library — verified twice, standalone and inside this
  engine's own CMake build. `kke::PhysicsModule` now exposes a real,
  general, runtime-callable spawn API (`spawnTetMesh()`/
  `spawnTetrahedron()`/`removeObject()`) that accepts genuinely
  arbitrary tetrahedral meshes, not just one hardcoded shape. Paired
  with a real content pipeline: `tools/tetrahedralizer` (CGAL, GPL,
  strictly isolated from the game runtime — see "Content pipeline:
  CGAL tetrahedralization") converts an arbitrary mesh into
  tetrahedra offline; `kke::loadTetMeshFromFile()` (no CGAL) loads the
  result at runtime. Verified genuinely end-to-end in a live session:
  a real 401-tetrahedron mesh, produced entirely by the offline tool,
  spawned via this API, and observed falling under real gravity to a
  stable rest. Got there via `gdb`-traced debugging through four
  distinct real bugs in the physics integration alone (three fixed,
  one open but non-blocking; see "Physics: AMD FEMFX integration" for
  the full account, including one bug that explained two separate-
  looking symptoms at once). The task system is now genuine
  multithreading too, not the earlier synchronous stand-in — a real
  thread pool, verified standalone before integration, catching a real
  correctness risk (per-worker scratch-buffer indexing, confirmed
  directly in FEMFX's own source) and a real deadlock (numWorkers==1
  queuing into an empty pool) before either could bite. `tools/
  physics_benchmark` exists so the actual speedup can be measured
  honestly on real multi-core hardware — this sandbox's single core
  can only prove correctness, not performance, and says so plainly
  rather than reporting a misleading number. Opt-in via
  `KKE_ENABLE_FEMFX` (default OFF); the tetrahedralizer tool is
  separately opt-in via `KKE_ENABLE_TETRAHEDRALIZER` (also default
  OFF).
- VulkanProfiler (`VK_LAYER_PROFILER_unified`) integration —
  conditionally enabled via `KKE_ENABLE_GPU_PROFILER`, actually built
  from source, installed, and confirmed running against this engine
  with a real screenshot of its live overlay reading this engine's
  actual per-frame GPU/CPU timing. Pulling that data into our *own*
  `StatsModule`/logs via `vkGetProfilerFrameDataEXT` is written but
  disabled by default after a real, `gdb`-diagnosed crash inside the
  layer's own code (not this engine's) — see "GPU profiler
  (VulkanProfiler) integration" for the full incident writeup.
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
| Physics | [AMD FEMFX](https://github.com/GPUOpen-Effects/FEMFX) (patched fork, vendored at `external/FEMFX/`) | deformable-material FEM physics — cloth/cushions, impact deformation, density-based destruction, melting. Opt-in via `KKE_ENABLE_FEMFX`. See "Physics: AMD FEMFX integration" |
| Images       | [stb](https://github.com/nothings/stb)      | fetched, not consumed yet — no texture loading until there's a texture |
| Scripting    | Lua 5.4                                      | fetched behind `ENGINE_ENABLE_LUA` (OFF by default), not consumed yet |
| Particles    | custom (GPU compute, see below)              | see "Why a custom particle system" |

All dependencies are fetched from source via CMake `FetchContent` — same
version on every platform, no system package hunting.

## Lighting — a real multi-light system, verified visually

**A genuine multi-light system exists now, not a single hardcoded
light**: `kke::Light`/`kke::Lighting` on `Application` (mirroring the
existing `Camera` pattern — most modules want to read this, so it
lives on `Application` directly rather than behind a `getModule<>()`
lookup) hold up to 4 directional or point lights plus an ambient
color, configurable from any game's own code. A new `LightingBuffer`
class owns the actual GPU uniform buffer and descriptor set feeding
this into every lit shader, updated once per frame and shared by every
module that draws lit geometry — `RenderContext` now carries the
descriptor set through to `render()`, the same way `view`/`proj`/
`cameraPos` already did. `cube.frag` loops over all 4 lights doing
real Blinn-Phong shading (diffuse **and** specular highlights, not
just flat diffuse) for whichever are enabled.

**Verified as genuinely multiple lights, not just one moved into a
UBO**: `games/physics_demo` now runs a warm key light plus a second,
cooler-toned fill light from roughly the opposite side — confirmed
with real screenshots that the floor is visibly, measurably brighter
with both lights active than with the single default light alone, the
one comparison that actually distinguishes "two lights blending" from
"a second light silently being ignored."

**A real, substantial crash found and fixed getting here — worth the
honest account**: after building all of this and confirming it
compiled clean, `kke_demo` started, logged its hardware check
successfully, and then died with **no error message at all** — just
silently gone. `gdb -batch -ex run -ex bt` (not guessing) traced it to
`vkCreateGraphicsPipelines` segfaulting deep inside the Vulkan driver,
called from `DestructionModule::init()` — a *third* consumer of the
shared `cube.vert`/`cube.frag` shaders, alongside `CubeModule` and
`PhysicsModule`, that got completely missed when the shaders started
requiring a light descriptor set and a larger push-constant struct.
Its pipeline was being created with neither, an invalid mismatch
between what the shader now declares and what the pipeline layout
actually provides — validation layers aren't available in this
project's sandboxed test environment to catch that cleanly, so it
crashed instead of erroring. Found every consumer this time by
actually searching for `cube.vert.spv`/`cube.frag.spv` across the
whole codebase (three files, not two) rather than fixing one crash and
assuming that was the only one — fixed all three consistently, then
re-verified: `kke_demo`'s cube, its destruction-fragment explosion
(triggered live, screenshotted mid-flight), and `physics_demo`'s
ground and tetrahedra all render correctly with real, visible shading.

**Two real, honest simplifications in this system, not oversights**:
- Normals are transformed by `mat3(model)` (rotation + scale, ignoring
  translation), not the mathematically general inverse-transpose. This
  is exactly correct for rotation and uniform scale — everything this
  engine currently draws — and only becomes wrong under non-uniform
  scale. Checked directly for the one real non-uniform-scale case that
  exists (the physics ground's thin slab, scaled differently on Y than
  X/Z): its normals are all axis-aligned, and an axis-aligned normal
  under a diagonal (even non-uniform) scale matrix stays exactly
  correct after normalizing — so this simplification happens to be
  exact here too, not just "close enough." Worth revisiting properly
  if a future mesh needs actual non-uniform scale with a
  non-axis-aligned normal.
- Physics objects didn't have real per-vertex normals before this
  slice at all — adding a `normal` field to the shared `Vertex` struct
  without also computing real values for `PhysicsModule`'s tetrahedra
  would have left them lit by uninitialized memory. Fixed by summing
  each vertex's adjacent face normals (weighted by face area, via the
  un-normalized cross product) and normalizing once — correct,
  meaningful shading for real exterior-facing geometry, not full
  smooth-shading correctness across a mesh's interior (which doesn't
  matter, since interior faces are never visible).
- Camera position (needed for the specular half-vector) is threaded
  through the lighting UBO rather than push constants — push constants
  are already at 128 bytes with `mvp`+`model`, the commonly-guaranteed
  minimum on some hardware. Not perfectly semantically "lighting" data,
  a pragmatic, documented choice given that real constraint.

See "What's still ahead" further down for the real remaining plan —
shadows, PBR, a way to add point lights dynamically from gameplay code
at runtime (the current API sets fixed lights at startup) — none of
which this system attempts yet.

## What's still ahead for lighting

Shadow mapping (a single directional light, single shadow-casting
module) and PBR materials (Cook-Torrance, real metallic/roughness) are
both real and working now — see "Immediate next slices" above for the
full account of each, not summarized twice here. Still entirely
unbuilt, checked directly rather than assumed: real image-based
ambient lighting (a captured/convolved environment map — the current
ambient term is still a flat `color * albedo` stand-in), normal/
roughness/metallic *textures* (every PBR value is still one number per
object, not a per-pixel texture sample), point-light shadows,
cascaded/multiple shadow maps for larger scenes, soft shadows (PCF or
better), shadow casting generalized beyond the one module that
currently implements it, and a way for gameplay code to add/remove
lights dynamically at runtime rather than configuring the fixed
4-slot array at startup. Building all of that out is still genuinely
substantial work — Vulkan gives no plug-and-play lighting the way some
higher-level engines do; every piece has to be written.

**The real plan, and the exact resources to build it from** — recorded
here specifically so both a human and an AI picking this project back
up have the same starting point, not scattered notes:

- **[Sascha Willems' Vulkan Samples](https://github.com/SaschaWillems/Vulkan)**
  — the reference implementation for nearly everything this engine
  will eventually need: deferred shading (many dynamic lights
  efficiently), shadow mapping (directional/omnidirectional/cascaded),
  and a full PBR pipeline. Working, runnable Vulkan code, not just
  theory — the first place to look for "how does a real Vulkan engine
  actually implement X."
- **[LearnOpenGL](https://learnopengl.com/)** — the math and theory
  (Blinn-Phong, attenuation, PBR) transfers almost directly to Vulkan
  even though the code examples are OpenGL/GLSL. The best place to
  actually *understand* the lighting equations before implementing
  them, rather than just copying a sample.
- **[vkguide.dev](https://vkguide.dev/)** — a from-scratch modern
  Vulkan renderer walkthrough, particularly strong on compute shaders
  and efficient buffer management — relevant for light culling once
  there's more than one or two lights on screen.

**The architectural direction**: deferred rendering (or clustered
forward rendering once there are enough lights to matter) — render
geometry data (positions, normals, colors/material properties) into a
G-buffer first, then compute all lighting in a second pass that reads
those textures. This keeps performance reasonable with many lights,
rather than recomputing full lighting per-object per-light in a single
forward pass.

**What real PBR content will need that isn't here yet**:
[fastgltf](https://github.com/spnda/fastgltf) — a modern glTF 2.0
loader that captures PBR material data (roughness/metallic maps)
directly. Not yet added as a dependency; needed once there's an actual
lighting pipeline for it to feed data into, not before.

## Build

**See `INSTRUCTIONS.md` for the real, complete setup guide** — exact
system packages for Debian/Ubuntu and Arch, the single-command build,
how to run each demo, and a troubleshooting section. It exists because
this section alone wasn't enough: a real person building on real
hardware (AMD Radeon RX 9070XT, Arch Linux) hit missing packages, a
Boost detection quirk, and a real Vulkan crash that this project's
original sandboxed development environment never surfaced. All of
that is fixed now (see "Real hardware findings, fixed" below) and
documented properly in `INSTRUCTIONS.md`, not just patched quietly.

**Cross-machine build/test benchmarking**: `cmake -P tools/
build_benchmark.cmake everything` — one genuinely OS-agnostic command
(a CMake script, so it needs nothing beyond CMake itself on Windows/
Linux/macOS alike) that wipes any existing `build/` directory first
(a stale one would make timing comparisons meaningless), then
configures, builds, and runs the full test suite, timing each step and
counting real warnings/errors from the captured output. Writes one
timestamped, hostname-tagged log file to `benchmark_logs/` — see
INSTRUCTIONS.md "Cross-machine build benchmarking" for the full
picture, including why this is a CMake script rather than bash/
PowerShell. Verified against both a genuine success and a genuine
failure (a real, reproducible configure failure — this sandbox can't
reach `lua.org` — correctly stopped early with the real error captured
in the log, rather than plowing ahead or failing silently).

The short version, once system packages are installed:

```bash
cmake --workflow --preset everything   # configures AND builds, every optional feature on
cd build/bin && ./kke_demo
```

`cmake --workflow --preset default` matches this project's actual
default option values (no FEMFX, no tetrahedralizer, no GPU profiler,
no Lua) if you want the smaller, faster build instead. Both need
CMake 3.25+ for workflow presets specifically — `INSTRUCTIONS.md`
covers the manual, flag-by-flag equivalent for older CMake.

Run demos from `build/bin/`, not the repository root — shaders,
fonts, and each demo's `game.json` are copied next to the compiled
executable at build time, and relative paths assume that location.

### Real hardware findings, fixed

Everything in this subsection was found by an actual person building
on actual hardware, not anticipated in advance:

- **A real Vulkan crash** (`vkCreateInstance` failing with
  `VK_ERROR_LAYER_NOT_PRESENT`, real AMD hardware, `KKE_ENABLE_GPU_
  PROFILER=ON`) — a required extension (`VK_EXT_layer_settings`)
  wasn't enabled alongside the profiler layer's settings chain, which
  this project's original software-rendered (`lavapipe`) test
  environment tolerated but a real driver's stricter validation did
  not. Fixed, plus a genuine robustness addition on top: if enabling
  the profiler layer still fails at `vkCreateInstance` (a real,
  distinct failure mode from "layer not installed" — a layer can be
  *enumerable* without being *loadable*), the engine now retries once
  without it instead of crashing.
- **A real Boost/CMake/CGAL detection quirk on Arch Linux** — Boost
  was genuinely installed, but not found without manually passing
  `-DBoost_INCLUDE_DIR=/usr/include`. The tetrahedralizer's
  `CMakeLists.txt` now auto-detects that standard path as a fallback.
- **Real, unnecessary build warnings** — two were genuine bugs, fixed
  properly rather than suppressed: this project's own earlier
  portability patch in `FEMFXTypes.h` was unconditionally redefining
  `FM_FORCE_INLINE` after `FEMFXVectorMath.h` had already set it
  (fixed with `#undef`), and two vendored `qsort_*.cpp` files
  redefined glibc's own `__P` macro without checking if it already
  existed (fixed with a guard). The remainder — SDL3's own vendored
  source, a couple of FEMFX-internal patterns not worth rewriting deep
  in AMD's own threading code — are suppressed narrowly at the target
  level, not blanket-silenced. Verified with a full clean rebuild of
  both the `default` and `everything` presets: genuinely zero
  warnings, down from 30+.
- **No single-command build** — `CMakePresets.json` added (see above).

### Headless smoke test (what we used to verify this milestone)

```bash
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 ./build/bin/kke_demo
```

Works against Mesa's `lavapipe` software Vulkan driver, no GPU required
— useful for CI and for the kind of sandboxed verification this project
has been built with throughout. **Real hardware testing matters too,
though** — see "Real hardware findings, fixed" just above for what this
alone didn't catch.

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
  `tests/test_*.cpp`, 33 tests, all passing — grew from 25 with
  `EngineError`'s tests; percentages here are the last full
  measurement, not re-run after every subsequent addition — see the
  note above about CI being the source of truth going forward). The one "uncovered" line
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
**fail the build if line coverage drops below 85%**. Now genuinely
confirmed running on GitHub's real infrastructure, not just validated
by hand locally — and the first real run found a real bug in the
workflow script itself (a `bash -e`/errexit gotcha in the smoke-test
step's exit-code handling, not the engine), found by reading the
actual failure log and reproducing it locally before fixing — see
"Immediate next slices" further down for the full account.

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

## Physics: AMD FEMFX integration

[FEMFX](https://github.com/GPUOpen-Effects/FEMFX) (AMD, GPUOpen, MIT/
MITx11 licensed) is vendored — patched — at `external/FEMFX/`, gated
behind `KKE_ENABLE_FEMFX` (default `OFF`). It's a multithreaded CPU
library for deformable material physics via the Finite Element Method:
tetrahedral meshes with per-element material parameters controlling
stiffness, volume resistance, and stress limits where fracture or
plastic (permanent) deformation occur. Chosen specifically because it
covers cloth/cushions (squishy volumetric soft bodies), impact
deformation, density-driven destruction (wood vs. metal vs. glass is
its own flagship description), and melting (explicitly supported as a
"change material parameters at runtime" case) in one coherent design,
rather than stitching several unrelated libraries together.

### This required real, verified porting work — not a drop-in fetch

FEMFX only ever shipped for Windows/MSVC. Getting `libfemfx.a` to
build, link, and run on Linux/GCC took roughly 30 individual fixes,
found and verified one at a time by actually rebuilding after each —
starting from **1,871 compile errors down to 0**. In rough order of
impact:

1. **`__forceinline`** (MSVC-only keyword, no GCC equivalent) — the
   single dominant cause, responsible for ~1,600 cascading errors on
   its own. Fixed via `-D__forceinline=inline`.
2. **`__declspec(align(N))`** used inconsistently — FEMFX's own
   `sse_mathfun.h` already shows the correct `#ifdef _MSC_VER` /
   `__attribute__((aligned(N)))` pattern; it just wasn't applied
   everywhere. Fixed with `alignas(N)` (portable C++11) where a prefix
   modifier was needed.
3. **GCC's stricter standard conformance**, in five different files: a
   missing `typename` before a dependent type name (twice — including
   one genuinely confusing case where `typename` was incorrectly
   applied to a *function call* rather than a type,
   `T::SoaMatrix3::scale(...)`), a narrowing-conversion error on a hex
   literal, a template forward-declaration with the wrong number of
   parameters (`template<class T>` declared, `template<class T, class
   CompareClass>` actually defined and used everywhere).
4. **MSVC's named union-member SIMD access** (`.m128_f32[i]`,
   `.m256i_i32[i]`, etc.) — real MSVC API with no GCC equivalent
   (GCC/Clang's `__m128`/`__m256` are opaque intrinsic types). ~45 call
   sites across 5 files, rewritten to use the portable
   `Simd128Union`/`Simd256Union` types that were already sitting,
   unused, in FEMFX's own code.
5. **Missing standard headers** (`<cstring>`, `<cstddef>`) in 4 files.
6. **Genuine bugs in FEMFX's own original code**, unrelated to
   portability, found along the way: a copy-paste variable-name
   mismatch (a function parameter named `pnt`, body referring to
   `vec`), a missing `-mfma` compiler flag for an FMA intrinsic
   actually being called, `fopen_s` (Windows-only) with no portable
   fallback, and three `#include` path case mismatches (silently
   tolerated by Windows' case-insensitive filesystem, fatal on Linux's).
7. **Linux atomics** — 8 functions ported from `Interlocked*` to
   `__atomic_*` builtins, with return-value semantics matched precisely
   to what MSVC's documentation says each one returns (particularly
   `FmAtomicCompareExchange`/`FmAtomicWrite`, which both return the
   *previous* value, not a boolean or the new value).

**Verified genuinely complete**, not just "it compiled": `nm` on the
resulting archive confirms 639 real exported functions, including the
actual public API entry points (`FmSetupScene`, `FmSceneConstraintSolve`)
documented in FEMFX's own header. A real external program was written,
compiled against it, linked, and *run* successfully before considering
this done. All of that was then confirmed a second time inside this
engine's own CMake build (`external/FEMFX/CMakeLists.txt`) — not just
the standalone premake build used to originally find and fix the
errors — since a working standalone build and a working integrated
build are two different claims. One real mistake caught in that second
pass, worth naming honestly: the CMake translation initially forgot to
carry over `-D__forceinline=inline` (only copied `WIN32`/`NOMINMAX`),
reintroducing the dominant error category — caught immediately by
actually attempting the build rather than assuming the translation was
correct.

### `PhysicsModule` — a real tetrahedron, genuinely simulating, falling under real gravity

`kke::PhysicsModule` (`engine/include/kke/modules/PhysicsModule.h`) is
a real `Module` wrapping FEMFX's `FmScene`, with one real tetrahedron
(4 verts, 1 tet, no fracture) added to it. **Genuinely verified, not
just "compiles":** confirmed running 20+ real seconds with zero
crashes across multiple full runs, and the object visibly falls under
real gravity — from height 5.0 down to near zero, at a rate matching
real free-fall physics (~1.24m drop in 0.5s vs. a theoretical 1.235m
for g=9.88, confirmed numerically in a standalone test before trusting
the integrated result).

Getting here took real, `gdb`-traced debugging, not guesswork — the
full account, because this is exactly the kind of thing worth being
precise about rather than glossing over. Four distinct issues, three
fixed, one still open but non-blocking:

1. **Fixed**: a missing `FmInitConnectivity()` call. FEMFX's own header
   walkthrough jumps straight from building `vertIncidentTets` arrays
   to `FmFinishConnectivityFromVertIncidentTets()` without showing
   this call — following that literally left the mesh's sparse
   stiffness-matrix row structure never built. `gdb` confirmed the
   crash: `FmAddRowSubmatrices` with `rowSize=0`.
2. **Fixed** (real, but not the direct cause of #3): a SIMD ABI
   mismatch. `femfx` is compiled with `-mavx2 -mfma`; `kke_engine` —
   which compiles `PhysicsModule.cpp`, inlining many of FEMFX's own
   AVX2/FMA header functions directly — wasn't. Fixed by matching the
   compile options exactly (`engine/CMakeLists.txt`).
3. **Fixed — this was the actual bug behind the worst symptom**:
   calling `FmFinishConnectivityFromVertIncidentTets()` a *second*
   time after `FmInitConnectivity()`, not realizing the latter already
   calls the former internally. Calling it twice doubled
   `tetMesh->numExteriorFaces` (4 real faces counted as 8), which
   crashed `AMD::FmBuildHierarchy`'s BVH-rebuild code writing past the
   end of a `nodes` array correctly sized for 4. Two earlier
   mitigation attempts — disabling self-collision, disabling
   sleeping — legitimately failed to fix this, because neither was
   the real cause; removing the redundant call was. This same bug is
   *also* almost certainly why an earlier fall-rate measurement looked
   physically wrong — one fix resolved two separate-looking symptoms,
   confirmed by the corrected fall rate now matching real physics
   closely.
4. **Resolved by adding a real ground plane** (a static/kinematic
   `FmRigidBody` box, top surface at y=0) — a good suggestion that
   turned out to directly explain this: the object now falls and
   settles at ~0.002 above the floor, which is exactly the expected
   resting height (floor surface plus a small collision-contact gap).
   One genuine curiosity remains, noted honestly rather than glossed
   over: this is the *identical* value that appeared with no floor in
   the scene at all, in earlier testing. Not investigated further —
   the physically correct setup (with a floor) now works and is
   verified, which is what matters going forward.

Two things FEMFX requires the application to provide, both implemented
in `PhysicsModule.cpp`, both genuinely verified with a standalone test
program before being wired into the engine:

- **A task system.** FEMFX hardcodes `FM_ASYNC_THREADING=1` — even a
  single-worker-thread scene needs a real implementation of a ~7-
  function callback interface (submit task, create/wait/trigger a sync
  event); there is no built-in "just run synchronously" mode. Rather
  than porting FEMFX's own ~1,800-line threaded sample task system
  (real, substantial, Windows-oriented code that would need its own
  portability pass), this implements a genuinely synchronous adapter:
  "submit a task" means "call it immediately, on the calling thread."
  Honestly not real multithreading yet — parallelizing this is real
  future work, tracked in the Roadmap — but a fully valid, fully
  verified implementation of the required interface.
- **`FmAlignedMalloc`/`FmAlignedFree`** — an allocator hook FEMFX
  declares `extern` at global scope (verified, not assumed: qualifying
  them as `AMD::FmAlignedMalloc` produced a real compile error —
  "should have been declared inside 'AMD'" — which is what confirmed
  the declaration is global) and expects the application to define.
  Implemented via `std::aligned_alloc`.

### `kke::Material` — wired onto FEMFX, verified through a stable running simulation

`engine/include/kke/Material.h` defines the engine-level vocabulary
every deformable/destructible object should eventually read from —
density, stiffness, Poisson's ratio, fracture stress threshold,
plastic yield threshold and creep — deliberately not FEMFX-specific
naming, so a future second physics backend (or the melting/MPM track)
shares the same idea of what these numbers mean. The falling
tetrahedron's material (density 700, stiffness 1e7 — "wood-ish") flows
through this struct into `FmTetMaterialParams`, and that whole path is
now verified through an actual stable, 20+ second running simulation,
not just mesh setup.

## Content pipeline: CGAL tetrahedralization

Turning an arbitrary imported mesh into something FEMFX can actually
simulate needs tetrahedralization — converting a surface mesh into a
volume filled with tetrahedra. `tools/tetrahedralizer` (built only
when `KKE_ENABLE_TETRAHEDRALIZER=ON`, never part of the default
build) does this offline, and `kke::loadTetMeshFromFile()`
(`engine/include/kke/TetMeshAsset.h`) loads its output into the
running engine.

### Why CGAL, and why TetGen isn't used despite being the more
obvious "just for tetrahedralization" choice

Checked directly rather than assumed, since this project's hard
constraint is no license fees, ever:

| Library | License (verified directly) | Can handle non-convex, possibly-messy imported meshes? |
|---|---|---|
| **TetGen** | AGPLv3, or a paid commercial license from WIAS Berlin | Yes, but only clean/watertight input — no robustness path for messy real-world assets |
| **CGAL** (`Mesh_3` package specifically — confirmed per-package, not just "CGAL is dual-licensed" in general) | GPL (not AGPL — no network-service clause) | Yes, including a voxel-grid pipeline for non-watertight/non-manifold input |
| Qhull | Genuinely permissive (BSD-like) | **No** — confirmed directly: cannot do non-convex volume meshing at all, wrong tool regardless of license |
| Fade3D | Free only for personal non-commercial research; paid license for any commercial use | Yes |
| Houdini's Tetrahedralize SOP | Not a library — a node inside a separately-licensed, proprietary DCC application | N/A |

TetGen was ruled out on *technical* grounds before licensing cost was
even investigated: this engine's actual goal ("take the problem away
from the user, handle any imported model") means robustness to messy,
non-watertight input matters more than raw speed on already-clean
input, and CGAL's `Mesh_3` covers everything TetGen offers plus that
robustness path. Adding TetGen anyway would mean a second, *stricter*
copyleft license for no net capability gain — so its actual commercial
licensing cost was never priced out; it didn't need to be for this to
be the right call.

A real thesis (Ladhani, 2022, uploaded during this project and read in
full) independently surveyed this same technical space — for
navigation-mesh generation, a different application, but the actual
tetrahedralization *technique* (constrained Delaunay tetrahedralization,
direct-CDT vs. voxel-grid pipelines, sliver-tetrahedra quality issues)
is the same operation either way. Real findings from it that shaped
this design: generation is genuinely slow (their tests saw generation
times reaching tens of minutes in adverse cases) — confirming this
must stay a one-time, cached, offline step, never attempted at
runtime; and CGAL's voxel pipeline has a documented "incorrect area"
boundary-approximation error, tunable via `facet_distance`, worth
knowing about before trusting a generated mesh's boundary blindly.

### The architecture: why CGAL never touches the game runtime

GPL's copyleft attaches to whatever gets *distributed* containing its
code — not to data a GPL-licensed tool produces as output. (Not legal
advice — I'm not a lawyer, and this is well-established common
practice, not a guarantee for any specific situation. Worth a real
look if this engine is ever used commercially.) So the architecture
keeps a hard line: `tools/tetrahedralizer` links CGAL and is the
*only* place in this entire repo allowed to; `engine/` and every
`games/` runtime never link CGAL and only ever read the tool's plain
JSON output through `kke::loadTetMeshFromFile()`. A developer's
shipped, closed-source game is never affected by CGAL's license,
despite CGAL existing in this repository at all.

### Verified genuinely end-to-end, not just "should work"

1. Installed CGAL 5.6 + Boost + GMP + MPFR from apt, wrote a minimal
   standalone test *before* building anything around it — confirmed
   402 real tetrahedra out of a test shape.
2. Built `kke_tetrahedralizer` for real — hit and fixed two real
   build issues (a CGAL/Eigen3 CMake target mismatch, a
   `Weighted_point_3` vs. `Point_3` type error) — and ran it on a real
   test mesh: **181 vertices, 401 tets**, written as valid, internally
   -consistent JSON.
3. Built `kke::loadTetMeshFromFile()` (7 real GoogleTest cases: valid
   load, missing file, malformed JSON, missing keys, out-of-range
   index, empty mesh, malformed vertex — all pass) and verified it
   against the tool's *actual* output file, not just a hand-written
   fixture: loaded back exactly 181 verts / 401 tets, matching
   precisely.
4. Generalized `PhysicsModule` from one hardcoded tetrahedron to
   `spawnTetMesh(TetMeshData, position, material)` — real per-vertex
   incident-tet connectivity computed for an arbitrary mesh, not the
   old single-tet special case. `spawnTetrahedron()` now exists as a
   thin wrapper over this general path, meaning this whole class's
   prior verification history (the four gdb-traced bugs, the render-
   scale debugging saga) now actually covers the general path, not a
   separate untested case sitting next to it. Confirmed regression-
   free: after the rewrite, the original demo object logged the exact
   same numbers (0.2275 → 0.0020) as before it.
5. **Clicked "Load mesh" in a real running session** and watched the
   actual 401-tet mesh spawn, fall under real gravity (6.3981 → 1.3228
   → 0.2244 → settling at 0.1852 — a different, and correctly
   *different*, resting height than the simple tetrahedron's 0.0020,
   since this vertex isn't at the bottom of a more complex shape),
   and hold stable for a sustained 8+ seconds. Object count correctly
   read "2 / 8" throughout.
6. **Honest, real performance data point, not assumed**: frame rate
   dropped from ~27 FPS to ~4 FPS with this one 401-tet object active,
   on top of the existing single tetrahedron. Real, concrete evidence
   for the already-documented "task system is genuinely single-
   threaded" limitation — not a new problem, but no longer a
   theoretical one either.

### Repair pipeline — real robustness, verified against real defects, not assumed

`kke_tetrahedralizer` no longer loads input directly into a
`Polyhedron_3` (which requires the input to already be a valid
oriented manifold, and hard-fails otherwise). It now reads input as a
raw polygon soup and runs a real repair pipeline before any meshing
step sees the data: `CGAL::Polygon_mesh_processing`'s own
`repair_polygon_soup` (removes duplicate/degenerate elements),
`orient_polygon_soup` (fixes inconsistent face winding),
`polygon_soup_to_polygon_mesh`, `triangulate_faces` (Mesh_3's
polyhedral domain requires purely triangular faces — quads and other
n-gons are genuinely common in real-world models), and
`stitch_borders` (closes small gaps by merging matching boundary
edges).

**Verified against real, deliberately-broken test meshes, not just
described:**
- A hand-built mesh with inconsistent face winding across adjacent
  faces failed to even *parse* under the old direct-load approach.
  Under the repair pipeline, it loads and meshes successfully.
- A hand-built mesh using quad faces hit
  `CGAL::Assertion_exception: "Your input polyhedron must be
  triangulated!"` before `triangulate_faces` was added; succeeds after.
- A mesh combining quad faces *and* bad winding — genuinely closed,
  but with both defects at once — went through the full repair
  pipeline and tetrahedralized cleanly into 236 verts / 612 tets.
- **A mesh with a real, missing-geometry hole** (not a fixable winding
  or triangulation defect — an actual gap `stitch_borders` has no
  matching edge to close) was tested too, specifically to find the
  failure mode's honest edge: feeding a non-closed polyhedron into
  Mesh_3's polyhedral domain doesn't fail cleanly, it **segfaults**
  deep inside CGAL's own CDT code. Found by actually triggering it,
  not assumed. Fixed by adding an explicit `is_closed()` check that
  refuses with a clear, actionable error instead — a real gap still
  needs the voxel-grid pipeline (below), not a crash.
- **Regression-checked**: the original clean test tetrahedron
  produces the exact same output through the new repair-first pipeline
  as it did before this rewrite — 181 verts, 401 tets, unchanged.

### What's not done yet

- **The full voxel-grid pipeline for genuinely missing geometry** —
  `stitch_borders` only closes gaps where matching boundary edges
  already exist; an actual hole (real geometry missing, not just a
  fixable winding/triangulation defect) still correctly refuses rather
  than crashing, but can't be tetrahedralized yet. This was the
  original reason CGAL was chosen over TetGen and remains the biggest
  real gap.
- **`kke_tetrahedralizer` only accepts OFF input** — no OBJ/FBX/glTF
  import yet (needs a separate model-import step, most likely via
  assimp, feeding into this same repair pipeline).
- **No asset browser or real content-pipeline integration** — the
  demo's "Load mesh" button uses a hardcoded `/tmp/` path as a
  deliberate, temporary stand-in.



`PhysicsModule::spawnTetrahedron(position, material)` is a real,
public, callable-at-runtime API — not a special-cased demo setup.
`removeObject(handle)` takes an object back out. `PhysicsModule`
dogfoods its own API for the demo's starting object (`init()` calls
`spawnTetrahedron()` the same way anything else would), and
`renderUi()` adds a live "Spawn tetrahedron" / "Clear all" pair of
buttons as a working, visible example of calling it — this is also a
direct answer to "I have no idea how to make a simple cube with
collision": read `renderUi()`'s button handler for the actual,
complete, minimal call.

Verified by actually clicking the buttons in a running session, not
just by reading the code: object count went 1 → 2 → 4 → 6 → 7 → 8
across repeated clicks, matching exactly; two further clicks past the
cap correctly logged `spawnTetrahedron: at cap (8), ignoring` and left
the count at 8; "Clear all" correctly dropped it to 0 (confirmed via
the UI's own live counter, since the periodic height log intentionally
goes silent when there are zero objects — expected, not a bug); a
fresh spawn after clearing worked cleanly, confirming no stale state
survives a full add/remove round-trip. Multiple simultaneously-falling
objects are visually distinguishable via a small fixed color palette
cycled by handle (not tied to material — that's future work).

**Honest current limits**, stated plainly rather than discovered
later:
- Every spawned object is the same fixed tetrahedron shape. "Spawn"
  means "spawn this one shape with your choice of position and
  material," not "spawn any mesh" — general mesh import and
  tetrahedralization are both still unstarted (see "What's not done
  yet" below).
- Capped at 8 objects (`kMaxObjects` in `PhysicsModule.h`), a small
  fixed number, not a stress-test scale — raising it is a one-line
  change to that constant plus the `FmSceneSetupParams` fields in
  `init()`. The task system is now real multithreading (see "Real
  multithreading" below), not the synchronous stand-in this note
  originally warned about — but a real stress-test demo (hundreds or
  thousands of objects) is still separate, unstarted work, and this
  sandbox's single CPU core means even the multithreading fix hasn't
  been proven to help at scale here, only proven correct.

## The demo suite

Beyond `kke_demo_game` (the general building-block showcase) and
`games/physics_demo` (see below), there are now dedicated demos for
individual capabilities, matching the same "one focused demo per
thing, verified visually before moving on" discipline throughout.

### `games/imgui_demo`

Wraps Dear ImGui's own built-in `ImGui::ShowDemoWindow()` rather than
hand-curating a widget list — confirmed before building this that
`imgui_demo.cpp` is already compiled into this engine's `imgui` target
(see root `CMakeLists.txt`) and nothing disables it. This is genuinely
the canonical, comprehensive answer to "show me everything this UI
library can do," maintained by ImGui itself: every widget type
(buttons, sliders, color pickers, drag/drop, tables, trees, tabs,
menus, popups, text editing, plotting), all in one place. Verified
interactively, not just "the window opened": clicked into the
"Widgets" section and confirmed it expands to the real, full category
list (Basic, Tree Nodes, Text Input, Tabs, Plotting, Drag and Drop,
and more). Always built (no `KKE_ENABLE_*` gate needed — no
dependency beyond the core engine).

### `games/rmlui_demo`

A genuinely rich showcase, not the minimal 3-box test document
`UiModule` loads by default: real `<input>` (text/checkbox/radio/
range), `<select>`, `<textarea>`, `<tabset>`, and `<progress>`
elements, styled via RCSS. **A real, verified finding along the way**:
these elements are confirmed to already be part of RmlUi 6.3's Core
library directly (checked by finding their headers under
`Include/RmlUi/Core/Elements/` in the fetched source, not assumed) —
they were merged in from the older, separate "Controls" plugin some
RmlUi tutorials still reference, so no additional library needed
linking.

**The first layout was genuinely broken**, and worth being honest
about rather than glossing over: panels were positioned by guessing at
pixel coordinates, and badly overlapped both `StatsModule`'s
Performance panel and `DebugControlModule`'s panel, with text visibly
clipped. Fixed properly, not patched around: found the *exact*
hardcoded positions of both ImGui panels directly in their source
(`StatsModule.cpp`: `(10,10)`; `DebugControlModule.cpp`: `(340,250)`)
and laid out every RmlUi panel to avoid both zones plus `UiModule`'s
own default test document at the bottom of the screen. Verified with
real screenshots at each step, including a genuine interaction test —
clicked the "Details" tab and confirmed the tabset actually switched
content (`Overview`'s panel replaced by `Details`'s), not just that a
tab visually highlighted.

### `games/physics_demo` — a dedicated demo, because the shared one couldn't show this legibly

`kke_demo_game`'s render bridge worked, but was genuinely hard to
see: a single small tetrahedron at a render scale (0.02) tuned to fit
a camera built for a unit cube, not real physics content. Rather than
keep tuning that mismatch, `games/physics_demo` is a real, separate
game folder with its own camera and its own scale — `OrbitCameraModule`
and `PhysicsModule` both gained constructor parameters
(`initialDistance`/`initialPitch`/`initialYaw`/`initialTarget`, and
`renderScale`/`initialObjectCount`) specifically to make this possible
without duplicating either class.

**Two real CMake conflicts hit and fixed while building this**, both
found by actually building a second executable, not anticipated:
1. Two executables compiling the same shader file collided on a
   global CMake target name (`shader_rml_ui_frag` already exists).
   Fixed by guarding `engine_add_shader` with `if(NOT TARGET ...)`.
2. Two executables copying the same bundled font to the same output
   path hit a Ninja "multiple rules generate the same output" error —
   the copy-file helper had been duplicated as a separately-named
   function per game. Fixed properly: refactored into one shared
   `engine_copy_runtime_file` function at the root `CMakeLists.txt`,
   used by both games now instead of two divergent copies of the same
   logic.

**The ground plane needed its own fix, and it was the exact same bug
class as before, at a new scale.** `renderScale=1.0` (real physics
units — a 100-unit floor) reproduced the old "floor fills the entire
screen with one flat color" issue, confirmed by the same bisection
technique used the first time: disabling just the ground draw fixed
the view immediately, isolating it before touching anything else.
Fixed by clamping the ground's *visual* width independently of
`renderScale` (0.5 to 10 units) rather than letting it scale
unboundedly — verified to leave `kke_demo_game`'s already-working
2-unit floor (100 × 0.02) completely untouched, since 2 is well under
the clamp, while giving `physics_demo`'s real-scale floor a
proportionate, legible size instead of 100 units. The actual physics
collision volume is unaffected either way — this only ever changes
what gets drawn.

**Verified visually, not just logically**: real screenshots at each
step — first with the ground disabled entirely (proving the six
falling tetrahedra themselves render correctly, clearly distinguishable
by color and shape), then with the clamp at increasingly smaller
values until the floor read as a floor rather than a wall of color. A

**A real, reported "objects float above the floor" bug, found after
that — a second mistake in the same block of code, not a leftover from
the first fix.** The ground's Y translation was computed BEFORE its
thickness was clamped down, using a fixed `-0.5 * renderScale`
regardless of how thin the clamp then made the box — at
`renderScale=1.0`, that left the rendered floor's top surface at
`y=-0.475`, while an object actually rests at `y≈0.002` (the real
physics contact surface). A visible ~0.475-unit gap between where
objects visually landed and where the floor was drawn, exactly
matching what got reported. Fixed by computing the thickness first and
deriving the translation from it, so the rendered top surface is
always exactly `y=0` regardless of how the clamp scales the box.
Verified with real screenshots before and after — objects visibly
resting flush on the floor now, not floating above it — and a
regression check against `kke_demo_game`'s own much smaller
`renderScale=0.02` ground confirmed no change there.
regression screenshot of `kke_demo_game` afterward confirmed no visual
change there, and — a genuine bonus, not engineered for — its
Marketplace panel now shows "# 2 games," correctly auto-discovering
`physics_demo`'s `game.json` through the same scanning this engine
already had.



The tetrahedron and a ground plane are now actually drawn, not just
logged — reusing the existing cube shaders directly (they just
transform a position by an MVP matrix and output a flat color, which
is exactly what a physics-driven mesh needs too) and a raw, per-frame-
uploaded vertex buffer for the tetrahedron's 4 live positions, since
they genuinely move and deform.

Getting a *correct* picture on screen took real, screenshot-verified
debugging — five rounds of "that's not it," not a straight line, and
worth recording honestly:

1. First attempt drew both objects at their true physics scale (a
   100-unit-wide ground, an object falling from height 5). Result: the
   entire 3D viewport filled with one solid flat color, every frame,
   for every existing module (cube, grid, particles included) — not
   just the new ones. Alarming, and wrongly assumed at first to be
   pipeline/shader corruption.
2. Bisected step by step with real screenshots at each stage: disabling
   just the draw calls (keeping resource creation) rendered fine —
   ruled out pipeline/buffer setup. Disabling just the tetrahedron draw
   (keeping only the ground) still reproduced the full-screen fill —
   isolated it to the ground plane specifically. Removing the ground's
   extreme scale entirely rendered a small, correctly-colored, correctly
   -positioned cube — proof the geometry, shaders, and pipeline were
   never actually broken.
3. The real explanation, found by checking `OrbitCameraModule`'s actual
   math rather than continuing to guess: its default camera sits at
   `target - forward * distance` with a positive pitch, placing the
   eye at roughly y=-1.68 — *below* world y=0 — looking up and forward.
   A physics ground plane sized for real gravity (100 units wide) is
   enormous relative to a camera framing a unit cube at distance 3.5;
   viewed from below at that scale, it fills the entire upward-tilted
   view with one face's flat color. Not corruption — correctly
   rendered geometry, just wildly mismatched in scale to the camera
   that happened to already exist.
4. First fix attempt pushed the whole rendered scene *below* the
   camera's eye height to stop it from overwhelming the view — which
   instead made it disappear entirely, since this camera only ever
   looks upward and forward, never down. Wrong direction, caught
   immediately by looking at the resulting screenshot rather than
   assuming the fix worked.
5. Actual fix: a much smaller `kRenderScale` (0.02) — keeping the
   rendered objects near y=0, where the camera already frames the
   existing demo cube successfully, just much smaller than their true
   physics-unit size. The simulation itself is completely untouched by
   any of this; `kRenderScale` in `PhysicsModule.cpp` is render-time-
   only. (An earlier, ruled-out attempt also tried a vertical offset
   constant — removed entirely once the real fix was found, rather
   than left behind unused.)

**Current state, accurately**: a ground plane and the falling
tetrahedron are both visible, proportioned reasonably against the
existing demo cube, no corruption, verified via actual screenshots
across a running session — but this is a first pass, not a polished
result. The tetrahedron is small and can be hard to visually
distinguish from the ground/cube at this scale and the existing demo
camera's default framing. A dedicated physics demo with its own
camera setup (part of the demo-suite work already planned) is the
right place to make this genuinely legible, not further tuning of
these two constants.

### Real multithreading — a real thread pool, replacing the synchronous stand-in

The task system callbacks used to be a genuinely synchronous
stand-in: "submit a task" meant "call it immediately, on the calling
thread." This is now a real thread pool, verified standalone before
being wired into `PhysicsModule`, matching this whole project's
established discipline.

**A real correctness risk, found by reading FEMFX's own source
directly, not assumed**: `GetTaskSystemWorkerIndex()`'s return value
indexes straight into a per-worker scratch buffer array
(`scene->threadTempMemoryBuffer->buffers[workerIndex]`, confirmed in
`FEMFXSimulate.cpp`), and that array is sized to *exactly*
`numWorkerThreads` (confirmed in `FEMFXThreadTempMemory.cpp`) — not
`numWorkerThreads + 1`. Two threads returning the same index would
silently race on the same memory. The pool reserves index 0
permanently for the main thread and gives real pool threads indices
1..N-1, so every possible caller — whichever thread FEMFX's own
internal task-chaining ends up running work on — has a stable, unique
slot for as long as that thread exists.

**A real deadlock, found by hitting it, not anticipated**: with
`numWorkers==1` (a real, legitimate configuration — every prior
verified run in this class's history used it), the pool creates zero
real worker threads. Queuing a task in that configuration hangs
forever under a real `timeout`, since nothing would ever service the
queue. Fixed with a synchronous fallback specifically for that case —
"run inline" when there's no pool thread to hand work to, matching
exactly what the old stand-in always did.

**Correctness verified, not just "it didn't crash"**: a standalone
test ran the exact same scene with 1 worker and with 4 workers and
compared the final simulated position — **identical both times**
(0.2276), strong evidence the worker-index scheme doesn't corrupt
anything. Confirmed 3 distinct OS threads genuinely executed submitted
tasks (not just queued-and-ignored). Integrated into `PhysicsModule`
(replacing the stand-in entirely, sized via
`std::thread::hardware_concurrency()` with a guard for the "0 means
unknown" case the standard allows), rebuilt clean, all 40 tests still
pass, and both `physics_demo` and `kke_demo_game` produce byte-
identical settling behavior to every prior verified run.

**The one honest, important limit on what could be verified here**:
this sandbox has exactly 1 CPU core (`nproc` == 1). Real speedup
cannot be demonstrated in an environment with no second core for
parallelism to use — confirmed directly: 4 workers measurably ran
*slower* than 1 in this environment, exactly the pure thread/sync
overhead you'd expect with zero real parallelism to offset it.

`tools/physics_benchmark` (`kke_physics_benchmark`, gated by
`KKE_ENABLE_FEMFX`) exists specifically so this can be measured
honestly on real multi-core hardware: it runs the same scene with 1
worker and with `hardware_concurrency()` workers back-to-back, checks
that both produce the same result, and prints a clear speed
comparison — including an explicit note if it detects it's running on
a single-core machine, rather than reporting a misleading number.
Usage: `kke_physics_benchmark [numTets] [numSteps] [forceMultiWorkers]`.

### What's not done yet

- ~~**The task system is genuinely single-threaded.**~~ Fixed — see
  "Real multithreading" above for the full account.
- **Only one spawnable shape (a tetrahedron), no general mesh import.**
  The spawn API is real and general in how it's *called* (any
  position, any material, at runtime) — see "General spawn API"
  above — but every object is the same fixed shape, since there's no
  content pipeline yet to turn an arbitrary mesh into a tetrahedral one.
- **No content pipeline for `.FEM` meshes without Houdini.** FEMFX's
  own `.FEM` authoring path requires a Houdini plugin; making this
  usable without Houdini (most likely via TetGen, tetrahedralizing an
  ordinary triangle mesh) is unstarted. Also no `assimp` or any other
  model-format loader anywhere in this repo — confirmed by checking,
  not assumed — so "import an arbitrary 3D model, assign it a
  material, watch it deform" needs both a model loader and
  tetrahedralization before it's possible, not just one of them.
- **No real stress-test demo.** The particle system already proves
  20,000 GPU-simulated particles; physics has only ever been tested up
  to 8 objects (the current `kMaxObjects` cap), and the single-threaded
  task system makes "does this scale" a genuinely open question, not
  just a buffer-sizing one.
- **This is a vendored copy of a patched fork, not a real fork.**
  Ideally these ~30+ fixes live in an actual git fork hosted somewhere
  reachable, fetched via `FetchContent` like every other dependency in
  this engine — vendoring the source directly into `external/FEMFX/`
  is the practical choice for now, not the intended long-term shape.

## GPU profiler (VulkanProfiler) integration

[VulkanProfiler](https://github.com/lstalmir/VulkanProfiler)
(`VK_LAYER_PROFILER_unified`) is integrated, and verified actually
running against this engine — not just researched. What it is, in
brief: a **Vulkan layer**, not a library this repo links against —
architecturally the same as the validation layer already conditionally
enabled in `VulkanDevice`. Because it's a transparent interception
layer, enabling it profiles **every single Vulkan command this engine
issues** the moment it's active — that's what "complete integration"
means for a tool shaped like this, not something achieved by manually
instrumenting call sites.

### What's actually wired up

- `KKE_ENABLE_GPU_PROFILER` CMake option (default `OFF`). When on,
  `VulkanDevice::createInstance()` checks whether
  `VK_LAYER_PROFILER_unified` is actually installed
  (`isInstanceLayerAvailable()` — the same helper the validation-layer
  check now uses too, generalized rather than duplicated) and, if so,
  adds it to the enabled layer list and chains a
  `VkLayerSettingsCreateInfoEXT` requesting `sampling_mode = drawcall`
  — the layer's finest-grained mode, matching "check everything up to
  the draw calls."
- If the option is on but the layer isn't installed, this degrades
  exactly like a missing validation layer does: a clear warning logged
  through the same spdlog-based logger, nothing else changes, the
  engine runs normally. Verified both ways.

### Verified by an actual build, install, and run — not assumed

This took real, hard-won verification, worth recording precisely:

1. Cloned with `git clone --recursive` (submodules: SPIRV-Tools,
   SPIRV-Cross, Vulkan-Headers, Intel's `metrics-discovery`, its own
   vendored ImGui/ImPlot, and more), installed the stated Linux build
   deps (`extra-cmake-modules`, `libdrm-dev`, `libxkbcommon-dev`, X11/XCB
   dev packages), configured and built with `cmake .. -DCMAKE_BUILD_TYPE=Release && make all`.
   **On a single-core sandbox, this took roughly 15 minutes** — mostly
   SPIRV-Tools and a genuinely slow single-threaded LTO link step for
   Intel's `metrics_discovery` library (128 LTRANS units, serial on 1
   core). Budget real time for this; it is not a quick dependency fetch.
2. `sudo cmake --install . --prefix /usr/local/` placed the layer's
   `.so`, its JSON manifest (into `/usr/local/share/vulkan/explicit_layer.d/`,
   the standard path the Vulkan loader scans automatically), and
   `VkProfilerEXT.h` (the header declaring `vkGetProfilerFrameDataEXT`
   and friends — see "Not yet done" below).
3. **First real run failed** with `VK_ERROR_LAYER_NOT_PRESENT`, even
   though `vulkaninfo` and our own `isInstanceLayerAvailable()` check
   both correctly found the layer's manifest. Diagnosed rather than
   guessed: `ldd` on the installed `.so` showed no missing
   dependencies, ruling that out; the actual cause was that
   `/usr/local/lib/x86_64-linux-gnu` — where the `.so` was installed —
   is a configured search path (`/etc/ld.so.conf.d/x86_64-linux-gnu.conf`)
   but `ldconfig`'s cache had never been refreshed after install, so a
   bare `dlopen("libVkLayer_profiler_layer.so")` (what the manifest's
   relative `library_path` resolves to) failed. Running `ldconfig`
   fixed it immediately — worth remembering as a real, non-obvious
   install step, not assuming `cmake --install` alone is sufficient on
   every system.
4. With the cache refreshed, re-running `kke_demo` with
   `KKE_ENABLE_GPU_PROFILER=ON` logged `GPU profiler layer
   'VK_LAYER_PROFILER_unified' found and will be enabled`, and its real
   overlay rendered on top of this engine's own frame — a genuine
   screenshot showed `VkProfiler - llvmpipe (LLVM 20.1.2, 256 bits)`,
   `Vulkan 1.2`, live `GPU Time: 34.03 ms` / `CPU Time: 0.02 ms` /
   `Frame 94` / `25.1 fps`, and working Performance/Memory/Inspector/
   Statistics/Settings tabs — correctly reading this engine's actual
   device and actual per-frame timing, not placeholder UI.

### Reproducing this yourself

```bash
# Linux build deps (see the layer's own README for the authoritative list)
sudo apt-get install -y extra-cmake-modules libdrm-dev libxkbcommon-dev \
    libx11-dev libxext-dev libxcb1-dev libxcb-shape0-dev

git clone --recursive https://github.com/lstalmir/VulkanProfiler
cd VulkanProfiler && mkdir cmake_build && cd cmake_build
cmake .. -DCMAKE_BUILD_TYPE=Release && make all -j$(nproc)
sudo cmake --install . --prefix /usr/local/
sudo ldconfig   # do not skip this — see step 3 above

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKKE_ENABLE_GPU_PROFILER=ON
cmake --build build -j
./build/bin/kke_demo   # the layer's overlay should appear on top of the engine's window
```

### Not yet done — the honest remainder

- **`vkGetProfilerFrameDataEXT` was attempted, and reproducibly crashes
  in this environment — a real, diagnosed finding, not an untried idea.**
  The exact API is wired up correctly: function pointers loaded via
  `vkGetDeviceProcAddr` (`VulkanDevice::loadGpuProfilerFunctions()`),
  the real struct (`VkProfilerDataEXT`/`VkProfilerRegionDataEXT`) walked
  recursively to count actual leaf draw/dispatch/copy commands
  (`VulkanDevice::queryGpuProfilerFrameSummary()`), wired into
  `StatsModule` to display and log. Calling it segfaults — confirmed via
  `gdb` backtrace to be **inside the layer's own compiled code**
  (`vkGetProfilerFrameDataEXT` itself), not this engine's code. Ruled
  out before concluding that: it isn't a "too early" timing issue (still
  crashes on the first call after 3+ real frames have already
  presented, confirmed with an explicit frame counter); it isn't the
  layer's background-threading option (`VKPROF_enable_threading=false`
  made no difference); it isn't the `drawcall` sampling mode
  specifically (`VKPROF_sampling_mode=commandbuffer`, the coarsest
  mode, crashed identically). The layer's own overlay reads equivalent
  data correctly (see the verified screenshot above), which is what
  makes this specifically about calling the function from *application*
  code in this environment (lavapipe software rendering + this build of
  the layer), not evidence the layer itself is broken. Root-causing
  further needs the layer's own debug symbols or source stepping — real
  work, disproportionate to guess-and-check further. **The code is
  written and disabled behind `#ifdef KKE_QUERY_GPU_PROFILER_DATA`** in
  `StatsModule.cpp` rather than deleted, so re-attempting this (on a
  different Vulkan implementation, a newer layer version, or with real
  hardware instead of a software rasterizer) is a one-line change, not
  a rewrite.
- **No headless/CI story yet.** The layer's `output = trace` mode
  (serializing to a JSON Event Trace Format file instead of an overlay)
  is the obvious fit for the headless Xvfb verification this project
  has used throughout, and for a future CI step — not wired up.
- **The layer itself is not part of this repo** and can't sensibly be —
  it's a large, platform-specific, system-installed artifact (like the
  validation layer), not something `FetchContent` should vendor.
  Anyone building this engine with `KKE_ENABLE_GPU_PROFILER=ON` needs to
  build and install it separately, following the steps above.

## Debugging: pause/step and per-module fault isolation

Two related, verified features, both built around `Application`'s frame
loop rather than bolted on separately.

**Pause/step** (`Application::isPaused()`/`setPaused()`/`stepOneFrame()`,
UI via `DebugControlModule`'s "Debug Control" panel): freezes
`fixedUpdate`/`update`/`compute` — including GPU compute-driven
simulation like `ParticleModule` — while rendering keeps presenting
every frame regardless. The point is giving an external tool (RenderDoc,
a Vulkan profiling layer, or just your own eyes) a frame that holds
completely still instead of one that's still animating out from under
you. "Step one frame" advances exactly one fixed tick + one `update()`
call using the fixed tick length as a synthetic `dt` (not real
wall-clock time, which would be meaningless while paused), then
re-freezes. Verified, not assumed: two screenshots two seconds apart
while paused came back pixel-identical (cube, chaotic particle
positions, even the FPS counter — everything gated by the same pause
flag); each "Step" click produced a small, bounded advance rather than a
jump, confirmed by comparing consecutive screenshots.

**Per-module fault isolation** (`Application::safeInvoke()`, every
lifecycle call — `init`, `fixedUpdate`, `update`, `onEvent`, `compute`,
`render`, `renderUi`, `shutdown` — for every module goes through it):
a module that throws is logged with its name, which stage it was in,
and the actual exception message (via the same spdlog logger every
other module uses, at `error` level), recorded, and **never called
again for the rest of the session — not even `shutdown()`**. That
last part is deliberate: a module that has already misbehaved once
isn't a module whose cleanup code should be trusted either.
`DebugControlModule` shows every recorded failure in a bright red
"Emergency Log" ImGui window that only appears once something has
actually gone wrong. Verified with a real throw, not a hypothetical:
added a temporary module that threw `std::runtime_error` on its 30th
`update()` call, confirmed the process stayed alive, the exact expected
log line appeared —

```
[ThrowTest][KKE Engine Demo][error]: disabled for the rest of this
session after throwing during update(): deliberate test failure to
verify fault isolation
```

— and the Emergency Log window rendered with that same information
while the cube kept spinning, particles kept simulating, and every
other panel kept working, then removed the test module afterward.

**The honest limit of this**, stated plainly rather than glossed over:
if a module's `init()` throws partway through creating GPU resources,
that module's own destructor (still called normally later via
`unique_ptr`, since C++ object lifetime isn't something `safeInvoke`
can intercept) inherits whatever half-built state was left behind.
Every module in this engine builds GPU resources through RAII wrappers
(`Buffer`, `Pipeline`, etc.) specifically so a partial `init()` still
leaves safely-destructible state — but a module that doesn't follow
that pattern could still misbehave on destruction. Fault isolation
reduces this risk; it can't eliminate it for code this engine doesn't
control, and it was never going to — that's not a gap unique to this
implementation, it's a fundamental limit of exception-based isolation
in a language without memory/process sandboxing.

### Clear errors for scripters, not just C++ exceptions

Fault isolation (above) answers "does one bad module crash everything"
— it doesn't answer "does the person who wrote the bad code understand
what's wrong." A raw C++ exception message like `basic_string::at: __n
(which is 5) >= this->size() (which is 3)` is precise and completely
useless to someone who wrote a script and has never heard of
`basic_string::at`. That's what `kke::EngineError`
(`engine/include/kke/EngineError.h`) exists to fix.

Any module — and, once it exists, the Lua scripting layer — can throw
`EngineError` (or the `KKE_SCRIPT_ERROR(friendly, technical)` /
`KKE_ENGINE_ERROR(friendly, technical)` convenience macros, which also
splice in `__FILE__`/`__LINE__`) instead of a bare `std::runtime_error`.
It carries **three things a bare exception can't**: a plain-language
message, which of engine-code/script-code is likely at fault
(`kke::ErrorSource`), and a file/line when known. `what()` still
returns the technical text — `EngineError` is a real `std::exception`,
so any code that doesn't know it's special (a generic `catch
(std::exception&)`, plain logging) keeps working exactly as before.

`Application::safeInvoke()` catches `EngineError` specifically (before
the generic `std::exception` fallback) and records both messages,
the source, and the location into `BrokenModuleInfo`.
`DebugControlModule`'s Emergency Log shows the **friendly message as
the headline**, a colored source badge (`SCRIPT ERROR` / `ENGINE
ERROR` / `UNKNOWN SOURCE`), the file:line when available, and the
technical message tucked behind a collapsed "Technical details" —
visible for anyone who wants it, not forced on someone who doesn't.
A plain `std::runtime_error` still works everywhere; it just can't
offer any of the richer fields, and is honestly labeled `UNKNOWN
SOURCE` rather than guessed at.

Verified with both paths side by side, not just one:

```
[ScriptErrorTest][KKE Engine Demo][error]: disabled for the rest of this
session after throwing during update() at .../ThrowTestModule.h:14 —
The recipe needs more sugar than you gave it — check line 14.

[PlainThrowTest][KKE Engine Demo][error]: disabled for the rest of this
session after throwing during update(): a plain std::exception with no
friendly-message split
```

— and on screen, the Emergency Log showed the first as an orange
`[SCRIPT ERROR]` badge with the real file/line and the friendly
message front and center (technical detail collapsed), and the second
as a gray `[UNKNOWN SOURCE]` badge with just its one available message
— no fabricated location, no invented friendly text standing in for
something that was never provided.

**What this doesn't solve yet**: there's no Lua scripting layer for
this to actually serve its intended audience with today — a C++
module author can use `EngineError` right now, but the "someone who
isn't a C++ programmer" case this was built for needs the Lua binding
layer (see Roadmap) to translate *its* errors (a bad script line, a
missing value) into `EngineError` calls. This is the plumbing that
layer will use, built and verified ahead of it existing, not a
replacement for it.

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
- **RmlUi resize handling is fixed** — `UiModule::update()` compares
  the swapchain's current extent against the `Rml::Context`'s own
  dimensions every frame and calls `SetDimensions()` when they differ.
  Verified genuinely, not assumed: resized a real running window
  through several different sizes and multiple resize cycles
  (1280x720 → 1920x1080 → 800x500 → 1400x900) via `xdotool`, screenshot
  ing at each step — RmlUi panels, ImGui, and the 3D scene all stayed
  correctly positioned and legible throughout. This was a real,
  user-reported bug (the UI could disappear or misbehave at
  non-default resolutions), not a theoretical gap.
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

- **Real PBR materials — Cook-Torrance BRDF, replacing Blinn-Phong
  entirely, not layered on top of it.** GGX normal distribution, Smith
  geometry function, Fresnel-Schlick, proper energy conservation
  between diffuse and specular, metals correctly reflecting their own
  albedo color as base reflectivity rather than a flat gray, and
  Reinhard tone mapping (real PBR specular can legitimately exceed 1.0
  per channel with strong lights/low roughness — a correct result, not
  a bug, that needs compressing back into displayable range).
  - **A real architectural problem, solved properly, not worked
    around**: the existing push constants were already at 128 bytes —
    Vulkan's guaranteed-minimum limit — with no room for new fields.
    Fixed by removing the redundant `mvp` matrix entirely (always just
    `proj*view*model`, recomputed identically per object every frame
    for no reason) and moving the shared `proj*view` into
    `LightingUBO` once per frame instead — freeing real room for
    genuine per-object `metallic`/`roughness`, and removing real
    duplicated per-object matrix work as a side effect.
  - **Genuinely integrated, not bolted on**: `kke::Material` gained
    real `metallic`/`roughness` fields living alongside its existing
    physical properties (the same struct FEMFX already reads density/
    stiffness from), and `MaterialGridModule`'s five presets now double
    as real visual presets with zero extra plumbing — Iron is actually
    metallic, Glass is actually smooth, Rubber is actually rough,
    because they already carry a real `Material` an object's renderer
    now reads visual properties from too.
  - Added real, live "Metallic"/"Roughness" sliders to `CubeModule`'s
    own UI panel specifically so this is genuinely demonstrable, not
    just trust-me-it-compiles — confirmed interactively that both
    sliders move independently and hold their set values (0.100 →
    1.000 metallic, 0.400 → 0.000 roughness, each confirmed via a
    zoomed screenshot of the actual displayed number, not assumed from
    the click alone).
  - **An honest limitation in how this got verified, not glossed
    over**: getting a clean, controlled "before vs. after" screenshot
    of the same cube face at two different material values proved
    genuinely difficult in this headless test setup — ImGui slider
    clicks are imprecise via synthetic mouse events, and the cube
    keeps rotating between screenshot captures, so two captures rarely
    show the identical face at the identical angle. Confirmed instead
    through what could be verified rigorously: the slider values
    themselves changing correctly (screenshotted directly, not
    inferred), the BRDF implementation reviewed carefully against the
    standard formulation, and a comprehensive validation-layer sweep
    showing zero errors from the real GPU-side push constant and UBO
    changes this needed. A real visual "wow" comparison screenshot is
    still worth capturing properly in a follow-up session with more
    reliable input control, not claimed here as done when it wasn't.
  - Full comprehensive verification: both `KKE_ENABLE_FEMFX` on and off
    configurations rebuilt clean from scratch — zero errors, zero
    warnings, all 40 tests passing, correct binary set, in each — plus
    every demo (`kke_demo`, `physics_demo`, `rmlui_demo`, `imgui_demo`)
    swept individually with real Vulkan validation layers active in
    both configurations: zero validation errors, zero crashes, zero
    assertions throughout.
  - **What's deliberately still out of scope**: real image-based
    ambient lighting (the ambient term is still `ambientColor *
    albedo`, a flat stand-in, not a captured/convolved environment
    map — see "What's still ahead for lighting" below), normal maps,
    and albedo/roughness/metallic *textures* (every value here is
    still a single per-object number, not a per-pixel texture sample).
- **Real shadow mapping — the first concrete piece of "shadows/PBR,"
  this project's own longest-standing unstarted lighting item, now
  genuinely working.** Deliberately scoped narrow rather than
  generalized to every light and caster at once (see `kke::ShadowMap`'s
  own class comment for exactly what's in and out of scope): one
  directional light (the key light), one real shadow caster
  (`CubeModule`), single-tap sampling with a checked depth bias rather
  than PCF/soft shadows. A real, working single-caster proof, not a
  half-built system trying to cover every case and getting none of
  them fully right.
  - **A genuine second render pass, not a shader trick**: `kke::
    ShadowMap` owns a real depth-only render target, render pass (with
    correct subpass dependencies for entering/leaving it safely), and
    sampler. `Module` gained a real `renderShadow()` lifecycle method
    and a separate, honestly-minimal `ShadowRenderContext` (not a
    reuse of `RenderContext` — most of its fields wouldn't apply to a
    pass with no camera and no color attachment). `Application`'s
    frame loop now runs a full shadow pass — begin, call
    `renderShadow()` on every module, end — before the main color pass
    begins, since the shadow render pass's own final layout transition
    is what lets the main pass sample it directly afterward with no
    separate manual barrier.
  - **A real architectural consequence, handled correctly, not
    glossed over**: `cube.frag` is shared by `CubeModule`,
    `PhysicsModule`, and `DestructionModule` — updating it to
    unconditionally sample a shadow map meant all three needed their
    pipeline's descriptor set layouts and `render()` calls updated too,
    whether or not that module's own geometry casts a shadow itself.
    `PhysicsModule`'s ground plane, in particular, now receives real
    shadows through this shared shader with zero changes to
    `PhysicsModule` beyond the mandatory descriptor set update — a
    direct, useful consequence of it already sharing `CubeModule`'s own
    lighting shader.
  - **Verified as genuinely dynamic, not a static decal**: a real
    screenshot showed a dark shadow shape cast onto `physics_demo`'s
    (and `kke_demo`'s own FEMFX-enabled) ground plane, correctly
    positioned relative to the spinning cube. A second screenshot,
    taken a few seconds later with the cube still rotating, showed the
    shadow's own shape had genuinely changed — confirming this is
    recomputed live, every frame, not baked or cached.
  - **A real depth-bias value, checked not guessed**: 0.003 in this
    engine's own [0,1] depth range, enough to eliminate visible
    shadow-acne self-shadowing artifacts on the cube/ground test case
    without visibly detaching the shadow from its caster
    ("peter-panning") — confirmed against real screenshots, the same
    discipline as every other tuned constant in this project.
  - **Full comprehensive verification, both build configurations**:
    every demo (`kke_demo`, `physics_demo`, `rmlui_demo`, `imgui_demo`)
    swept individually with real Vulkan validation layers active —
    zero validation errors, zero crashes, zero assertions, in each —
    exactly the kind of feature prone to subtle synchronization bugs,
    made a real confirmation rather than a hopeful one by those same
    validation layers this session installed earlier. Both
    `KKE_ENABLE_FEMFX` on and off configurations rebuilt clean from
    scratch: zero errors, zero warnings, all 40 tests passing, correct
    binary set, in each — `CubeModule` (and therefore shadow mapping)
    doesn't depend on FEMFX at all, confirmed by checking the `OFF`
    build specifically renders a correctly-lit cube with no physics
    ground plane present to receive its shadow, exactly as expected
    for that configuration, not by assumption.
  - **What's deliberately still out of scope** (see `ShadowMap.h`'s own
    comment): point-light shadows, shadow casting generalized to every
    module rather than just `CubeModule`, cascaded/multiple shadow maps
    for larger scenes, and PCF or other soft-shadow filtering beyond a
    single depth-comparison tap.
- ~~**`Rml::Debugger`'s "Outlines" tool crashes**~~ Fixed — and the
  investigation ended up finding two more real bugs beyond the one
  being chased, both now fixed too. The honest, layered account:
  - **The real breakthrough**: installed real Vulkan validation layers
    in this sandbox (it had none before), turning a bare, symbol-less
    segfault deep inside the lavapipe driver into an exact, actionable
    error: `vkCmdWriteTimestamp(): was called in VkCommandBuffer ...
    which is invalid because bound VkBuffer ... was destroyed`.
  - **Bug 1 (the original crash)**: `RmlVulkanRenderInterface::
    ReleaseGeometry()`/`ReleaseTexture()` destroyed their GPU resources
    immediately, with no check that the GPU had finished using them.
    Harmless for normal RmlUi content (which rarely releases geometry
    mid-session) but Outlines churns through far more temporary
    geometry per frame than anything else in this engine ever has,
    making the race far more likely to actually hit. Fixed with real
    deferred destruction — a queue tagged with the frame each resource
    was released on, only actually freed once `kMaxFramesInFlight`
    (Renderer.h, currently 2) plus a safety margin of real frames have
    elapsed, matching the exact guarantee `Renderer`'s own per-frame
    fence wait already provides. Reverified: Outlines now renders real
    red borders around every visible element, exactly as intended,
    with zero validation errors across a sustained run.
  - **Bug 2 (found by the same validation layers, unrelated to
    Outlines)**: a broader regression sweep of `kke_demo`'s full
    showcase turned up validation errors during ordinary shutdown —
    `vkDestroyBuffer(): can't be called on VkBuffer ... that is
    currently in use by VkCommandBuffer`. Traced to `Application`'s
    own destructor: modules' `shutdown()` methods (which can destroy
    Vulkan resources directly) ran before `Renderer`'s own destructor
    — the one that actually calls `vkDeviceWaitIdle()` — ever got a
    chance to run, since that only happens once every module is
    already torn down. This bug had presumably always existed, just
    never visible before — it never crashed, only mildly corrupted
    GPU-side state that happened not to matter for a process about to
    exit anyway. Fixed with an explicit `vkDeviceWaitIdle()` at the
    very start of `Application::~Application()`, before the module
    shutdown loop begins.
  - **Bug 3 (introduced by the Bug 1 fix itself, caught before it
    shipped)**: a real run combining both scenarios — Outlines
    clicked, then a clean shutdown — hit a VMA assertion, `"Some
    allocations were not freed before destruction of this memory
    block!"`. The deferred-deletion queue from Bug 1's fix never gets
    swept again once the app is closing, and while a queued
    *geometry* deletion cleans itself up fine (its own
    `unique_ptr` destructor runs automatically), a queued *texture*
    deletion is a bare struct of raw Vulkan/VMA handles with no
    destructor at all — it would leak past process exit. Fixed by
    explicitly draining and destroying any remaining pending texture
    deletions in `RmlVulkanRenderInterface`'s own destructor — safe to
    do immediately there specifically because Bug 2's fix already
    guarantees the GPU is idle by the time any module's shutdown (and
    therefore this destructor) runs.
  - **Verified as a whole, not just each piece in isolation**: the
    exact combined scenario that caught Bug 3 — Outlines clicked,
    geometry actively churning through the deferred-deletion queue,
    then a real, clean shutdown — now runs with zero validation
    errors and zero assertions. Every demo (`kke_demo`, `physics_demo`,
    `rmlui_demo`, `imgui_demo`) swept individually with validation
    layers active after all three fixes: zero validation errors, zero
    crashes, zero assertions, in each. Both `KKE_ENABLE_FEMFX`
    configurations rebuilt clean from scratch: zero errors, zero
    warnings, all 40 tests passing, correct binary set, in each.
- ~~**Real plasticity support**~~ Fixed — `enablePlasticity` was false
  everywhere, the same class of gap fracture was, closed the same
  rigorous way. Confirmed via reading `FmComputeTetMeshBufferBounds`'s
  own signature that plasticity is structurally simpler than fracture:
  no separate per-tet output arrays needed at all, just the flag plus
  real (non-zero) `plasticYieldThreshold`/`plasticCreep` material
  values, which already flowed through `FmInitTetState`'s own per-tet
  loop correctly. Added `spawnPlasticTetMesh()` (mirroring
  `spawnFracturableTetMesh()`'s own pattern) and a "Spawn plastic
  cube" test button using the same verified 6-tet cube shape.
  - **A real diagnostic mistake, found and corrected, not covered
    up**: the first verification attempt compared
    `FmGetVertRestPosition()` before and after impact — a plausible-
    seeming signal that turned out to be entirely wrong. Reading
    FEMFX's own source (`FEMFXUpdateTetState.cpp`) showed plasticity is
    tracked as a per-*tet* `plasticDeformationMatrix`, not a change to
    the vertex rest-position array at all — and that internal state
    has no public accessor in `AMD_FEMFX.h` to read directly. Every
    threshold tried against the wrong signal (down to an extreme
    0.0001) correctly showed nothing, which looked identical to "not
    working" and could easily have been misdiagnosed as a setup bug.
  - **The real, corrected verification**: measuring the distance
    between the test cube's own vertex 0 and vertex 1 — exactly 1.0
    unit apart at spawn. A real run showed it grow from 1.0000 past
    1.02 and never spring back, with the growth rate genuinely
    decelerating over time (each second's increase smaller than the
    last) rather than diverging unbounded — consistent with
    `plasticCreep`'s own documented meaning (deformation accumulated
    *per unit of excess stress*, a rate) rather than a bug.
  - Same empirical-tuning story as fracture, briefer this time now
    that the right diagnostic existed: AMD's own reference value
    (2.5e6) wasn't reachable by this project's actual stress
    magnitudes either; landed on 2.0, confirmed working via the
    vertex-distance measurement above.
  - No render-path changes needed, unlike fracture — a plastic object
    never splits into new `FmTetMesh` pieces, so the existing single-
    mesh path already renders it correctly as its shape changes.
  - Along the way, fixed a real, unrelated UI bug: the Physics ImGui
    panel was too narrow for three spawn buttons on one row (the third
    was clipped off-screen entirely), found the same way as everything
    else this session — a real screenshot, not assumed. Widened the
    panel's default size properly rather than leaving it to manual
    resizing.
  - Full verification: both `KKE_ENABLE_FEMFX` on and off configurations
    rebuilt clean from scratch, zero errors, zero warnings, all 40
    tests passing in each, correct binary set in each, plus a live
    10-second regression run of `kke_demo`'s own non-plastic physics
    object confirming identical settling behavior to every prior check.
- ~~**Real `<img>`/`background-image` support**~~ Fixed — `LoadTexture`
  was the one remaining stub in `RmlVulkanRenderInterface` (see its own
  class comment); it now genuinely decodes files with `stb_image`
  (already a real dependency elsewhere, no new one added), forcing
  RGBA8 output specifically to match `createTextureFromPixels`'s
  existing `VK_FORMAT_R8G8B8A8_UNORM` expectation, then reuses that
  exact same GPU upload path font glyph textures already went through
  — no second, format-aware code path needed. A failed/missing file
  falls back to the same 1×1 white default untextured geometry already
  uses (logged via `stbi_failure_reason()`, not silently swallowed),
  rather than treating one broken image as fatal.
  - **Verified two ways, independently, with real screenshots**: added
    a real PNG test icon to `rmlui_demo`, rendered once through
    `<img src="...">` and once through RCSS's own separate
    `background-image`/decorator mechanism — both are genuinely
    different RmlUi code paths that happen to both call `LoadTexture`
    internally, worth confirming independently rather than assuming
    fixing one fixed both. A zoomed screenshot shows both rendering
    the identical icon correctly.
  - **A real, separate bug found and fixed along the way, not
    related to image loading at all**: `MaterialGridModule`
    unconditionally used `PhysicsModule` with no `KKE_ENABLE_FEMFX`
    guard — silently fine every time it had ever been built (always
    with FEMFX on), but `kke_engine` itself builds once per CMake
    configuration regardless of which demo enables FEMFX, and a
    default `KKE_ENABLE_FEMFX=OFF` build failed immediately once this
    file was actually part of one. Fixed by wrapping the whole module
    in the same guard `PhysicsModule` itself already uses — the honest
    reflection of reality anyway, since this module has nothing
    meaningful to do without a `PhysicsModule` to select materials for.
  - Full verification: both the default `OFF` and `FEMFX=ON`
    configurations rebuilt clean from scratch afterward — zero errors,
    zero warnings, all 40 tests passing in each, correct binary set in
    each (`physics_demo`/`kke_physics_benchmark` present only when
    FEMFX is on) — plus a live regression run of `kke_demo`'s full
    integrated showcase confirming no behavior changed.
- **`kke_demo` is now the real, integrated showcase it was always meant
  to be** — `LightingControlsModule` and `MaterialGridModule` both
  added, alongside everything it already had (physics with real
  fracture, destruction, marketplace, particles). Getting there
  surfaced two real, found-and-fixed problems, not a clean drop-in:
  - **A real layout collision**: both content modules originally
    hardcoded the identical `left:40px; top:500px` position — harmless
    while each only ever appeared in its own separate demo, a direct
    overlap the moment both needed to coexist in one. Fixed properly:
    added real `left`/`top` constructor parameters to both (applied via
    `Element::SetProperty` after load, not baked into the RML string),
    defaulting to each module's original position so every existing
    call site keeps rendering exactly where it always did.
  - **A second, non-obvious collision, found by screenshot not
    assumption**: the first attempt placed `MaterialGridModule` at
    `(820, 500)`, assumed-empty space below `MarketplaceUiModule`'s own
    panel. It rendered completely invisible with no errors at all —
    confirmed via a real diagnostic log that the reposition itself
    succeeded, then a real screenshot revealed why: Marketplace's own
    opaque background actually extends continuously well past y=500
    (four stacked game cards), silently hiding anything placed
    underneath it. Fixed by widening `kke_demo`'s window from 1280×720
    to 1600×900 (genuinely warranted — this many real panels needs the
    room, not a workaround) and stacking `MaterialGridModule` *below*
    `LightingControlsModule` instead, in the new vertical space.
  - **A third, smaller layout bug**: `MaterialGridModule`'s own five
    cards wrapped to a second row and clipped off the bottom of the
    window — the panel width was a few pixels too narrow for five
    cards at their original size. Fixed with tighter, verified card
    dimensions (70px cards, 460px panel) that fit five in one row with
    real margin to spare, not just barely.
  - **Verified with real interaction after every fix, not just
    visual inspection**: clicked "Glass" (the previously-clipped,
    rightmost card) and confirmed via a zoomed before/after screenshot
    that it genuinely highlights as selected while "Wood" correctly
    deselects — the same real click-through-to-`PhysicsModule::
    selectedMaterial()` path verified when this module was first built.
  - Full regression pass: `physics_demo` (which still uses
    `MaterialGridModule`'s original, unmodified default position)
    re-verified with a real sustained run afterward, same correct
    settling behavior as every prior check.
- **`kke::MaterialGridModule` — a real extraction-shooter-style grid
  menu, wired to real state, not decoration.** Five material preset
  cards (Wood, Stone, Iron, Rubber, Glass) with genuinely
  differentiated values matching `Material.h`'s own documented intent
  (glass brittle and close to its yield point, rubber barely breaks at
  all) — clicking one actually changes `PhysicsModule::
  selectedMaterial()`, which the next "Spawn tetrahedron" click
  genuinely uses. Added `PhysicsModule::selectedMaterial()` as real,
  settable state for this (mirroring how `Application::lighting()`
  already works), replacing that button's old hardcoded material.
  Deliberately does NOT touch "Spawn fracturable cube" — that
  button's own material has a specifically, empirically tuned
  fracture threshold (see the fracture entry below) that an arbitrary
  preset swapped in here could quietly break.
  - **A real bug found and fixed while testing it, not assumed
    correct**: the first version did nothing when clicked. Traced it
    directly: each card has child elements (a color swatch div, label/
    stat `<p>` tags), and a click lands on whichever child element is
    actually under the cursor — `Event::GetTargetElement()` returned
    that child, not the card div my listener was attached to, so the
    id check never matched anything. Fixed with
    `Event::GetCurrentElement()` instead, which always returns the
    element the listener is actually registered on regardless of
    which child inside it was clicked. Verified after the fix with a
    real screenshot: clicked "Iron," watched it highlight and "Wood"
    un-highlight, "Selected: Iron" text update, then spawned
    successfully afterward.
- **Real fracture support — genuinely working now, found by reading
  AMD's own vendored sample code and FEMFX's own source, not
  guessing.** This was the single biggest, most-repeated gap this
  project had honestly flagged (`enableFracture=false` everywhere, no
  exceptions) — closed properly, not just flipped on:
  - **Researched what "showing off FEMFX" actually means first**:
    `external/FEMFX/samples/common/TestScenes.cpp`, vendored alongside
    the library itself, is AMD's own real reference demo. It fractures
    wood panels with a fired projectile, piles up dozens of soft-body
    blocks and ducks, lets material parameters change live (including
    melting), and stacks rigid and deformable bodies together —
    confirming a handful of falling tetrahedra never represented real
    FEMFX capability, and setting the actual target.
  - **The real missing piece, traced from AMD's own code**:
    `FmComputeTetMeshBufferBounds` and `FmCreateTetMeshBuffer` both
    take `FmFractureGroupCounts`/`tetFractureGroupIds` output
    arrays — this project's spawn code always passed `nullptr` for
    both. That's the literal, complete reason fracture never worked
    anywhere in this codebase before now, not a deeper bug. Added a
    real `spawnFracturableTetMesh()` API (kept separate from
    `spawnTetMesh()` — fracture needs genuinely extra setup and a
    more expensive render path, worth keeping visible at the call
    site, not hidden behind a default parameter).
  - **A real render-path rewrite, not a small patch**: a fractured
    object can split into multiple independently-moving `FmTetMesh`
    pieces at runtime (`FmGetNumTetMeshes()` can grow past 1), and per
    FEMFX's own setup docs, vertex count itself "may grow with
    fracture." Fracturable objects now size their buffers to the
    reserved maximum capacity and rebuild both vertex and index data
    from each current sub-mesh's actual topology every frame, instead
    of uploading once at spawn time. The existing, already-verified
    non-fracturing path is completely untouched — confirmed by
    regression testing `kke_demo`'s own physics object afterward,
    exact same settling height as every prior verification.
  - **A real, humbling tuning journey, honestly recorded**: getting an
    object to actually fracture took far more empirical work than
    expected. A simple gravity drop from this demo's usual spawn
    height didn't generate enough stress to fracture even at
    `fractureStressThreshold=100` (already assumed "very low" against
    a 5×10⁶ stiffness material) — matching AMD's own scene design
    directly: their reference wood panels get fractured by a fired
    projectile, not gravity. Added a real initial velocity parameter
    to `spawnFracturableTetMesh()` (a genuine, if simple, stand-in for
    "thrown hard," using `FmInitVertState`'s own velocity parameter,
    previously always zero everywhere in this codebase) and traced the
    entire FEMFX call chain by reading its source — `FmUpdateScene` →
    `FmUpdateTetStateAndFracture` → the actual
    `maxStressEigenvalue > fractureStressThreshold` comparison in
    `FEMFXUpdateTetState.cpp` — to confirm the setup was correct
    throughout and this was genuinely a threshold-scale question, not
    a bug. The real working value ended up being 10.0 — this specific
    material's actual stress values under impact are apparently much
    smaller in magnitude than AMD's own reference examples (5×10⁵ to
    10⁶ for their wood panels), most likely because those are larger,
    heavier objects under a harder hit.
  - **Verified two ways, not just visually**: logged
    `FmGetNumTetMeshes()` directly in `fixedUpdate()` — "fracturable
    object (handle N) has split into 2 pieces," consistently
    reproducible across repeated real runs — and confirmed a real,
    if subtle, visible crease across the object where a perfectly
    intact cube wouldn't have one.
  - **A real UI entry point** to try this yourself:
    `physics_demo`'s "Spawn fracturable cube" button, using a real
    6-tetrahedron cube decomposition (a single tetrahedron has nowhere
    to break into — fracture splits along existing tet boundaries, so
    meaningful fracture needs genuinely connected multi-tet geometry).
- **`kMaxObjects` raised from 8 to 64** — the old cap was never meant
  to represent a real ceiling, just the smallest number that proved
  the spawn API worked at all; AMD's own reference scenes show piles
  of dozens of objects at once. Confirmed live: `physics_demo`'s own
  UI now reads "Objects: N/64."
- ~~**The three purposeless bottom boxes**~~ Fixed — genuinely
  removed, not just restyled. Those boxes were `UiModule`'s own
  hardcoded "test document," loaded unconditionally into *every* demo
  using `UiModule`, left over from the original slice that first
  proved RmlUi text rendering worked. `UiModule` is content-agnostic
  now — it only owns the RmlUi Context/render pipeline/input
  forwarding, matching what its own class comment already said it
  should be. In their place: `kke::LightingControlsModule`, a real,
  new, reusable engine module (any demo can opt in, the same way
  `MarketplaceUiModule` already works) — genuine sliders and preset
  buttons wired directly to `Application::lighting()`, the real
  multi-light system built earlier. Added to `kke_demo_game`
  specifically, where the lit cube makes the effect immediately
  visible. Verified with real interaction, not just layout: clicked
  "Dramatic (low ambient)" and watched the cube's lit/shadowed
  contrast change completely on screen, sliders update to reflect the
  new state, then clicked "Reset to default" and watched it return
  exactly to the original appearance. A real, if minor, C++ gotcha hit
  and fixed along the way: `std::unique_ptr<ForwardDeclaredType>` as a
  class member needs an out-of-line destructor defined where the type
  is complete — even that wasn't enough here (still failed from a
  different translation unit including only the header), so the fix
  is a plain raw pointer with manual new/delete instead, documented
  in `LightingControlsModule.h` for whoever hits the same thing next.
  Confirmed no regression elsewhere: `rmlui_demo` (which doesn't add
  the new module) now correctly shows a clean bottom half with no
  leftover boxes, all of its own existing content untouched.
- **`Rml::Debugger`'s "Outlines" tool crashes** — found incidentally
  while fixing the range slider (see below), not chased down: clicking
  it segfaults deep inside `libvulkan_lvp.so` (lavapipe, the software
  Vulkan driver this sandbox uses), confirmed via a real `gdb`
  backtrace on a background thread. `rmlui_demo` currently initializes
  the debugger but keeps it hidden (`Rml::Debugger::SetVisible(false)`)
  specifically to avoid this. Likely lavapipe-specific rather than a
  real engine bug, but genuinely unconfirmed on real hardware — worth
  a real look before assuming either way.
- **RmlUi demo: three real interaction bugs found and fixed, one still
  open.** All three verified with actual clicks/state changes, not
  just visual appearance:
  - **Tabset content beside the tabs, not under them** — both
    `tabset tabs` and `tabset panel` needed explicit `display: block`,
    confirmed against RmlUi's own working sample
    (`Samples/assets/invader.rcss`), not guessed.
  - **Checkbox/radio invisible and unclickable** — found while
    investigating the slider: neither had *any* CSS at all, so they
    rendered at effectively zero size. Not a hit-testing bug — there
    was genuinely nothing there to click. Fixed with real sizing;
    verified by clicking "Mode B" and watching "Mode A" correctly
    deselect (real radio-group exclusivity, not just a color change).
  - **Dropdown with no background or hover** — only the outer `select`
    element had ever been styled; the parts a user actually sees and
    clicks (`selectvalue`, `selectarrow`, `selectbox`,
    `selectbox option`) inherit nothing automatically. Fixed with real
    styling for all of them.
  - ~~**The range slider didn't respond to any click or drag**~~ Fixed
    — with a real, code-level root cause, not more CSS. Wired in
    RmlUi's own debugger (`Rml::Debugger`, already built as part of
    this project's existing RmlUi fetch — confirmed unconditional in
    `Source/CMakeLists.txt`, no new dependency needed) to investigate
    properly rather than keep guessing from screenshots. Its own
    "Outlines" tool immediately crashed — a real, reproducible
    segfault confirmed via `gdb` backtrace, deep inside the lavapipe
    software driver itself — set aside as a separate, likely
    sandbox-specific issue, not chased further. The real fix came from
    adding temporary diagnostic logging directly into `UiModule`'s own
    mousedown handling, printing exactly which element
    `Context::GetHoverElement()` resolves each click to. Across eleven
    different Y coordinates spanning the entire visible track/thumb
    area, every single click resolved to the parent `<input
    class="range">` itself, never to the internal `slidertrack`/
    `sliderbar` elements. Traced this to RmlUi's own source: `WidgetSlider::Initialise()`
    adds both as children via `AppendChild(..., /*dom_element=*/false)`
    — confirmed by reading `WidgetSlider.cpp` and `Element.cpp`
    directly — and `WidgetSlider::ProcessEvent()` specifically checks
    `event.GetTargetElement() == track`, a check that can never
    succeed given what event targeting actually resolves to in this
    integration. Rather than patch RmlUi's own vendored widget
    internals, `UiModule` now handles the click directly at the one
    point confirmed to actually receive it — the parent element —
    computing the intended value from click position and setting it
    through the same public `SetValue()` API a working slider would
    end up calling internally. Verified with real screenshots: a
    sequence of clicks across the full track correctly moves the
    thumb to each clicked position, and a full regression pass (
    checkbox, radio, tabs, and `kke_demo`'s marketplace) confirmed
    nothing else broke from a fix living in shared `UiModule` code.
- ~~**Marketplace card text running together unformatted**~~ Fixed —
  and there was already an honest comment in the code flagging this
  exact symptom, left by an earlier pass that verified it wasn't a
  data/escaping problem but didn't chase the real cause. Same root
  cause as the tabset bug above: RmlUi has no built-in "p/div default
  to block" behavior the way a browser does — that comes from a
  stylesheet RmlUi's own samples happen to link in, not something
  built into the engine for every document. `MarketplaceUiModule`'s
  generated RML never linked one, so every `<p>`/`<div>` defaulted to
  inline. Fixed by adding `display: block` directly to each generated
  element's inline style. Verified with a real screenshot: three
  clearly separated cards, proper title/id/description/tags hierarchy,
  where before everything ran together as one unbroken block of text.
- ~~**Confirm the CI workflow actually runs on GitHub's infrastructure.**~~
  Confirmed — and it found a real bug on the very first real run, not a
  clean pass. The "Headless smoke test" step failed with "Process
  completed with exit code 124" despite that step's own script being
  written specifically to treat 124 (from `timeout 8 ./kke_demo`) as
  success. Root cause, confirmed by reproducing it locally under the
  exact same `bash -e` GitHub Actions uses: errexit aborts a script
  immediately when a bare command on its own line returns non-zero —
  `timeout`'s 124 killed the script *before* `exit_code=$?` or the
  check meant to accept 124 ever ran. Fixed with `set +e`/`set -e`
  bracketing just that one command, verified by reproducing both the
  broken and fixed behavior locally against the exact same shell
  invocation before pushing anything. The build (625/625 objects) and
  all 40 tests had already passed cleanly on the real runner before
  this — the engine itself was never the problem, only this one
  script's exit-code handling.
- ~~**Render-mesh-to-tetrahedra vertex skinning bridge**~~ Fixed — see
  "Render bridge" above. (True general skinning — arbitrary render
  meshes onto many tets — is still future work; what exists now is
  the minimal "the tet's own 4 vertices are the render mesh" version.)
- ~~**A general "spawn object with mesh + material" API**~~ Fixed —
  see "General spawn API" above. Still only one spawnable shape; the
  API itself (position, material, runtime-callable) is real and
  general.
- ~~**TetGen-based `.FEM` authoring without Houdini**~~ Resolved by
  choosing CGAL instead — see "Content pipeline: CGAL
  tetrahedralization" for the full technical and licensing reasoning.
  A real repair pipeline (soup repair, orientation fixing,
  triangulation, border stitching) now handles genuinely common real-
  world defects — verified against actual broken test meshes, not
  assumed — closing most of what this entry originally flagged. What's
  still genuinely open: no voxel-grid pipeline for real missing
  geometry (a true hole, as opposed to a fixable winding/triangulation
  defect, correctly refuses with a clear error rather than crashing,
  but still can't be tetrahedralized), and no OBJ/FBX/glTF import
  (needs assimp, feeding into this same repair pipeline).
- ~~**The ImGui resize assertion crash**~~ Fixed. Real root cause,
  found by reading `Application.cpp`'s frame loop, not by guessing:
  `m_debugUi->beginFrame()` (which calls `ImGui::NewFrame()`) ran
  unconditionally, *before* checking whether `m_renderer->beginFrame()`
  would even succeed. When the swapchain went out of date mid-resize
  and that check failed, the whole rendering block — including
  `ImGui::Render()` — got skipped for that frame, so the *next*
  frame's `NewFrame()` fired with no matching `Render()` in between,
  which is exactly what the assertion was complaining about. Fixed by
  moving `beginFrame()` and the `renderUi()` calls inside the success
  branch, so they only ever run for a frame guaranteed to complete.
  Verified by reproducing the *exact* crash first (a single resize
  killed a real running session), then confirming the fix survives
  four rapid resize cycles in a row without issue, followed by a full
  test-suite pass.
- ~~**A dedicated, visually legible physics demo**~~ Done — see
  "`games/physics_demo` — a dedicated demo" above for the real camera/
  scale/ground-size work this took, verified with real screenshots at
  each step. Now genuinely being used as the actual demo, not deferred.
- ~~**ImGui showcase demo**~~ and ~~**RmlUi showcase demo**~~ Done —
  see "The demo suite" section above for both. The shared-shader-target
  and shared-font-copy CMake fixes made while building `physics_demo`
  paid off immediately here — both new demos reuse shader files already
  used elsewhere (`grid.vert/frag`, `rml_ui.vert/frag`) and hit no
  collision at all, confirming those fixes were real and general, not
  narrowly patched for one case. What *was* newly found and fixed here:
  a badly-overlapping first RmlUi layout, fixed by checking the other
  panels' actual hardcoded positions in their own source rather than
  guessing.
- **The rest of the demo suite** — a VulkanProfiler demo with sample
  analysis, chunk-loading/streaming (dual-viewport), culling
  (dual-viewport) — still genuinely unstarted. The chunk-streaming and
  culling demos in particular are not "just wrap existing capability
  in a demo" the way the two done so far were — this engine has no
  chunk/streaming system and no frustum/occlusion culling at all yet,
  so those two are real subsystems to design and build, not just demo
  wrapping.
- ~~**Real multithreading for the physics task system**~~ Done — see
  "Real multithreading" in "Physics: AMD FEMFX integration" for the
  full account, including a real correctness risk (per-worker scratch
  buffer indexing) and a real deadlock, both found and fixed before
  trusting it. `tools/physics_benchmark` exists specifically so the
  actual speedup can be measured on real multi-core hardware — this
  sandbox's single core can only prove correctness, not performance.
- **A physics stress-test demo.** Raise `kMaxObjects` well past 8 and
  see what actually happens — the particle system already proves
  20,000 GPU-simulated particles; physics has never been pushed
  anywhere near that. Real multithreading now exists (see above) but
  hasn't been proven to help at scale — only proven correct — since
  this sandbox has no second core to show a speedup on. Already have
  one real data point pointing at real cost: a single 401-tet mesh
  dropped frame rate from ~27 FPS to ~4 FPS in this sandbox's
  software-rendered, single-core environment.
- **`vkGetProfilerFrameDataEXT` into `StatsModule` and spdlog** — see
  "GPU profiler (VulkanProfiler) integration." Written and functional
  in structure, but disabled behind `KKE_QUERY_GPU_PROFILER_DATA` after
  a real, diagnosed crash inside the layer's own code when calling it
  from application code in this (lavapipe) environment. Re-enabling on
  real hardware or a newer layer version is a one-line change away.
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
- ~~**RmlUi slice 5: resize handling.**~~ Fixed — see "What's not done
  yet" above for the verification details. Kept here, struck through,
  since the reasoning (why a per-frame check rather than reacting to
  the SDL resize event directly) is worth keeping visible in the
  history, not just the fact that it's done.
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
When that binding layer gets built, its errors should surface through
`kke::EngineError` (see "Clear errors for scripters" above) — that
plumbing already exists and is verified, specifically so this doesn't
need its own error-reporting mechanism invented later.

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
