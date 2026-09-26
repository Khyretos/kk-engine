# SCALING.md — worst cases, multiplayer, platforms: what's realistic

The question (2026-09-26): *a player runs from an erupting volcano,
parkouring while it throws giant rocks and lava. Does that perform? And
with 4-player split screen, or 40 players online (5 PCs on Linux/Windows/
Mac each running 4-player split screen = 20, plus 10 phones and 10
browsers)? Are we truly optimized for that?*

**Short answer.** Not yet, and it isn't only an optimization problem.
Parts of the answer are measured below; the rest is architecture the
engine doesn't have yet (a rigid-body layer, networking, multi-view
rendering, non-x86 builds). None of it is out of reach; all of it is
known engineering. This file says what exists, what the numbers are,
and the order to build the rest in. Numbers come from the tools named
next to them, so anyone can re-run them.

---

## 1. What the numbers say today

### Physics: the volcano (`kke_physics_lab volcano`)

Breakable boulders (0.5-1.4 m, 8-14 Voronoi pieces each, ~160 tets)
thrown from a crater onto a 40 x 40 m area, debris budget on, lava as
3,000 PBF particles. One core of the CI-class machine this was written
on (per-step cost; 16.7 ms = a whole 60 Hz frame):

| Boulders/s | Debris budget | Threads | Avg step | Worst step |
|---:|---:|---:|---:|---:|
| 1 | 60 pieces | 1 | ~8 ms | 12 ms |
| 2 | 150 | 1 | ~10 ms | 20 ms |
| 2 | 150 | 4 | ~10 ms | 12 ms |
| 4 | 60 | 1 | ~29 ms | 21 ms+ (falls behind) |
| 1 + 3,000 lava particles | 60 | 1 | ~11 ms | 26 ms |

What it means:
- **FEMFX's cost follows the number of awake bodies**, ~0.15-0.2 ms each,
  much more than their tet count. A volcano keeps creating awake bodies.
- **Threads barely help** here (FEMFX parallelizes inside big islands;
  lots of small, separate rocks don't split up well).
- **FEMFX is the wrong tool for a rock storm.** It's a soft-body/FEM
  engine for a handful of hero objects (a car crumpling, a door
  bending, a glass pane cracking). Boulders, rubble, props and players
  need a rigid-body engine, which is 10-100x cheaper per body. Every
  shipping engine splits it this way (Unreal: Chaos rigid + cloth/flesh
  separate; Roblox: rigid bodies only).

### Physics: the scripted demo scene (your dev box, HW-010, 2026-09-26)

Ryzen 7 9800X3D, 8 threads: 1.00x realtime, 3.9 ms avg step, 12.6 ms max,
481 pieces, 104 MB RAM. Fine for one machine, one scene.

### Rendering

Measured today: frustum culling (Synty demo start view: 17 of 44
instances skipped), exterior-faces-only for physics objects, sleeping
objects not rebuilt, mip maps, one texture cache. **Not yet built:**
instancing (one draw per model part, not per instance), mesh LODs,
shadow cascades / shadow distance, occlusion culling, GPU-driven
rendering. On lavapipe (software) nothing here is meaningful; the dev-box
GPU numbers (HW-013, HW-014) are the ones that count.

---

## 2. Scenario by scenario

### A. Volcano parkour, one player, one machine — **doable, with changes**

- Rocks, rubble, props, the player: **rigid bodies** (new, see §3).
  Rocks break with the same Voronoi pieces and `BreakGraph` borders, as
  *rigid* pieces (convex hulls), the way Chaos does it. Debris budget as
  now. Small debris can become GPU particles once it's out of play.
- FEMFX for 1-3 hero objects at a time (a bridge bending, a metal gate
  denting), near the player only ("physics LOD": far objects frozen).
- Lava: gameplay uses a cheap height field / damage volumes; the PBF
  particles are visual, near the camera only, with a budget (3,000 costs
  ~3 ms on one core). Lava *rivers* in the distance are a shader.
- Min-spec (1 core, 2 GB): a reduced tier (fewer rocks, no hero FEM,
  lower particle budget). Realistic target: 30 fps.

### B. 4-player split screen on one PC — **doable, needs a multi-view renderer**

- Physics and gameplay run once; only rendering is per player.
- The renderer draws one camera today. Needed: per-view camera data
  (the lighting UBO holds one view-projection), viewports/scissors,
  per-view culling (culling is now per call, so that part is cheap to
  extend), UI per viewport, one input device per player (SDL3 has
  gamepads).
- Cost: roughly 4x the draw calls and fragment work at 1/4 resolution
  each; shadows can be shared if the shadow map covers all players.
  Instancing and LODs matter much more here, since 4 views multiply
  every draw call.
- On min-spec: 2 views, not 4.

### C. 40 players online, mixed platforms — **possible, it's the biggest item**

How games of this kind do it (Roblox, Fortnite, Rust, Valheim):
- **Authoritative dedicated server** (a headless Linux build: no
  window, no GPU; the engine already runs headless for tests). The
  server runs gameplay and the gameplay-relevant physics.
- **Clients predict their own player** and interpolate everyone else
  from server snapshots (~20-30 Hz). 40 players x ~50 bytes x 20 Hz is
  about 40 KB/s per client: small.
- **Destruction is mostly cosmetic.** The server sends *"object 812
  broke: these borders, this impact"* (a few bytes). Each client
  replays the break with the same Voronoi pieces, because the pieces
  come from `kke::fractureSeed(world, object)` and are identical
  everywhere. The *debris* doesn't need to match between clients (and
  won't: floating-point physics isn't bit-identical across CPUs and
  thread counts). Only pieces that matter for gameplay (a bridge span,
  a wall that blocks a path) get server-owned positions. This is how
  Fortnite replicates building damage.
- **Lava/water:** gameplay volumes on the server, visuals per client.
- **Per-platform quality tiers:** the server is the same for everyone;
  what varies is the client's render and debris budgets.
- A 5-PC x 4-split-screen setup is just 5 clients with 4 local players
  each: the protocol carries several players per connection.
- Library choice (to decide when we get there): GameNetworkingSockets
  (Valve, BSD) or yojimbo/netcode (BSD), both UDP with reliability and
  encryption; ENet as the simplest.

### D. Phones and browsers — **real work, in this order**

| Target | Graphics | Blocker | Route |
|---|---|---|---|
| Windows | Vulkan | none known | MinGW cross build in Docker (this session), MSVC in CI next |
| Linux | Vulkan | none | Docker + CI (working) |
| Android | Vulkan | **FEMFX is x86-AVX only** (Vectormath, SoA collision, solver) | SIMDe (MIT, header-only) maps AVX to NEON; APK packaging via SDL3's Android project |
| macOS / iOS | MoltenVK (Vulkan on Metal) | Apple SDK can't be containerized (licence); FEMFX AVX (Apple Silicon is ARM) | GitHub Actions macOS runners; SIMDe as for Android |
| Browser | **no Vulkan in browsers** | needs a WebGPU renderer; FEMFX threads need SharedArrayBuffer (cross-origin isolation) | a render-backend layer (Vulkan + WebGPU) + Emscripten; SIMDe -> WASM SIMD; the largest item on this list |

Phones and browsers would run the low tier: rigid-body debris only, no
hero FEM (or a reduced one), fewer particles, lower resolution.

---

## 3. The plan, in order

1. **Rigid-body layer** (biggest win for the volcano and for multiplayer):
   Jolt Physics (MIT; Horizon Forbidden West, Godot 4), a swappable
   module next to FEMFX. Rigid breakables using our Voronoi pieces and
   `BreakGraph`, a character controller, physics LOD.
2. **Render scaling:** instancing, mesh LODs, shadow distance/cascades,
   then multi-view (split screen).
3. **Networking module** + headless server build + break events.
4. **Platforms:** Windows (done in Docker, MSVC next), Android (SIMDe
   port of FEMFX), macOS/iOS (CI runners), Web (WebGPU backend).
5. **Animation** (the Synty animation library you sent): blend trees,
   state machines, IK, root motion, as its own demo, alongside the above.

Each step comes with its lab or benchmark scenario, so "is it fast
enough" stays a number, not a feeling.

## 4. Re-running the numbers

```bash
cmake --build build --target kke_physics_lab
build/bin/kke_physics_lab volcano 20 1 2 150 0     # seconds threads rocks/s budget lava
build/bin/kke_physics_lab shoot voronoi 18         # breaking a crate
build/bin/kke_physics_lab fracture all 20          # stability of every pattern
taskset -c 0 build/bin/kke_physics_lab volcano ... # pinned to one core (min-spec CPU)
```
