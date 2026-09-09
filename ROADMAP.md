# ROADMAP.md — Kreative Kompas Engine, current state by system

**This file answers "what can this engine actually do right now?"** —
distinct from `README.md`'s "Immediate next slices," which is a
chronological narrative of *how* each thing got built (useful for the
full story and reasoning) and `BUGS.md`, which tracks specific defects
(useful for "has this exact symptom been seen before?"). This file is
for "what systems exist, how solid is each one, and what's the next real
gap in each" — read top-to-bottom for orientation, or jump to a system
you're about to touch.

**Update this file in the same session as any change that adds,
finishes, or meaningfully changes a system's status below.** A stale
Roadmap is worse than none — see `BUGS.md`'s own intro for a concrete
example of this happening (README's old "Known simplifications" section
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
| Cross-module communication | 🟢 | `Application::getModule<T>()`, documented capability-discovery pattern (`findCapability<T>()`) for loose coupling — see README "Cross-module communication." |
| Logging | 🟢 | spdlog-based, async, per-module named loggers. |
| Debug pause/step | 🟢 | Real: freezes `fixedUpdate`/physics while still rendering, `Step one frame` for single-tick advance. |
| Build system, both `KKE_ENABLE_FEMFX` on/off | 🟢 | Both configurations verified to build clean and run clean every session this file's history covers. |
| Test suite | 🟢 | 40 GoogleTest unit tests, 85% coverage floor enforced for pure-logic code (`GameManifest`, `MarketplaceIndex`, `RmlTextSafety`). GPU/Vulkan code is verified by build-and-run instead (see README "Test suite & coverage" for why the split). |
| Icon / branding | 🔴 | Not wired up at all. |
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
| Particle system | 🟢 | Custom GPU-driven system (not a third-party library — see README "Why a custom particle system" for the reasoning), 20,000 particles is an untuned default, not a measured ceiling. |
| Orbit camera | 🟢 | Doesn't use locked/relative cursor mode — dragging past the window edge stalls rather than wrapping (documented limitation, not a bug). |
| Reference grid | 🟢 | |

## Lighting & shadows

| System | Status | Notes |
|---|---|---|
| Multi-light system (up to 4 lights) | 🟢 | Real, verified visually — see README "Lighting" for the full account. |
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
| Tetrahedral deformable-body simulation | 🟢 | Real FEMFX integration, not a stub — see README "Physics: AMD FEMFX integration." |
| Real fracture | 🟢 | Verified with real, measured piece counts across many real drops — see `BUGS.md` BUG-007 for the shape-resolution fix that made this visually convincing. |
| Real plasticity | 🟢 | Verified via real vertex-distance measurement (not FEMFX's own rest-position API, which doesn't track this — see `BUGS.md` BUG-003). |
| Material toughness tuning | 🟢 | Real, measured stress ranges per material, not a guessed formula — see `BUGS.md` BUG-020 for the full account, including the wrong approach that preceded it. |
| Scene-scale robustness (many objects) | 🟢 | Verified up to ~57 simultaneous objects without falling through the ground — see `BUGS.md` BUG-005. |
| General box-shape generator | 🟢 | `PhysicsModule::buildGridBox(cellsX, cellsY, cellsZ, sizeX, sizeY, sizeZ)` — arbitrary per-axis cell counts and physical dimensions, used by every scene below. |
| Purpose-built physics scenes | 🟢 | Five real scenes, each independently verified: **Glass Sheet** (shatters into ~13-58 real pieces depending on threshold tuning at time of test), **Brick** (real 2:1:1 proportions, breaks into chunks), **Rubber Ball** (honest approximation — a box, not a true tetrahedralized sphere, see its own in-code comment; bounces without fracturing), **Car Crash** (two real objects: a plastic-deforming "car" and a fracturable "wall"), **Lava Melt** (honest approximation — real plasticity under sustained real weight, not true phase-change physics; FEMFX has none). |
| Real sphere / curved-shape tetrahedralization | 🔴 | Not started — would need real mesh-import machinery (see Content pipeline's own CGAL section) beyond the box generator. |
| Performance at scale on real hardware | 🔴 | User-reported and confirmed as a real, serious problem, not just a test-sandbox artifact: single-digit FPS spawning a handful of fracture fragments, CPU-bound (~69%+), no GPU offload for the physics itself. Real, honest context found while investigating: this sandbox has exactly 1 CPU core (confirmed via `nproc`), which alone explains a large part of it — but the *architecture* also has real, unaddressed gaps (no active-body budget, no GPU-particle handoff for settled/small debris, no buffer pooling for fragments, no sleep state) found by studying RayFire and Chaos's own real optimization patterns — see `PERFORMANCE_NOTES.md` for the full, real research and a concrete, prioritized action list. None of it implemented yet. |
| Network authority / reconciliation | 🔴 | Not started — see Networking section. |

## UI (RmlUi + ImGui)

| System | Status | Notes |
|---|---|---|
| RmlUi Vulkan backend | 🟢 | Real custom render interface, not a stub — text, `<input>` (text/checkbox/radio/range/select), `<textarea>`, `<progress>` all confirmed working. |
| RmlUi image loading | 🟢 | See Rendering section — `<img>`/`background-image` real via `stb_image`. |
| RmlUi context resize handling | 🟢 | Context dimensions genuinely track the swapchain extent every frame. |
| RmlUi panel *positioning* at different window sizes | 🟢 | Percentage-based, genuinely scales — see `BUGS.md` BUG-018. |
| RmlUi panel *width* at different window sizes | 🔴 | Still fixed pixels — see `BUGS.md` BUG-022 (open). A panel can still overflow a narrow window. |
| RmlUi range slider interaction (click + drag) | 🟢 | Real click-to-jump and real continuous drag, both bypassing a genuine RmlUi internal-widget limitation — see `BUGS.md` BUG-011. |
| RmlUi hit-testing across multiple simultaneous documents | 🟢 | See `BUGS.md` BUG-012 — was fundamentally broken (most panels effectively unclickable), now fixed everywhere it's been found. **Any new panel/document added in the future needs the same `pointer-events` treatment from day one, or this regresses for that panel specifically — see BUG-012's own fix description before adding a new UI document.** |
| RmlUi panel dragging (move by title bar) | 🟢 | Real, general mechanism (`draggable-handle` class + nearest-positioned-ancestor search) — see `BUGS.md` BUG-013/BUG-014. |
| RmlUi debugger (Outlines, etc.) | 🟢 | Real GPU-resource-lifetime bugs found and fixed — see `BUGS.md` BUG-002. |
| RmlUi text rendering (`font-family` on every rule) | 🟢 | Real, systemic gap found via a general verification sweep, not a specific bug report — `font-family` does not reliably inherit through the RmlUi DOM in this project's setup, so several elements across all four generated documents silently rendered no text at all (Material Grid's card labels/stats, confirmed missing in a real screenshot). Fixed everywhere found — see `BUGS.md` BUG-024. **Any new RCSS rule that displays text needs `font-family` set explicitly, every time — this is now a standing rule, not just a one-off fix.** |
| RmlUi discoloration behind 3D content | 🔴 | **Reported by user, not yet reproduced or root-caused** — see `BUGS.md` BUG-021 (open, needs clearer repro). |
| ImGui integration | 🟢 | Full canonical demo confirmed working (buttons, sliders, color pickers, drag/drop, tables, trees, tabs, plotting, text editing). |
| ImGui + RmlUi coexistence | 🟢 | Both receive every SDL event; no conditional capture-blocking exists between them (confirmed directly while investigating BUG-012, ruled out as a cause). |
| Font fallback chain | 🟢 | Noto Sans + Noto Color Emoji, documented in README. |
| Marketplace UI (game browsing) | 🟢 | Real `MarketplaceIndex` backing it (see Content pipeline), not just static demo content. |

## Content pipeline

| System | Status | Notes |
|---|---|---|
| Game manifest format (`game.json`) | 🟢 | Real parser, real unit-tested (`GameManifest`). |
| Marketplace index (multi-game discovery) | 🟢 | Real, unit-tested (`MarketplaceIndex`). |
| CGAL tetrahedralization pipeline | 🟢 | Real external tool (`kke_tetrahedralizer`) producing real `.ktet.json` assets loadable via `spawnTetMesh()`'s general path — see README "Content pipeline." |
| Asset browser | 🔴 | Not started — tetrahedralized mesh loading path is proven (`Load /tmp/test_output.ktet.json` button) but there's no UI to browse/pick assets. |
| Lua scripting | 🔴 | Fetched as a dependency, not called from any code yet. |

## Networking

| System | Status | Notes |
|---|---|---|
| Replication measurement/demo | 🟢 | `NetworkModule` genuinely measures and displays what *would* be sent. |
| Real network transport | 🔴 | Nothing actually leaves the process yet. |
| Authority / reconciliation model | 🔴 | `deserializeReplicatedState()` exists but is unused; two peers disagreeing about state isn't handled at all. |

## Tooling / dev experience

| System | Status | Notes |
|---|---|---|
| GPU profiler (`VulkanProfiler`) | 🟢 | Real integration, not a stub. |
| `StatsModule` | 🟢 | Real GPU timing — the actual right tool for finding a real particle-count performance ceiling (still not done, see Rendering). |
| Headless verification workflow (Xvfb + lavapipe) | 🟢 | This is how every visual claim in this project's whole history has actually been checked — screenshot or log-inspect, never "should work." See README "Build" and `AI_GUIDE.md`. |
| `BUGS.md` (this pair, bug side) | 🟢 | Just established this session. |
| `ROADMAP.md` (this file) | 🟢 | Just established this session. |

---

## How these four files relate

- **`AI_GUIDE.md`** — process rules or an AI/human picking up this repo
  needs to follow (verify before claiming, read real source over
  memory, small slices, etc.). Rarely changes.
- **`ROADMAP.md`** (this file) — current-state snapshot, by system.
  Update when a system's status genuinely changes.
- **`BUGS.md`** — specific defects, symptom → root cause → fix, cross-
  referenceable by ID. Update every time you find or fix something,
  in the same session.
- **`PERFORMANCE_NOTES.md`** — real, researched architecture notes on
  how production destruction systems (RayFire, Chaos) actually solve
  the performance problems KKE is now hitting for real. Not a changelog
  — a plan, with a concrete, prioritized action list, none of it
  implemented yet as of this file's own last update.
- **`README.md`**'s own "Immediate next slices" — the fuller narrative
  of *how* and *why*, kept for depth/reasoning. Not a replacement for
  the files above, which exist specifically so that depth doesn't
  have to be re-read (or re-derived) just to answer "does X work yet?"
