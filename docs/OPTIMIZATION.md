# OPTIMIZATION.md — how this engine thinks about performance

> Program like it's for a Nintendo 64 or a PlayStation 1, with today's
> tools. Squeeze every millisecond and every megabyte, measure everything,
> and write down *why* — so the next person, or the next AI, never has to
> guess what a clever piece of code is protecting.

This file is the doctrine, the technique catalog, and the log. It exists
because the goal is an engine and marketplace that runs on practically
any machine. That doesn't happen by accident, and it doesn't survive
contributors who don't know which code is load-bearing for performance.

Three rules come before everything else:

1. **Openness** — every optimization is written down here with its
   reasoning and its measured effect. No unexplained magic.
2. **Measurement** — "faster" means a number from a benchmark, before and
   after, on stated hardware. Guesses go in the backlog, not the code.
3. **Effort** — complexity is allowed where the numbers justify it. Simple
   code is the default, not a religion: the old masters wrote hard code in
   the hot paths and plain code everywhere else. So do we, and we document
   the hard parts.

We will be wrong sometimes. When a measurement disproves an assumption,
record that too — a documented dead end saves the next person a week.

---

## 1. Budgets (what "fast enough" means)

**Reference machines** (see HARDWARE_TESTS.md):

| Machine | Spec | Target |
|---|---|---|
| **Min-spec** | 1 CPU core, ~2 GB RAM, weakest/software GPU | Must run, correctly; heavy moments may slow to ~10 FPS but must recover and never stall. |
| **Mainstream** | 4 cores, 8 GB, integrated GPU | 60 FPS in typical scenes. |
| **Dev box** | Ryzen 7 9800X3D, RX 9070 XT | 60 FPS with lots of headroom — headroom *is* the min-spec margin. |

**Frame budget at 60 Hz = 16.6 ms**, split as a starting guide (adjust
with measurements, then update this table):

| System | Budget (mainstream) | Notes |
|---|---|---|
| Physics (fixed step) | 4 ms | Measured dev box: 1.4 ms avg for the 484-piece benchmark. |
| Game logic / scripts | 2 ms | Lua later. |
| Animation / skinning | 1 ms | CPU skinning today — see backlog. |
| Render CPU (culling, command recording) | 3 ms | |
| UI (RmlUi + ImGui) | 1 ms | |
| Slack | ~5 ms | OS jitter, spikes. |

**Memory budget (min-spec 2 GB total):** engine + a typical game should
stay under ~500 MB resident. Benchmarks report `peak_rss_mb` for this.

---

## 2. The rules

1. **Do less work before doing work faster.** Skip what can't be seen
   (culling), what hasn't changed (dirty flags), what has stopped moving
   (sleeping), what's too small to matter (LOD, debris budgets). This has
   been the single biggest win every time so far (see the log).
2. **No heap allocation in the frame loop.** Pre-size, reuse, pool.
   Growing a `std::vector` that's cleared each frame is fine (capacity is
   kept); `new`/`make_unique` per object per frame is not.
3. **Contiguous data, touched in order.** Arrays of plain structs (or
   struct-of-arrays for hot loops) over pointer-chasing trees. Handles
   (indices) over pointers so data can move and be packed.
4. **Bake offline whatever is static.** Tetrahedralization, mip maps,
   texture compression, collision proxies, fracture patterns — computed
   once at import, never at runtime ("asset cooking").
5. **Fixed budgets, graceful degradation.** Every system that can explode
   in cost (debris, particles, AI agents, catch-up physics ticks) gets a
   cap and a documented policy for what happens past it. Slow motion is
   better than a frozen frame; fewer debris pieces is better than slow
   motion.
6. **Batch GPU work.** Fewer draw calls, fewer state changes, instancing,
   persistent mappings, one upload per frame per buffer.
7. **Spread work over time.** If it doesn't need to finish this frame
   (loading, lightmap updates, AI planning, streaming), time-slice it.
8. **Single-core first, then threads.** Everything must work on one core;
   threads are a multiplier, never a requirement. The physics pool
   already respects CPU affinity.
9. **Precision is a budget too.** 16-bit where 32 isn't needed (vertex
   UVs/normals, indices for small meshes), compressed textures, quantized
   network state.
10. **Hot code gets comments that explain the trick; this file gets an
    entry.** If you optimize something and don't write it down, you've
    created a trap for the next contributor.

---

## 3. Technique catalog — old and new, and where each fits here

| Technique | Era | Where it applies in KKE | Status |
|---|---|---|---|
| Sleeping / rest detection | all | FEMFX objects and ragdolls | ✅ done |
| Dirty flags | all | Skip vertex rebuilds for sleeping physics objects; UI data-model variables | ✅ partly |
| Fixed timestep + capped catch-up | 90s | `Application::setMaxFixedStepsPerFrame` | ✅ done |
| Object pools / free lists | 80s+ | Fracture pieces, particles, projectiles | backlog |
| Frustum culling | 90s | ModelModule instances, physics objects | in progress |
| Spatial partitioning (grid / BVH) | 90s | Culling, picking, placement in the sandbox | backlog (brute force measured fine for now — log #12) |
| Level of detail (mesh LOD, impostors) | 90s | Synty packs ship LODs in some packs; impostors for distant props | backlog |
| Texture atlases | N64/PS1 | Synty packs are built around one atlas per pack — keep it that way: one texture bind for a whole pack | ✅ natural fit |
| Mip mapping | 90s | All textures: less aliasing *and* less bandwidth | ✅ done (log #14); alpha coverage kept per mip (log #32) |
| Anisotropic filtering | 2000s | All material textures | ✅ done (log #32) |
| MSAA (forward, no G-buffer) | 2000s | Geometric edges; a setting, off on min-spec | backlog (#36) |
| Texture compression (BCn/ASTC) | 2000s | Asset cooking step | backlog |
| Vertex quantization (16-bit) | N64/PS1 | Static meshes: positions as int16 + per-mesh scale, normals as oct-encoded 2×8 bit | backlog |
| Instanced draws | 2000s | Repeated props (Synty levels are 90% repeats) | backlog |
| Baked / vertex lighting | PS1/N64 | Optional "retro/min-spec" lighting path | idea |
| Debris budget / particle hand-off | 2010s (Chaos, RayFire) | Small/far fracture pieces → GPU particles | backlog (PERFORMANCE_NOTES.md #1) |
| Clustered fracture | 2010s (Chaos) | Material-driven fracture patterns (chunks = fewer, bigger, cheaper pieces) | ✅ done (BUG-043) |
| Time-slicing | all | Asset streaming, tetrahedralization of user-placed props | backlog |
| Job system / task graph | 2010s | Physics already threaded; engine-wide jobs later | partly |
| GPU skinning | 2000s | Crowds of characters | backlog |
| GPU-driven rendering / compute culling | 2020s | Large open worlds | later |
| Lookup tables, branch-free math | 80s/90s | Only where a profiler says so | as needed |

---

## 4. The log — every optimization, with evidence

Newest first. "Min-spec" = 1-core emulation in the sandbox (`taskset -c 0`,
software GPU) unless stated. Each entry names the commit area and the
BUGS.md / PERFORMANCE_NOTES.md entry with full detail.

| # | What | Why it works | Measured effect | Where |
|---|---|---|---|---|
| 32 | Anisotropic filtering (8x where supported), specular anti-aliasing from normal derivatives, coverage-preserving alpha mips | Image quality without temporal accumulation or dithering (RENDERING_PRINCIPLES.md): glancing-angle textures stay sharp, distant shiny surfaces stop sparkling, cutout foliage keeps its shape down the mip chain instead of vanishing | Coverage kept exactly in unit tests (1-in-4 stripes: plain mips 0% after two levels, fixed 25%); GPU cost not measured yet (AF is fixed-function; specular AA is a few ALU ops) | `kke::Texture`, `kke::TextureMips`, `pbr_common.glsl` |
| 32 | Melt block: distance field rebuilt only when a voxel crosses solid/empty, on a padded grid with the 13 chamfer offsets and weights computed once; heat exchange clipped to the grid | Most steps melt a little without flipping a voxel; the old sweeps bounds-checked and took a `sqrt` for every neighbour of every voxel | 24^3 block under a 700-particle lava pour: 2.7-3.0 ms -> 0.65 ms per step (melt left after 15 s identical: 67 %) | `kke::MeltVolume::step`, `rebuildDistance` |
| 31 | Breakable props: surface subdivided to 1 cell instead of half a cell, then cut exactly at the cracks | Half-cell triangles were only there so whole triangles could follow the pieces; `splitSoupAtPieces` now cuts them along the crack instead (BUG-055) | Synthetic 3.5 m pillar, 40 pieces: 11,880 glued triangles before the crack fix, 17,300 right after it, 8,640 now | `SandboxModule::makeBreakable` |
| 30 | Point-in-tet lookup on a fine grid (1/3 tet size) with a BFS-filled "nearest covered cell" for points outside the volume | Each point tests only the 2-6 tets covering its cell, and stops at the first that contains it; outside points no longer ring-search 27 coarse bins | `embedPoints`, 12,000 surface points of a 756-tet pillar: 110 ms -> 5 ms; making it breakable (embed + crack cut): ~350 ms -> ~13 ms | `kke::embedPoints` (VoxelTets.cpp) |
| 29 | Impact sounds synthesized, not sampled; each mode a recursive oscillator; cached per material x 4 levels x 4 variants | No sample library to ship or load (the N64 way: generate, don't store). `y[n] = 2cos(w)y[n-1] - y[n-2]` is one multiply-add per sample instead of a `sin()`; the cache makes repeated hits a lookup | 0.1-1 ms to make one impact, then free; bounded at 16 buffers per material | `kke::ImpactSynth`, AUDIO.md |
| 28 | Audio budgets: 32 voices (steal the quietest, drop what would be quietest), 8 impacts per frame strongest first, 80 ms per body pair, occlusion ray re-cast every 0.1 s, not every frame | Rule 5: a collapsing pile reports hundreds of contacts per frame; only the loudest few are audible. Occlusion changes slowly compared with a frame | Mixer cost bounded by voices, not by physics events; 64 simultaneous voices stay in range (test) | `AudioMixer::play`, `AudioModule::handleContacts` |
| 27 | Showcase crates merged into one mesh per frame (CPU transform of 24 vertices per crate), one draw + one shadow draw | 300 crates were 600 draws of the same cube; rebuilding ~7k vertices costs ~0.1 ms, far less than the per-draw overhead | Stress test, 4-core VM, lavapipe: crates 18.9 -> 20.5 fps, impacts 13.2 -> 15.6 fps (software raster is fill-bound, so the draw-call win on a real GPU should be larger) | `ShowcaseModule::batchCrates` |
| 26 | Resource governor: default budget is half the usable cores for workers (1..8, CPU affinity aware), 144 fps cap with vsync off, 15 fps while unfocused/minimized, optional 3D render scale 0.5..1 (offscreen pass + one linear blit, UI stays full resolution). "Use everything" lifts all of it | Rule: run with what it needs. Past ~144 fps the GPU only makes heat; a game in the background needs no 60 fps; physics threads beyond half the cores fight the OS, browser and voice chat. Render scale cuts fragment work by scale²; at 1.0 the path is off (no copy) | Town block, 4-core VM, lavapipe: 2 worker threads instead of 4; 27 -> 32 fps at scale 0.5 (software raster, so the GPU win on real hardware is larger, not measured yet) | `kke::computeBudget`, `Renderer::setRenderScale`, `Application::run` background cap |
| 25 | Instancing: 2+ visible copies of the same rigid model (same texture/overlay) drawn as one instanced draw per mesh part, shadow pass too | Level kits repeat the same wall/floor pieces hundreds of times; each copy was its own draw with its own push constants. Now one per-frame instance buffer (matrix + tint) | Synty demo start view: 28 -> 15 draw calls, identical image | `ModelModule::buildBatches`, `model_instanced.vert`, `shadow_instanced.vert` |
| 24 | Debris budget for broken breakables (default 200 pieces; oldest sleeping piece removed first) | Rule 5. FEMFX's cost follows the number of *awake bodies* (~0.15-0.2 ms each per step on one core), not tets; an eruption keeps adding bodies | `kke_physics_lab volcano`, 1 thread, 1 boulder/s: ~8 ms/step steady with a 60-piece budget; unbounded it only grows | `PhysicsModule::enforceDebrisBudget`, Physics panel slider |
| 23 | Frustum culling of model instances (camera pass) and light-frustum culling (shadow pass); culled characters aren't skinned either | An object entirely outside one frustum plane can't be on screen; a box-vs-6-planes test is ~30 flops | Synty demo start view: 17 of 44 instances skipped (draws 28 instead of ~45) | `kke::Frustum`, `ModelModule::mightBeVisible` |
| 22 | Back-face culling on for physics objects | Every face was shaded twice (BUG-050); closed pieces never show their back faces | Half the fragment work for FEMFX objects (not yet measured on a GPU) | `PhysicsModule::init` |
| 21 | Breakables as plain FEMFX bodies swapped at break time, pieces from Voronoi *snapping* instead of cutting | FEMFX's fracture path (vertex splits, fracture groups, per-tet fracture tests) disappears for them; splitting happens only when a border actually breaks; snapping moves vertices instead of adding tets (cutting was 8-13x the tets) | Lab, 1 thread, brick/plank/pane dropped at 20 m/s: 2.3 ms -> 0.35 ms avg step, 0/80 explosions (was 80/80); max step ~12 ms at the moment of a split (spawning bodies) | `PhysicsModule::Breakable`, `kke::BreakGraph`, `kke::VoronoiFracture`, `tools/physics_lab` |
| 20 | Liquid surface in screen space instead of meshing it | Building a mesh from particles (marching cubes over a density grid) costs CPU every frame and scales with volume; the screen-space method's cost is per *pixel* — one sphere pass, 4 blur dispatches, one full-screen pass — independent of particle count past the sphere pass. Render targets are created once and only rebuilt on resize | Not measured on a real GPU yet (lavapipe here). Half-resolution targets are the backlog knob for min-spec | `kke::FluidSurfaceRenderer` |
| 19 | Marching tetrahedra instead of marching cubes; SDF rebuilt only after melting | 16 cases instead of 256-entry tables, no ambiguity, watertight output (unit-tested); a melting block's surface + distance field only recompute when density changed | 24³ grid: remesh + SDF < 1 ms, and 0 ms on frames where nothing melted | `kke::MeltVolume` |
| 18 | PBF neighbour lists built once per substep, reused by every solver pass | The grid search (27 hashed cells + dedupe) ran 7x per substep; lists make it 1x, passes then walk a flat array | 216-particle test: 2.5 ms -> 0.7 ms per step (3.6x) | `kke::ParticleFluid` |
| 17 | Hot CPU files at -O2 in Debug too (`Texture.cpp`, `VoxelTets.cpp`, `FracturePattern.cpp`) | Same reasoning as FEMFX (BUG-029): pure number crunching nobody steps through; -O0 made mip generation alone ~200 ms per atlas | Part of #16's 300 -> 12 ms | `engine/CMakeLists.txt` |
| 16 | One engine-wide texture cache (`Application::textureSet`) | ModelModule and PhysicsModule each loaded the same 2048² Synty atlas: 2x the GPU memory (21 MB each with mips) and ~200 ms per extra load | Making a prop breakable: 300 ms -> 12 ms total setup (of which texture 220 -> 0 ms) | `Application.cpp` |
| 15 | Point-in-tet search on a uniform grid with precomputed inverse matrices | Brute force was O(points x tets) with a matrix inverse per test; the grid makes it ~O(points) and a barycentric test one mat3 multiply | 4,368-triangle wall: embedding 123 ms -> 1.2 ms | `kke::embedPoints` |
| 14 | Mip maps for every `kke::Texture`, built on the CPU in linear light | Distant texels were sampled from the full 2048² atlas: shimmering, and every fetch a cache miss. Box filter averages in linear space (sRGB bytes averaged directly darken each mip); two LUTs (256 floats decode, 4096 bytes encode) keep it ~10 ms per 2048² image. CPU, not `vkCmdBlitImage`: works on every device regardless of blit format support, and is the same code the asset-cooking step will run offline | Grid overlay stable at distance (it shimmered without). GPU-side win not measured yet — needs a real GPU (HW-011) | `Texture.cpp` |
| 13 | Sandbox budgets: max 6 thrown balls (oldest removed), breakable-prop proxies capped at 48 cells (288 tets) | Rule 5 — anything a player can spam gets a cap and a policy. 48 cells ≈ a Glass Sheet, the worst single object we've measured | Min-spec (1 core): breaking a crate costs ~10 ms/step while 43 pieces move, back to ~0.1 ms once they sleep (~5 s) | `SandboxModule.cpp` constants |
| 12 | Brute-force ray-vs-AABB picking, on purpose | A slab test is ~20 flops; 2,000 objects ≈ 40k flops, far under 0.1 ms. A BVH would be complexity with no measurable win at sandbox sizes — revisit when levels reach ~10k objects | Not measurable in the frame profile | `SandboxModule::pickObject`, `kke/Picking.h` |
| 11 | Asset list uses `ImGuiListClipper` | Only visible rows are submitted, so a 3,000-asset catalog costs the same as 30 | 457-asset Prototype list: no UI cost change vs an empty list | `SandboxModule::assetBrowserUi` |
| 10 | Debug lines: all lines of a frame → one vertex buffer, 1 draw per layer | Camera-facing quads built on the CPU (no geometry shader, no wide-line feature needed, works on lavapipe/mobile); buffer per frame in flight, grown to high-water mark and reused — zero allocations once warm | Sandbox grid + boxes (~110 lines): 2 draw calls | `DebugDrawModule` |
| 9 | Box-projected UVs from rest positions for physics pieces | Correctness fix, but done without extra passes: computed in the same loop that builds the vertex, from data FEMFX already stores | No measurable cost (render prep unchanged within noise) | `PhysicsModule::prepareRenderData`, BUG-036 |
| 8 | CPU skinning only when drawn, once per frame | `skinnedFrame` guard: the shadow pass and main pass share one skinning result | 4 Synty characters: skinning not visible in profile | `ModelModule::skinInstance` |
| 7 | Premultiplied alpha + sRGB-correct UI | Correctness, but also removes a per-pixel divide from the naive fix path | — | BUG-021 |
| 6 | Scene capacities sized for fracture | Correctness: dropped contacts were lost work, not saved work | Fracture no longer silently capped at 64 pieces | BUG-027 |
| 5 | One vertex buffer per frame in flight | Removes a CPU/GPU race without a stall (no `vkDeviceWaitIdle`) | — (correctness, zero cost) | BUG-026 |
| 4 | Draw only exterior tet faces; skip sleeping objects | Interior faces can never be seen; sleeping objects can't have changed | ~10x fewer triangles; settled pile render prep → ~0.08 ms | BUG-026, PERFORMANCE_NOTES |
| 3 | Cap catch-up physics ticks at 2/frame | Stops the "spiral of death": one slow tick became 8 per frame | Baseline 0.4 FPS → usable slow motion under overload | BUG-030 |
| 2 | FEMFX always -O2, even in Debug | The solver is dense SIMD; -O0 turns intrinsics into calls + spills | Glass-sheet step 200–400 ms → ~25 ms (1 core) | BUG-029 |
| 1 | FEMFX sleeping on; ground = collision plane, not a rigid body | Settled objects stop simulating; the kinematic ground body re-woke everything each step | Settled 475-piece pile: ~95 ms → ~0.2 ms per step (1 core); dev box 0.06 ms | BUG-028 |

**Headline numbers so far** (scripted physics benchmark, 1200 ticks):

| Machine | Before | After |
|---|---|---|
| Min-spec emulation | 0.6 FPS, 235 ms avg step (76 pieces) | 11 FPS, 21 ms avg step (475 pieces) |
| Dev box (release) | — | 1.00x realtime, 1.38 ms avg step, 5.77 ms max (484 pieces) |

---

## 5. Backlog (ranked by expected win on min-spec)

1. **Debris budget + hand-off to GPU particles** — worst-case physics cost
   is "many awake pieces right after a big break" (~55 ms/step for ~475
   pieces on 1 core).
2. **Debris budget** — after a break, pieces take ~7 s of *simulated*
   time to fall asleep (measured: 67 pieces, speeds decaying normally,
   FEMFX's default sleep thresholds). On one core the simulation runs in
   slow motion, so that's ~15-20 s of 10-20 ms steps. Candidates: faster
   settling for small pieces (damping / sleep thresholds per piece size in
   our FEMFX fork), a cap on awake debris, tiny pieces handed to GPU
   particles. The physics benchmark's awake count swinging 2 <-> 390 is a
   separate lead: plastic objects that creep forever and re-wake the pile.
3. **Frustum culling + instancing for props** — Synty levels are hundreds
   of repeated meshes.
4. **Mip maps** — texture bandwidth and shimmering.
5. **Vertex quantization for static meshes** — half the vertex memory
   and bandwidth, the N64 way.
6. **GPU skinning** — for crowds.
7. **Asset cooking** — bake mips, compression (#39), collision and fracture
   data at import, not at load.

## 6. How to add an entry

1. Run the relevant benchmark *before* the change and keep the report
   file (`benchmark/*.json`).
2. Make the change; comment the trick in the code, pointing here.
3. Run the benchmark *after*, same machine, same build type.
4. Add a row to §4 (and update §5 if the backlog changed). If it didn't
   help, write that down too, and revert it.
