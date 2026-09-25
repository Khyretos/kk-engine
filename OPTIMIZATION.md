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
| Spatial partitioning (grid / BVH) | 90s | Culling, picking, placement in the sandbox | backlog |
| Level of detail (mesh LOD, impostors) | 90s | Synty packs ship LODs in some packs; impostors for distant props | backlog |
| Texture atlases | N64/PS1 | Synty packs are built around one atlas per pack — keep it that way: one texture bind for a whole pack | ✅ natural fit |
| Mip mapping | 90s | All textures: less aliasing *and* less bandwidth | in progress |
| Texture compression (BCn/ASTC) | 2000s | Asset cooking step | backlog |
| Vertex quantization (16-bit) | N64/PS1 | Static meshes: positions as int16 + per-mesh scale, normals as oct-encoded 2×8 bit | backlog |
| Instanced draws | 2000s | Repeated props (Synty levels are 90% repeats) | backlog |
| Baked / vertex lighting | PS1/N64 | Optional "retro/min-spec" lighting path | idea |
| Debris budget / particle hand-off | 2010s (Chaos, RayFire) | Small/far fracture pieces → GPU particles | backlog (PERFORMANCE_NOTES.md #1) |
| Clustered fracture | 2010s (Chaos) | Material-driven fracture patterns | in progress |
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
2. **Clustered, material-driven fracture** — fewer, bigger pieces is both
   cheaper and better-looking (in progress).
3. **Frustum culling + instancing for props** — Synty levels are hundreds
   of repeated meshes.
4. **Mip maps** — texture bandwidth and shimmering.
5. **Vertex quantization for static meshes** — half the vertex memory
   and bandwidth, the N64 way.
6. **GPU skinning** — for crowds.
7. **Asset cooking** — bake mips, compression, collision and fracture
   data at import, not at load.

## 6. How to add an entry

1. Run the relevant benchmark *before* the change and keep the report
   file (`benchmark/*.json`).
2. Make the change; comment the trick in the code, pointing here.
3. Run the benchmark *after*, same machine, same build type.
4. Add a row to §4 (and update §5 if the backlog changed). If it didn't
   help, write that down too, and revert it.
