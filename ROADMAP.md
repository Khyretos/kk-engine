# ROADMAP.md — Kreative Kompas Engine, current state by system

**This file answers "what can this engine actually do right now?"** —
distinct from `docs/HISTORY.md`'s "Immediate next slices," which is a
chronological narrative of *how* each thing got built (useful for the
full story and reasoning) and `BUGS.md`, which tracks specific defects
(useful for "has this exact symptom been seen before?"). This file is
for "what systems exist, how solid is each one, and what's the next real
gap in each" — read top-to-bottom for orientation, or jump to a system
you're about to touch.

**Update this file in the same session as any change that adds,
finishes, or meaningfully changes a system's status below.** A stale
Roadmap is worse than none — see `BUGS.md`'s own intro for a concrete
example of this happening (docs/HISTORY.md's old "Known simplifications" section
claiming RmlUi image loading was still a stub hours after it was fixed).

Status legend: 🟢 Solid — real, verified, no known blocking gaps for its
stated scope. 🟡 Partial — real and working, but with real, named
limitations. 🔴 Not started / stub.

---

## Core engine

| System | Status | Notes |
|---|---|---|
| Module system (`kke::Module`/`kke::Application`) | 🟢 | Explicit `init`/`update`/`fixedUpdate`/`render`/`renderShadow`/`renderUi`/`onEvent`/`shutdown` lifecycle. Per-module fault isolation (a throwing module gets disabled, not the whole app) is real and tested. |
| Frame loop, fixed timestep | 🟢 | Real accumulator-based fixed update separate from variable render rate. |
| Cross-module communication | 🟢 | `Application::getModule<T>()`, documented capability-discovery pattern (`findCapability<T>()`) for loose coupling — see docs/HISTORY.md "Cross-module communication." |
| Logging | 🟢 | spdlog-based, async, per-module named loggers. |
| Fixed-step overload behavior | 🟢 | At most `setMaxFixedStepsPerFrame()` (default 2) catch-up ticks per frame; an overloaded simulation runs in slow motion instead of freezing the app — see `BUGS.md` BUG-030. |
| Debug pause/step | 🟢 | Real: freezes `fixedUpdate`/physics while still rendering, `Step one frame` for single-tick advance. |
| Build system, both `KKE_ENABLE_FEMFX` on/off | 🟢 | Both configurations verified to build clean and run clean every session this file's history covers. |
| Test suite | 🟢 | 40 GoogleTest unit tests, 85% coverage floor enforced for pure-logic code (`GameManifest`, `MarketplaceIndex`, `RmlTextSafety`). GPU/Vulkan code is verified by build-and-run instead (see docs/HISTORY.md "Test suite & coverage" for why the split). |
| Icon / branding | 🟡 | Kreative Kompas logo is the window icon (SDL3, embedded) and the Windows `.exe` icon (`.rc`, not yet built on Windows); every game opens with the animated 3D logo intro (`kke::LogoIntro`, skippable, `KKE_SKIP_INTRO=1`). Missing: macOS `.icns` + bundle. |
| Shader path resolution | 🟡 | Relative to working directory, not executable path — fine for `./build/bin/kke_demo` run from `build/bin/`, needs `SDL_GetBasePath()` before shipping anywhere else. |

## Rendering (3D pipeline)

| System | Status | Notes |
|---|---|---|
| Vulkan core (device, swapchain, render passes) | 🟢 | |
| Vulkan validation layers in dev builds | 🟢 | Every demo swept with validation active every session; this is how several real GPU-resource-lifetime bugs (see `BUGS.md` BUG-002) were actually found, not guessed. |
| Mesh rendering, cube/box geometry | 🟢 | `kke::Mesh::createCube()`, and the more general `PhysicsModule::buildGridBox()` for arbitrary box shapes (see Physics section). |
| Vertex format | 🟢 | `position/color/normal/uv` — real UV mapping, flat-shading-correct (see `BUGS.md` BUG-006 for the smooth-shading bug this replaced). |
| PBR materials (Cook-Torrance) | 🟢 | Real metallic/roughness, per-object, verified via real pixel-value comparisons (not just visual impression) after finding and fixing a methodology bug in the first verification attempt. |
| Material albedo textures | 🟢 | Procedurally generated (not loaded from files — see Content pipeline). Six real textures: wood, stone, iron, rubber, glass, lava. Real UV-mapped sampling, not just flat tints, for both `CubeModule` and `PhysicsModule`-spawned objects. |
| Real image/texture loading from files | 🟡 | `stb_image`-based `LoadTexture()` works for RmlUi (`<img>`, `background-image`) — see `BUGS.md` BUG-004. The 3D pipeline's own material textures are still all procedurally generated, not loaded from files; no asset browser exists yet for this. |
| Particle system | 🟢 | Custom GPU-driven system (not a third-party library — see docs/HISTORY.md "Why a custom particle system" for the reasoning), 20,000 particles is an untuned default, not a measured ceiling. |
| Orbit camera | 🟢 | Doesn't use locked/relative cursor mode — dragging past the window edge stalls rather than wrapping (documented limitation, not a bug). |
| Reference grid | 🟢 | |

## Lighting & shadows

| System | Status | Notes |
|---|---|---|
| Multi-light system (up to 4 lights) | 🟢 | Real, verified visually — see docs/HISTORY.md "Lighting" for the full account. |
| Shadow mapping | 🟢 | Single directional light, single shadow-casting pass. Real regression found and fixed this session — see `BUGS.md` BUG-010. |
| Soft shadows (PCF) | 🟢 | Real 3×3 kernel, replacing the earlier hard single-tap edge. |
| Shadow casting scope | 🟡 | `CubeModule` and `PhysicsModule`-spawned objects cast real shadows. Not generalized to every module — a new module needs its own `renderShadow()` implementation. |
| Point-light shadows | 🔴 | Not started — only the single directional light casts. |
| Cascaded / multiple shadow maps | 🔴 | Not started — one shadow map, one fixed scene-radius frustum. |
| Image-based ambient lighting | 🔴 | Ambient term is still a flat `color * albedo` stand-in, not a captured/convolved environment map. |
| Normal maps | 🔴 | Not started — roughness/metallic are still one value per object, not per-pixel textures either. |
| Dynamic light add/remove at runtime | 🔴 | Fixed 4-slot array configured at startup; no runtime API to add/remove lights. |

## Physics (AMD FEMFX integration)

| System | Status | Notes |
|---|---|---|
| Tetrahedral deformable-body simulation | 🟢 | Real FEMFX integration, not a stub — see docs/HISTORY.md "Physics: AMD FEMFX integration." |
| Real fracture | 🟢 | Verified with real, measured piece counts across many real drops — see `BUGS.md` BUG-007 for the shape-resolution fix that made this visually convincing. Fractured pieces now all actually render (they mostly didn't — BUG-026). |
| Natural fracture patterns (Voronoi, KKE-driven breaking) | 🟢 | `kke::VoronoiFracture` (random-angle cracks, same tet count) + `kke::BreakGraph` + `PhysicsModule::Breakable` (plain FEMFX bodies swapped at break time, not FEMFX's own fracture: BUG-046). Seeds: world x object (`kke::fractureSeed`). Damage scales with the hit (break window + grace, BUG-051). Physics demo "Break test" scene. Thresholds measured with `tools/physics_lab`. |
| Rigid bodies + world collision + character controller (Jolt) | 🟡 | `kke::RigidWorld` / `RigidBodyModule` on Jolt 5.6 (MIT): static/kinematic/dynamic bodies (box, sphere, capsule, convex hull, triangle mesh), ray casts, contact events with material ids, `CharacterVirtual` controller (walk, slopes, stairs, jump, push). 1,000 falling boxes: 1.56 ms/step avg on one thread (FEMFX: ~0.2 ms *per body*). **Not yet:** FEMFX <-> Jolt collision between the two worlds, rigid breakables. |
| FEMFX on ARM / WebAssembly | 🔴 | FEMFX's vector math is x86 AVX intrinsics: no Android, Apple Silicon or browser build until a SIMDe port (docs/SCALING.md D). |
| Real plasticity | 🟢 | Verified via real vertex-distance measurement (not FEMFX's own rest-position API, which doesn't track this — see `BUGS.md` BUG-003). |
| Material toughness tuning | 🟢 | Real, measured stress ranges per material, not a guessed formula — see `BUGS.md` BUG-020 for the full account, including the wrong approach that preceded it. |
| Scene-scale robustness (many objects) | 🟢 | Verified up to ~57 simultaneous objects without falling through the ground — see `BUGS.md` BUG-005. Scene capacities are now sized for fracture pieces too (4096 pieces), and any FEMFX limit that is hit gets logged — see BUG-027. |
| General box-shape generator | 🟢 | `PhysicsModule::buildGridBox(cellsX, cellsY, cellsZ, sizeX, sizeY, sizeZ)` — arbitrary per-axis cell counts and physical dimensions, used by every scene below. |
| Purpose-built physics scenes | 🟢 | Five real scenes, each independently verified: **Glass Sheet** (shatters into ~13-58 real pieces depending on threshold tuning at time of test), **Brick** (real 2:1:1 proportions, breaks into chunks), **Rubber Ball** (honest approximation — a box, not a true tetrahedralized sphere, see its own in-code comment; bounces without fracturing), **Car Crash** (two real objects: a plastic-deforming "car" and a fracturable "wall"), **Lava Melt** (honest approximation — real plasticity under sustained real weight, not true phase-change physics; FEMFX has none). |
| Real sphere / curved-shape tetrahedralization | 🟢 | `buildSphere` (spherified cube) and `kke::voxelizeToTets` + `fitSurfaceToMesh` for any mesh. |
| Performance at scale | 🟡 | Much better, measured, not yet checked on real hardware. Sleeping works (settled piles cost ~0.2 ms/step), FEMFX always optimized, only exterior faces drawn, catch-up capped at 2 ticks/frame. Min-spec emulation (1 core): 0.6 → 11.0 FPS average over the scripted benchmark, with 6x more fracture pieces than before (the old build was silently capping fracture). Remaining gap: while a big break is still flying, cost scales with awake piece count (~55 ms/step for ~475 pieces on 1 core) — no debris budget yet. See `docs/PERFORMANCE_NOTES.md` "Status" and `docs/HARDWARE_TESTS.md` HW-001..HW-004. |
| Jiggle physics (bones, skin, soft bodies) | 🟢 | `kke/JigglePhysics.h`, core (no FEMFX needed): `JiggleRig` (verlet bone chains on any rig), `addJiggleBone` / `inflateSkin` / `addHumanoidSoftTissue` (soft tissue and curves for rigs that have none), `JiggleSkin` (boneless skin zones via `ModelModule::setSkinJiggle`), `JellyBody` (lattice shape matching with two-way ball contact). 14 unit tests; `games/jiggle_demo`. See docs/JIGGLE.md. **Not yet:** jiggle rigs spread over worker threads and a GPU skinning path for crowds (one character: ~36 µs; jelly: ~0.4 ms per frame on lavapipe, not worth it yet), jelly vs Jolt/FEMFX bodies. |
| Debris budget | 🟢 | `PhysicsModule::setDebrisBudget` (default 200 broken pieces, oldest sleeping piece removed first), slider in the Physics panel. |
| Network authority / reconciliation | 🟡 | Host is authoritative for Jolt bodies; clients simulate their copies and steer them to the host's (docs/NETWORKING.md). FEMFX breakables are still per machine. |

## UI (RmlUi + ImGui)

| System | Status | Notes |
|---|---|---|
| RmlUi Vulkan backend | 🟢 | Real custom render interface, not a stub — text, `<input>` (text/checkbox/radio/range/select), `<textarea>`, `<progress>` all confirmed working. |
| RmlUi image loading | 🟢 | See Rendering section — `<img>`/`background-image` real via `stb_image`. |
| RmlUi context resize handling | 🟢 | Context dimensions genuinely track the swapchain extent every frame. |
| RmlUi panel *positioning* at different window sizes | 🟢 | Percentage-based, genuinely scales — see `BUGS.md` BUG-018. |
| RmlUi scaling (position *and* size) | 🟢 | Everything in `dp`; dp ratio follows window height x user UI scale (`UiModule::setUiScale()`). See `BUGS.md` BUG-022. |
| RmlUi range slider interaction (click + drag) | 🟢 | Real click-to-jump and real continuous drag, both bypassing a genuine RmlUi internal-widget limitation — see `BUGS.md` BUG-011. |
| RmlUi hit-testing across multiple simultaneous documents | 🟢 | See `BUGS.md` BUG-012 — was fundamentally broken (most panels effectively unclickable), now fixed everywhere it's been found. **Any new panel/document added in the future needs the same `pointer-events` treatment from day one, or this regresses for that panel specifically — see BUG-012's own fix description before adding a new UI document.** |
| RmlUi panel dragging (move by title bar) | 🟢 | Real, general mechanism (`draggable-handle` class + nearest-positioned-ancestor search) — see `BUGS.md` BUG-013/BUG-014. |
| RmlUi debugger (Outlines, etc.) | 🟢 | Real GPU-resource-lifetime bugs found and fixed — see `BUGS.md` BUG-002. |
| RmlUi text rendering (`font-family` on every rule) | 🟢 | Real, systemic gap found via a general verification sweep, not a specific bug report — `font-family` does not reliably inherit through the RmlUi DOM in this project's setup, so several elements across all four generated documents silently rendered no text at all (Material Grid's card labels/stats, confirmed missing in a real screenshot). Fixed everywhere found — see `BUGS.md` BUG-024. **Any new RCSS rule that displays text needs `font-family` set explicitly, every time — this is now a standing rule, not just a one-off fix.** |
| Color management (sRGB) | 🟢 | Colors linearized before writing to the sRGB swapchain, premultiplied UI blending — see `BUGS.md` BUG-021. Needs a real-GPU look (HW-005). |
| RmlUi Vulkan backend features | 🟡 | Transforms, gradients (`linear/radial/conic`, repeating), stencil clip masks, premultiplied alpha, sRGB — all implemented. **Not yet:** filters/layers (`filter`, `box-shadow`, `backdrop-filter`) — silently ignored. |
| RmlUi input | 🟢 | Text input (SDL3), HiDPI mouse mapping, keeps updating while paused, captures the mouse from gameplay/camera (`Application::uiCapturesMouse()`), F5 reloads styles. See `BUGS.md` BUG-032. |
| UI showcase (`rmlui_demo`) | 🟢 | Main menu, settings, inventory (drag & drop), HUD, dialogue/chat, loading — see docs/HISTORY.md "The demo suite". Verified by screenshots + simulated clicks/drags/typing. |
| Player settings | 🟢 | `kke::EngineSettings` (JSON, unit-tested) + `kke::SettingsModule` (applies fullscreen, VSync, FPS cap, FOV, shadows, brightness, UI scale, dev overlay, sensitivity, physics steps). Audio values stored, no audio module yet. |
| ImGui integration | 🟢 | Full canonical demo confirmed working (buttons, sliders, color pickers, drag/drop, tables, trees, tabs, plotting, text editing). |
| ImGui + RmlUi coexistence | 🟢 | Both receive every SDL event; no conditional capture-blocking exists between them (confirmed directly while investigating BUG-012, ruled out as a cause). |
| Font fallback chain | 🟢 | Noto Sans + Noto Color Emoji, documented in docs/HISTORY.md. |
| Marketplace UI (game browsing) | 🟢 | Real `MarketplaceIndex` backing it (see Content pipeline), not just static demo content. |

## Content pipeline

| System | Status | Notes |
|---|---|---|
| Game manifest format (`game.json`) | 🟢 | Real parser, real unit-tested (`GameManifest`). |
| Marketplace index (multi-game discovery) | 🟢 | Real, unit-tested (`MarketplaceIndex`). |
| Mesh -> physics volume (`kke::voxelizeToTets`) | 🟢 | Runtime, any mesh (open ones too), fitted to the surface; the offline CGAL tool was removed (GPL, superseded) — see docs/HISTORY.md "Content pipeline". |
| FBX/OBJ model import (`kke::loadModel`) | 🟢 | ufbx-based: meshes, materials, texture resolution, skeletons, skin weights, sampled clips. Unit-tested (incl. real Synty character when installed). |
| Model rendering (`kke::ModelModule`) | 🟡 | Instances, PBR lit/textured, shadows, CPU skinning, clip playback, bone posing, bone overlay. Mip maps, texture variants per instance, world-space overlay (Synty Prototype grid). **Not yet:** GPU skinning (for crowds), frustum culling, instanced draws, normal maps. |
| Synty packs | 🟡 | POLYGON Prototype verified (`games/synty_demo`). Loaded from git-ignored `assets/synty/` — see `assets/README.md`. Other packs untested (docs/HARDWARE_TESTS.md HW-009). |
| Ragdolls | ✅ | Physics-agnostic `kke::RagdollDesc` + `buildHumanoidRagdoll()` behind `IRagdollPhysics`. On Jolt (`RigidBodyModule`, preferred by `bestRagdollPhysics()`): swing-twist cone and twist limits, hinged knees and elbows with bend ranges, limbs that collide with each other; FEMFX (`PhysicsModule`) remains a fallback. `blendPoses()` blends back to animation (the Synty demo stands up where the body landed). Issue #31. |
| Asset browser / sandbox editor | 🟡 | `games/sandbox`: `kke::AssetCatalog` finds every pack on disk (any layout), filter by pack/category/search, thumbnail grid (`kke::ThumbnailModule`: offscreen render to an ImGui atlas, background loading, disk cache per pack), ghost placement with grid snap + rotate + stacking, select/move/duplicate/delete, multi-select, move/rotate/scale gizmo, undo/redo, levels saved as kke.scene (spawn, lights, per-object Jolt collision) that kke_demo loads and walks. Built on engine pieces: `kke::DebugDrawModule`, `kke/Picking.h`, `kke::SceneFile`, `OrbitCameraModule::Controls::Editor`. **Not yet:** FEMFX collision with placed statics inside the sandbox. |
| Destructible Synty props | 🟢 | Any prop: voxelized + surface-fitted tets, material fracture patterns (splinters / Voronoi chunks / radial glass / shards / metal dents), prop's own mesh embedded and drawn deforming/breaking, settle-then-arm thresholds, runaway guard. **Not yet:** collision for static placed meshes, debris budget and faster debris sleep, impact-point-aware radial glass, sounds/particles on break. |
| Liquids (`kke::ParticleFluid`) | 🟡 | PBF particle liquid with temperature, per-material viscosity/solidification, SDF colliders, budget; `games/melt_demo`. Smooth surface via `kke::FluidSurfaceRenderer` (screen-space fluid). **Not yet:** transparency/refraction and thickness, half-res option for min-spec, GPU compute simulation path, two-way coupling with FEMFX bodies, water body / buoyancy (next: sea demo). |
| Meltable solids (`kke::MeltVolume`) | 🟡 | Voxel density+temperature, latent-heat melting into liquid particles, marching-tetrahedra surface, chamfer SDF. **Not yet:** arbitrary shapes from meshes (voxelizer exists), refreezing into solid, burning/charring. |
| Ocean + buoyancy (`kke::OceanWaves`, `kke::FloatingBodies`, `kke::OceanRenderer`) | 🟡 | Gerstner swell from wind (CPU = GPU), point-sampled Archimedes buoyancy with drag, heave damping, ballast; `games/sea_demo`. **Not yet:** refraction/underwater view, Synty props and FEMFX bodies as floaters, wakes/interactive ripples, shore/depth colour. |
| Lua scripting | 🟢 | `kke::ScriptVM` + `kke::ScriptModule` (SCRIPTING.md): GMod-style hooks/timers, hot reload, per-script globals, runaway scripts hard-stopped (coroutine yield past `pcall`), memory cap, per-script CPU time. Bindings: physics, models/animation, FEMFX breakables, RmlUi documents, scenes, input, audio, camera, net (`sv_` realm, `net.send`). Example game: `games/first_lua_game` (break-the-targets, in kke_demo). **Not yet:** replicating what server scripts spawn, character bindings. |
| Audio (`kke::AudioMixer`, `kke::AudioModule`) | 🟢 | docs/AUDIO.md: synthesized impacts per material (Jolt and FEMFX contacts, breaks), footsteps from the character's feet, ray-traced room reverb and sound through openings, occlusion, stereo or binaural (headphones), UI earcons, navigation pings, sound visualizer with captions. **Not yet:** Steam Audio backend, streaming music, Doppler. |

## Networking

| System | Status | Notes |
|---|---|---|
| Replication measurement/demo | 🟢 | `NetworkModule` genuinely measures and displays what *would* be sent. |
| Real network transport | 🟢 | `kke::net::EnetTransport` (UDP, reliable + unreliable channels, LAN discovery), loopback and lag/jitter/loss simulation for tests; `NetModule` hosts and joins games (docs/NETWORKING.md). Dedicated server image and internet NAT traversal not yet. |
| Authority / reconciliation model | 🟡 | `NetServer`/`NetClient`: owner-predicted players checked against speed limits and corrected, server-authoritative bodies with snapshot interpolation, reliable events. Input replay (competitive) is #28. The old `kke_demo_game` `NetworkModule` is still only a measurement demo. |

## Tooling / dev experience

| System | Status | Notes |
|---|---|---|
| GPU profiler (`VulkanProfiler`) | 🟢 | Real integration, not a stub. |
| `StatsModule` | 🟢 | Real GPU timing — the actual right tool for finding a real particle-count performance ceiling (still not done, see Rendering). |
| Headless verification workflow (Xvfb + lavapipe) | 🟢 | This is how every visual claim in this project's whole history has actually been checked — screenshot or log-inspect, never "should work." See docs/HISTORY.md "Build" and `AI_GUIDE.md`. |
| `BUGS.md` (this pair, bug side) | 🟢 | Just established this session. |
| `ROADMAP.md` (this file) | 🟢 | Just established this session. |
| Scripted physics benchmark (`KKE_PHYSICS_BENCH`) | 🟢 | Same scenes at the same simulation ticks on every machine, one comparable `BENCH RESULT:` line. `KKE_PHYSICS_THREADS` overrides the worker count; the pool respects CPU affinity, so `taskset -c 0` really is 1 thread. |
| Benchmark suite (`kke_bench` + stress test in CI) | 🟢 | Headless CPU cases plus the showcase stress test on every push to main, history and trend charts on the `benchmark-data` branch; Docker hardware profiles and `KKE_VRAM_BUDGET_MB`. docs/BENCHMARKS.md. |
| `docs/HARDWARE_TESTS.md` | 🟢 | Checklist of everything only real hardware can answer, with what to send back. |
| `tools/physics_lab` (`kke_physics_lab`) | 🟢 | Headless FEMFX experiments, no window/GPU: `fracture` (stability of every pattern), `shoot` (threshold tuning), `volcano` (worst-case load), `freefall`, `rest`. |
| Cross-platform builds | 🟡 | `docker compose run --rm linux / windows / android` (docker/). Linux: builds + all tests pass in the container. Windows: MinGW-w64 cross build of every game and tool, unit tests pass under Wine. Android: native build with FEMFX off. macOS / Windows-MSVC: manual "Platforms" GitHub workflow. Browser: needs a WebGPU renderer (docs/SCALING.md). |

---

## How these files relate

- **`AI_GUIDE.md`** — process rules or an AI/human picking up this repo
  needs to follow (verify before claiming, read real source over
  memory, small slices, etc.). Rarely changes.
- **`ROADMAP.md`** (this file) — current-state snapshot, by system.
  Update when a system's status genuinely changes.
- **`BUGS.md`** — specific defects, symptom → root cause → fix, cross-
  referenceable by ID. Update every time you find or fix something,
  in the same session.
- **`docs/PERFORMANCE_NOTES.md`** — real, researched architecture notes on
  how production destruction systems (RayFire, Chaos) actually solve
  the performance problems KKE is hitting, plus a "Status" section at
  the top with measured before/after numbers and the current next steps.
- **`docs/HARDWARE_TESTS.md`** — what needs a real machine to check (a real
  GPU, many cores, the min-spec emulation, how it looks and feels), with
  exact commands and what to send back. The AI side does the code and
  sandbox measurements; this is the human side's list.
- **`docs/GO_TO_MARKET.md`** — the free-only plan for launching and funding
  the engine, with the launch gate (which milestones must be closed).
  Open work is tracked as GitHub issues labelled `M1`/`M2`/`M3`.
- **`docs/HISTORY.md`**'s own "Immediate next slices" — the fuller narrative
  of *how* and *why*, kept for depth/reasoning. Not a replacement for
  the files above, which exist specifically so that depth doesn't
  have to be re-read (or re-derived) just to answer "does X work yet?"
