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

## What's actually in this milestone

- A window (SDL3) with a Vulkan swapchain, depth buffer, and render pass
- A **module system** (`kke::Module` + `kke::Application`) that drives
  everything else — see "Architecture" below
- A spinning cube (`CubeModule`) — the original milestone, ported into
  the module shape
- A depth-correct reference grid (`GridModule`) so you have a fixed sense
  of scale and perspective in the scene
- A GPU compute-driven particle system (`ParticleModule`) — 20,000
  particles simulated entirely on the GPU, no CPU-side particle array
- A performance overlay (`StatsModule`) — FPS, CPU frame time, and *GPU*
  frame time (via Vulkan timestamp queries), each with a rolling graph
- Dear ImGui wired in for all of the above

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

Run it from `build/bin/` (or copy the `shaders/` folder next to the
executable) — shader `.spv` files are compiled next to the binary at
build time, and the app currently looks for them via a relative path
(see "Known simplifications").

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
| A conventional draw — vertex/index buffer, push-constant MVP matrix | `app/CubeModule.cpp` |
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
   class NetworkModule : public kke::Module {
   public:
       const char* name() const override { return "Network"; }
       void init(kke::Application& app) override {
           // open socket / connect to server
       }
       void update(const kke::UpdateContext& ctx) override {
           // poll socket, apply incoming state, send outgoing state
       }
       // no render()/compute() overrides needed at all
   };
   ```

3. **Register it** in `main.cpp`: `app.addModule<DestructionModule>();`
4. **Cross-module communication** (e.g. destruction needs to know what
   the physics module just simulated) isn't solved yet — right now
   modules don't have a way to reach each other except through
   `Application`. The obvious next step is either (a) `Application`
   exposing a typed module lookup (`app.getModule<PhysicsModule>()`), or
   (b) a small event-bus module both sides depend on. Worth deciding
   before a second interdependent module shows up.

Copy `app/CubeModule.h`/`.cpp` as the template for a straightforward
render-only module, or `engine/src/modules/ParticleModule.cpp` for a
compute-driven one.

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
- **No cross-module communication.** See "Adding a module" above.
- **Lua and stb are fetched but not called from any code yet.**
- **No icon wiring** — see "Branding" at the top.
- **Depth format isn't probed for support** — `VK_FORMAT_D32_SFLOAT` is
  assumed available (true on effectively every Vulkan-conformant device,
  including lavapipe, which is what's been verified here) rather than
  checked via `vkGetPhysicalDeviceFormatProperties` with a fallback.
- **20,000 particles is an arbitrary default**, not a measured "this is
  the performance ceiling" number — `StatsModule`'s GPU timing is exactly
  the tool to find that ceiling on your actual target hardware.

## Roadmap (not yet started)

- Windows and macOS (MoltenVK) build verification — Linux-first per plan
- Texture loading via stb_image
- Cross-module communication (typed module lookup or an event bus)
- A real destruction-simulation module
- A real networking module
- Icon integration per platform
