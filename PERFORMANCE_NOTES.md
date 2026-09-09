# PERFORMANCE_NOTES.md — what RayFire and Chaos actually do, and what it means for KKE

This file exists because the physics demo's performance is genuinely bad
right now, and rather than guess at fixes, this is a real study of how two
production destruction systems (RayFire, a mature Unity/3ds Max plugin;
Chaos, Unreal's own in-house solver) actually keep hundreds of fracturing
objects fast. **Nothing here is copied** — no code, no assets, no direct
ports. This is architecture-level notes: the *shape* of the optimization,
re-derived and re-implemented for KKE's own FEMFX-based, Vulkan-based
stack. Sources: RayFire's own component documentation (Rigid, Connectivity,
Debris, Dust, Shatter, Unyielding — user-provided), and Epic's public Chaos
Destruction documentation plus their own published performance writeups.

## The one honest fact that has to come first

**This project's own dev sandbox has exactly 1 CPU core** (confirmed via
`nproc` while investigating this — not assumed). `PhysicsModule`'s own
thread pool sizing (`std::thread::hardware_concurrency()`) correctly
detects this and falls back to fully synchronous execution — which is
*correct behavior*, not a bug, but it means every FPS number measured in
this sandbox so far reflects one CPU core doing physics, rendering
(lavapipe, a *software* Vulkan renderer), and everything else, serially.
That is close to worst-case, not representative of target hardware (a
real GPU plus a multi-core CPU). This doesn't mean "nothing to optimize"
— see below, both RayFire and Chaos still need real optimization work on
real hardware with real GPUs and many cores — but it means the *absolute*
FPS numbers from this sandbox shouldn't be read as "this is how the
engine performs," only "this is how the engine performs in the worst
environment available to test it in." Real profiling on real target
hardware is still a real, separate task worth doing before drawing
conclusions about production viability.

## The most important, most surprising fact from researching this

**Neither RayFire nor Chaos runs the actual physics solve on the GPU.**
Chaos's own threading model (`EChaos ThreadingMode`) is `SINGLE_THREAD` or
`TASK_GRAPH` — a multi-threaded **CPU** task system, the same category of
thing as this project's own `ThreadPool`. RayFire, similarly, is built on
Unity's CPU-based PhysX. GPU involvement in both systems is specifically
for the *visual* result — particle-rendered debris, Niagara meshes for
Chaos, native Unity particle systems (pooled) for RayFire — never for the
rigid/deformable-body solve itself. This means FEMFX being a CPU solver
isn't an architectural mistake or a wrong library choice; it's the
industry-standard shape of the problem. **The real performance wins in
both systems come from doing less work, not from moving the same work to
a different processor** — see below.

## What RayFire actually does (from its own component docs)

- **Bake expensive structural data once, not every frame.** RayFire's
  "Editor Setup" button explicitly caches connectivity data, ignore-pairs,
  and collider setup *at initialization*, described in their own docs as
  "the most performant operation." The lesson: anything derivable purely
  from an object's static shape (connectivity graph, broad-phase ignore
  pairs) should be computed once at spawn/load, never recomputed per
  frame or per fracture event.
- **A real three-state (at minimum) simulation model, not just "on."**
  RayFire's Rigid component has `Dynamic` / `Sleeping` / `Inactive` /
  `Kinematic` / `Static`. Only `Dynamic` objects pay full simulation cost
  every tick. `Inactive` objects are frozen (no gravity, near-zero cost)
  until something *activates* them. This is the single biggest lever:
  most of a fractured structure, at any given moment, should not be
  paying full simulation cost at all.
- **Connectivity + "Unyielding" anchors decide *what* activates, not a
  timer or a flat rule.** A shard stays `Inactive` unless a connectivity
  check finds it's lost its path back to a designated "Unyielding" (fixed,
  load-bearing) shard — e.g. the ground, or a wall's foundation. Only the
  specific piece that lost support gets promoted to `Dynamic`. This is
  exactly the missing piece in KKE's own fracture scenes right now: every
  fragment becomes fully simulated the instant it exists, whether or not
  it's actually falling.
- **Object pooling for anything expensive to create.** RayFire's Debris
  and Dust components explicitly pool Unity Particle Systems rather than
  instantiate/destroy them per event, with real, tunable knobs:
  `Warmup` (pre-create before they're needed), `Capacity` (hard cap),
  `Rate` (create at most N new pooled objects per frame, not a burst),
  `Skip` (if the pool runs dry, degrade gracefully — use what's available
  — rather than force-creating everything in one frame and spiking).
  `Overflow` lets the pool temporarily grow under real demand, then
  shrink back. This maps directly onto KKE's own per-fragment GPU buffer
  (`vertexBuffer`/`indexBuffer`) allocation — right now, every new
  fragment from a fracture event allocates fresh VMA-backed buffers,
  which is exactly the "instantiate every time" cost RayFire's docs call
  out as "very resourceful" (expensive) and pool specifically to avoid.
- **Simplified colliders by default, full mesh only when it matters.**
  RayFire's automatic collider type defaults to a choice between
  `Mesh` / `Sphere` / `Box` / `None` — a full concave mesh collider is
  the *most* expensive option, used only when shape fidelity actually
  matters for that object. `Planar Check` specifically avoids attaching
  mesh colliders to thin/planar shapes (like a glass sheet) because
  they're both expensive and behave badly at that aspect ratio.
- **Broad-phase filtering for objects that spawn already touching.**
  `Ignore Near` explicitly disables collision pairs between shards whose
  bounding boxes already overlap at spawn time — the exact situation
  every one of KKE's own fracture scenes creates (a shattered object's
  pieces all start adjacent to each other), and a likely real, direct
  contributor to the contact-count/instability cost seen during fracture
  events.
- **Real Voronoi fracture, not a uniform grid.** RayFire's Shatter tool
  scatters a point cloud (with density, center-bias, and divergence
  controls) and computes a Voronoi diagram from it, producing irregular,
  natural-looking fragments. KKE's own `buildGridBox()` produces uniform
  cube-cells — functionally fine for proving fracture mechanics work, but
  visually nothing like real broken glass or rock. This is a real,
  separate, valuable piece of future work independent of performance.

## What Chaos actually does (from Epic's own docs and performance writeups)

- **Hierarchical/clustered fracture, not flat.** A Geometry Collection can
  break into large pieces, which can break into smaller pieces, which can
  break into smaller pieces still — simulated and rendered at whatever
  level of the hierarchy is currently relevant, not always at maximum
  fragmentation. This is a real LOD concept for destruction geometry
  itself, not just for rendering.
- **A hard, global cap on simultaneously active bodies.** A real,
  published example (a 105-statue destructible scene) explicitly keeps a
  "global budget manager" that never exceeds 200 simultaneously active
  rigid bodies, regardless of how many fragments visually exist — extra
  fragments beyond the budget are handled differently (see below), not
  simulated. This is a directly transferable, concrete number-based
  pattern: **cap the count of fully-simulated FEMFX objects, and have an
  explicit policy for what happens to fragments beyond the cap**, rather
  than simulating every fragment that exists unconditionally.
- **"Remove on Break," replacing small/settled debris with GPU-rendered
  particles instead of keeping them as physics bodies.** This is the
  single most directly-applicable idea found in this whole investigation:
  once a fragment is small enough (or has settled, or is far enough from
  the camera/player) it is deleted from the physics solver entirely and
  replaced with a Niagara (GPU) particle or mesh that just visually
  exists — no more rigid-body simulation cost for it at all. **KKE
  already has exactly the tool this needs**: `ParticleModule` is a real,
  proven GPU-driven particle system. Small/settled fracture fragments
  are a real, natural candidate to hand off to it instead of staying as
  live `FmTetMesh` objects forever.
- **Sleeping/disabled velocity thresholds.** Chaos Particles below a
  configurable linear/angular velocity threshold get put to sleep
  (removed from active simulation) automatically, and woken by a new
  collision. This is the same idea as RayFire's `Sleeping` state, from a
  different codebase — strong signal it's a broadly load-bearing pattern,
  not an implementation detail specific to one engine.
- **Cached/recorded simulation for non-interactive destruction.** Epic's
  own docs recommend switching from live simulation to a pre-recorded,
  played-back simulation specifically for destruction that happens in
  the background or doesn't involve player interaction — the physics is
  solved once (e.g. at build/bake time), recorded, and *played back* at
  runtime for a fraction of the cost. Directly relevant to any KKE scene
  where the same destruction sequence repeats (a demo loop, a scripted
  cutscene-style moment) rather than needing to be freshly simulated
  every single time.
- **A Field System for batch effects, not per-object logic.** Fields let
  Chaos apply forces/state changes (strain, velocity, activation) to
  every particle in a volume in one pass, evaluated at each particle's
  position, rather than looping per-object in gameplay code. A real,
  data-oriented alternative to "for each object, check a condition, do a
  thing" — worth keeping in mind if KKE's own per-object update loops
  start showing up as a real cost in profiling.
- **Multi-threaded CPU solve via a task graph, explicit thread
  separation (game/physics/render).** Confirms the "GPU doesn't do the
  actual solve" finding above, and confirms real multi-threading (not
  single-core) is where Chaos gets its own scaling — the same shape of
  optimization KKE's own `ThreadPool` is already aimed at, just not yet
  verified to be doing meaningful, well-distributed work under real
  fracture load (worth profiling directly, not assumed).

## What this means for KKE, concretely — real, actionable next steps

Roughly in order of expected impact vs. effort, **none of this done yet,
all of it a real, separate task**:

1. **Cap simultaneously-active FEMFX objects with an explicit budget**,
   and hand fragments beyond the cap (or fragments below a size
   threshold, or fragments that have settled — velocity near zero for N
   ticks) off to `ParticleModule` as visual-only GPU particles instead of
   live physics bodies. This is the single highest-leverage change found
   in this whole investigation, and KKE already has the GPU particle
   system this needs.
2. **Pool `vertexBuffer`/`indexBuffer` allocations for fragments**
   instead of allocating fresh VMA buffers per fragment per fracture
   event — the exact cost RayFire's own Debris/Dust pooling exists to
   avoid, and a likely real, direct contributor to the FPS drop measured
   right at the moment a fracture event happens (a burst of many
   simultaneous new allocations).
3. **Add a real sleep/inactive state** for FEMFX objects at rest (low
   velocity for N consecutive ticks) — skip their own render-time vertex
   regeneration and re-upload (`PhysicsModule::render()`'s own per-frame
   `FmGetVertPosition()` readback and buffer re-upload) when nothing
   about them has actually changed, not just conceptually "let them
   settle."
4. **Filter broad-phase contacts for freshly-fractured, still-adjacent
   pieces** — likely a real, direct contributor to the instability/cost
   seen right when a fracture event happens, matching RayFire's own
   `Ignore Near` reasoning exactly.
5. **Profile `ThreadPool` utilization directly** under real fracture load
   rather than assuming it's already helping — this sandbox's 1-core
   limit makes this untestable *here*, but the code path and its actual
   task distribution are worth a real, direct look (are enough
   independent units of work actually being submitted per frame to be
   worth parallelizing at all, on real multi-core hardware?).
6. **Real Voronoi fracture** (scattered point cloud → Voronoi cells) to
   replace `buildGridBox()`'s uniform grid — a visual-quality
   improvement, not a performance one, but real, separate, valuable
   future work in the same area.
7. **Cached/recorded playback** for any KKE scene where the same
   destruction sequence would otherwise be freshly re-simulated
   repeatedly (e.g. a demo that loops) — lowest priority here since
   KKE's current scenes are all genuinely interactive/one-shot, but worth
   keeping in mind as the engine grows scripted/repeated destruction use
   cases.

None of items 1-4 have been implemented yet — this file is the research
and plan, not a changelog of work already done. See `ROADMAP.md`'s
Physics section for current status and `BUGS.md` for the active,
unresolved hollow-tetrahedron rendering investigation, which is a
separate, correctness (not performance) issue being worked in parallel.
