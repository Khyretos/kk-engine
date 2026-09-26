# ACTION_PLAN.md — everything that's been asked for, in order

Ordered by one rule from the owner: **priority 1 is always the base
engine and a playable demo where a character walks around and interacts
with every system.** Everything else is ranked by how much it moves that
forward, then by performance on the 1-core floor. Status: ✅ done,
🔨 in progress, ⬜ not started. Update this file as items move.

Detailed design notes live in the linked files; this is the index.

---

## P1 — Base engine + the walkable showcase (kke_demo)

| # | Item | Why it's P1 | Status |
|---|---|---|---|
| 1.1 | **Rigid-body + collision layer (Jolt Physics, MIT)** as a module next to FEMFX | Nothing static collided (placed Synty meshes were only visual; FEMFX knows a ground plane only). Also the scaling fix: 1,000 boxes = 1.56 ms/step on one thread | ✅ core (`kke::RigidWorld`, 7 tests); FEMFX<->Jolt bridge next |
| 1.2 | **Character controller**: capsule, walk/run/jump/crouch, slopes, stairs, push objects, ride platforms | The "walk around" | 🔨 walk/slopes/stairs/jump/push/platforms done in `RigidWorld` (tested) and playable in kke_demo; crouch resizes the capsule (`setCharacterHeight`, refuses to stand under a ceiling; tested, and a low roof in kke_demo) ✅; `kke::Locomotion` (MOVEMENT.md, 12 tests): vault/climb from dynamic area awareness, inertial turning, air control, floor tiers, coyote time + jump buffer; parkour lane in kke_demo ✅ |
| 1.3 | **Camera system**: first person, third-person spring arm (collision-aware, lag), orbit/editor, cinematic rails; **viewports** (split screen 1-4, picture-in-picture) | Asked for; split screen is also multiplayer groundwork | 🔨 `kke::CameraRig` (first/third person spring arm, orbit, cinematic; 4 tests) used by kke_demo (V toggles, wheel = arm length); viewports/split screen next |
| 1.4 | **Showcase level (kke_demo)**: Synty level + interaction stations: break props (FEMFX + Voronoi), pour lava/melt, water with floating things, ragdolls, lighting controls, RmlUi HUD/menus, ImGui debug | The demo that proves every system at once | 🔨 v1 playable: `games/showcase/` is now `kke_demo` (old one renamed `kke_basics`). Level with stairs, ramp, too-steep slope, pillars, moving platform, 18 Jolt crates (E/right click pushes), FEMFX breaking yard (glass, plank, stone wall; F/left click shoots), lighting + camera panels, F1 engine panels. Next: lava/melt + water stations, FEMFX<->Jolt bridge, RmlUi HUD, Synty level art |
| 1.5 | **Animation basics** with the Synty animation library you sent: locomotion blend (idle/walk/run), state machine, root motion option, foot IK | A character that walks needs to *look* like it walks. Full animation demo is P2 (2.6) | 🔨 `kke::Animator` (6 tests) drives the UAL mannequin in kke_demo: idle/walk/jog/sprint blend space synced to speed, crouch blend, jump start/loop/land, vault/climb stand-in poses (UAL Standard has no vault clips). Now also foot IK (hips drop on stairs/slopes), hand IK onto vault/climb edges, root motion as data, and any Synty SK_ character wearing the UAL clips (retargeted by bone name; kke_demo Character panel). `kke/AnimRig.h`, 9 tests. Next: foot slope rotation, real vault/climb clips |
| 1.6 | **Resource governor**: frame cap, physics/worker thread cap, render scale, "use everything" toggle, all in Settings | "Run with what it needs; take everything only if the user says so" | ✅ `kke::computeBudget` (ResourceGovernor.h): half the usable cores for physics workers (1..8), 144 fps cap with vsync off, 15 fps in the background, render scale 0.5..1 (3D drawn smaller and upscaled, UI stays sharp via the new `Module::renderOverlay`), "use everything" lifts it. `performance` section in settings.json; Performance panel in the showcase; `KKE_USE_EVERYTHING=1`. OPTIMIZATION.md #26 |
| 1.7 | **Built-in stress test + benchmark log for end users** (like the build log: hardware + fps/frame times, one file) | Must always ship with the engine | 🔨 partly (physics benchmark exists; needs a graphics+physics scene and one report) |
| 1.8 | Natural fracture, seeds, debris budget, culling, instancing | Done this session (BUGS 045-051, OPTIMIZATION 21-25) | ✅ |
| 1.9 | **Input system** (asked for: every controller type, identical HOSAS sticks, Steam Controller gyro/paddles, input tester in RmlUi, controller menu navigation, Tarkov-style robust binding, left-handed) | Hardcoded keys block left-handed players; controllers are table stakes | ✅ `InputMap` + `InputDevices` + `InputModule` (17 tests), RmlUi Input screen, UI navigation, kke_demo on actions, left-handed mirror, virtual devices in CI. See INPUT.md. Next: per-player device assignment UI for split screen, gyro calibration/"ratchet" button, rumble on impacts |

## P2 — The systems that make it a platform

| # | Item | Notes | Status |
|---|---|---|---|
| 2.1 | **Networking module** | See "Networking" below: transport + rollback + voice, many peers per PC, dockerized server, lots of tests | ⬜ |
| 2.2 | **Audio engine** | See "Audio" below: physics-driven sound, occlusion by material, accessibility | ⬜ |
| 2.3 | **Accessibility layer** | Sound visualization for deaf players (direction, intensity, material tags, user-tunable), audio cues + material-distinct sounds for blind players, captions, remappable input, contrast/size | ⬜ |
| 2.4 | **Benchmark images + BENCHMARKS.md** | See "Hardware profiles" below | ⬜ |
| 2.5 | Cross-platform: Windows (MinGW in Docker: in progress; MSVC CI), Android (FEMFX SIMDe port), macOS/iOS (CI runners), Web (WebGPU backend) | SCALING.md §D | 🔨 |
| 2.6 | **Animation demo**: blend trees, state machines, IK (two-bone, look-at, foot placement), root motion, retargeting across Synty characters | Uses the Universal Animation Library you sent | ⬜ |

## P3 — Look and feel ("performance and feel are king")

| # | Item | Technique (chosen for cost on low-end first) | Status |
|---|---|---|---|
| 3.1 | Ambient occlusion | GTAO at half resolution (Jimenez 2016), SSAO fallback on the low tier | ⬜ |
| 3.2 | Sky | Physically based sky (Hillaire 2020 LUT method: a few tiny LUTs, cheap), plus cubemap skyboxes | 🔨 simple gradient sky exists (sea demo) |
| 3.3 | Smoke / fog that reacts to wind and players | Froxel volumetric fog (Wronski 2014) + a small 3D velocity grid (stable fluids, Stam) fed by physics bodies | ⬜ |
| 3.4 | Particle system v2 + demo | Emitter/module model like Unity VFX/Niagara but simple: spawn, forces (wind, vortex, physics colliders), color/size over life, sub-emitters; GPU compute path + CPU fallback | ⬜ |
| 3.5 | "Almost ray tracing" GI | Screen-space GI + reflections first; then Radiance Cascades (Sannikov 2023) or DDGI probes (Majercik 2019); hardware RT only as an optional tier | ⬜ |
| 3.6 | World streaming / chunks, LODs, choice per game | "Small world: load all" vs "streamed world: chunks by distance, async loading, LOD per chunk", picked in game.json and scaled by hardware tier | ⬜ |
| 3.7 | Shadow quality | Cascaded shadow maps with distance cap per tier | ⬜ |

## P4 — Demos that prove the engine

| # | Item | Notes | Status |
|---|---|---|---|
| 4.1 | **Procedural land demo (no cube voxels)** | Fractal noise (fBm octaves) heightfield + 3D density for caves/overhangs/floating islands, hydraulic + thermal erosion, meshing with Surface Nets / Dual Contouring (smooth, sharp-feature-preserving, fewer triangles than marching cubes), houses and nature placed by rules; terrain chunks breakable/deformable, FEMFX for props | ⬜ |
| 4.2 | Sea, melt, sandbox, synty, rmlui demos | Exist; keep them working | ✅ |

---

## Answers to the questions

### JSON or YAML?
**Keep JSON**, and make it stricter rather than switching:
- YAML is more fragile, not less: indentation decides structure, implicit
  types bite (`no` becomes `false`, `1.10` becomes `1.1`, the "Norway
  problem"), the spec is huge, and parsers differ from each other. Several
  YAML loaders have also had code-execution vulnerabilities through tags.
  For something every module and every game talks through, fewer surprises
  wins.
- JSON has one tiny spec, the same meaning in every language, fast parsers,
  and it's what the engine already uses (nlohmann/json).
- What people like about YAML we can have in JSON: **comments** (allowed
  when reading, via `ignore_comments`), **JSON Schema** validation of every
  `game.json` / layout / module config with clear error messages, and
  versioned formats (`"format"`, `"version"`) — already the convention here.

### Can everything run on both GPU and CPU? No crash without a GPU?
- **Rendering needs Vulkan, but not a GPU**: with no GPU, Mesa's
  *lavapipe* is a CPU implementation of Vulkan. That's exactly how this
  engine has been developed and tested (the 1-core VM). With no Vulkan at
  all, it stops with a clear message instead of crashing (VulkanCheck).
  Next: bundle/offer the software fallback and pick it automatically.
- **Physics, fluids, melting, buoyancy are CPU** today (FEMFX is CPU-only
  by design). The GPU particle system has a compute path. Moving more to
  compute (particles, fluids, cloth) is planned per item, always with a
  CPU fallback.
- **Not taking 100%**: 1.6 (resource governor, done) is the answer: frame
  cap, thread cap, background cap, render scale, and an explicit "use
  everything" switch (settings.json `performance`).

### Do we have culling / chunk loading?
- **Yes now**: frustum culling (camera and shadows), instancing, only
  exterior faces for physics objects, sleeping objects skipped.
- **Not yet**: world streaming/chunks, LODs, occlusion culling, cascaded
  shadows (3.6, 3.7). The per-game choice goes in `game.json`, and the
  engine picks defaults from the detected hardware tier.

---

## Networking (2.1) — plan

Goals from the brief: every game type (competitive, co-op, fighting,
MMO-ish), peer-to-peer *and* dedicated servers (Docker), several
instances/servers on one PC talking to each other, couch co-op (2+
players per client), voice chat, robust first (security designed in,
hardened later), FOSS, every OS.

### The candidates, checked (2026-09-26)

| Library | What it is | Licence | Verdict |
|---|---|---|---|
| **ENet** | Reliable + unreliable channels over UDP, C, tiny, every OS | MIT | **Default transport**: LAN, local multi-instance, host-a-game P2P, couch+online |
| **yojimbo** (= **netcode** + **reliable** + **serialize**) | Secure client/server: connect tokens, encrypted packets (libsodium), reliable messages, bit-packed serialization | BSD-3 | **Dedicated-server transport** (Docker server images, matchmaker-issued tokens) |
| **netcode**, **reliable**, **serialize** | The three layers yojimbo is built from, usable alone | BSD-3 | Used through yojimbo; `serialize`'s pattern for all our packets |
| **GameNetworkingSockets** (Valve) | Reliable/unreliable messages, encryption, **P2P with ICE NAT traversal and relays** | BSD-3 | **Internet P2P backend**, later (heavier: protobuf + OpenSSL/libsodium) |
| **GGPO** | Rollback netcode (fighting games), the reference implementation | MIT | **Rollback layer**: port its algorithm cross-platform (the repo is Windows-first); needs a deterministic game state |
| **fixed** (mas-bandwidth) | Deterministic Q48.16 fixed-point maths, bit-identical on every CPU/compiler (from Box3D) | MIT | **Determinism for rollback/lockstep** game state (FEMFX debris stays cosmetic float) |
| **KCP** | ARQ protocol: lower latency than TCP-style reliability at ~10-20% more bandwidth | MIT | Optional reliable-channel mode for latency-critical streams |
| **proton** (mas-bandwidth) | **Not Valve's Proton** (the Windows-on-Linux layer): a Linux kernel module for crypto inside XDP packet filters | GPL-3 | Not for the engine (GPL, kernel-only); only relevant to huge server fleets |
| **Network Next** | Paid-at-scale route acceleration (free under 10k peak players, self-hosted) | own terms | Optional plug-in later, never a dependency |

About Valve's Proton (Windows games on Linux): our games are native on
Linux, so it isn't needed; Windows builds of KKE games should still run
under Proton (Wine: the Windows build's unit tests already run under
Wine in `docker/windows.Dockerfile`).

### Proposed layers (each a separate, replaceable module)
1. **Transport interface** with backends: ENet (default), yojimbo
   (dedicated secure servers), GameNetworkingSockets (internet P2P, NAT
   traversal) later. Games pick one in `game.json`; tests run against
   all of them.
2. **Serialization**: bit-packed, versioned, fuzz-tested, one function per
   message for read and write (the `serialize` pattern).
3. **Replication models**, picked per game:
   - *Authoritative server + client prediction + snapshot interpolation*
     (shooters, sandboxes, physics games; debris as seed-based events,
     see SCALING.md).
   - *Rollback* (fighting games, 2-8 players): GGPO's algorithm on a
     deterministic state (`fixed`).
   - *Lockstep* (RTS), also on `fixed`.
4. **Sessions**: many local players per connection; "host = client + server
   in one process"; any number of server/client processes on one machine
   (port allocation, LAN discovery); headless server image in
   docker-compose.
5. **Voice**: Opus (BSD) codec, jitter buffer, its own unreliable channel,
   optionally positional through the audio engine.
6. **Tests**: loopback, simulated loss/latency/jitter/reordering, several
   processes on one PC, docker-compose multi-client, fuzzing of every
   decoder, rollback determinism checks (same inputs, same checksum on
   Linux/Windows).

## Audio (2.2) — plan

Goals: FOSS, fast, as physically accurate as makes sense, driven by the
physics (materials, impacts, breaking, sliding), occlusion and muffling
by material, and accessible (blind players can tell materials apart;
deaf players see sound, tuned to taste).

- **Mixer/output**: miniaudio (public domain / MIT-0; every OS including
  Android, iOS, Web).
- **Spatialization**: HRTF (Steam Audio is Apache-2.0 since 2024 and does
  HRTF, occlusion, transmission through materials, reflections/reverb from
  scene geometry; the alternative is our own raycast approach).
- **Vercidium**: this development environment's network policy blocks
  vercidium.com, so its docs couldn't be read here (not guessed at).
  Vercidium is known for ray-traced audio (rays between sound and
  listener for occlusion, reverb and panning from geometry). Before
  relying on it: licence (must be FOSS-compatible for a marketplace
  engine) and platforms. The technique itself is well documented and can
  be built on our own collision layer (Jolt ray casts) if the SDK doesn't
  fit; Steam Audio is the FOSS reference implementation of the same idea.
- **Physics-driven sound**: impact events from FEMFX/Jolt (material pair,
  impulse, contact speed) select and shape samples; breaking plays
  per-material cracks; modal synthesis for resonant objects later.
- **Materials**: one table: density/stiffness for physics, absorption/
  transmission per frequency band for audio, footstep/impact sound sets.
- **Accessibility**: every sound carries a category and a material; a
  visualizer draws them around the screen edge by direction, intensity
  and category, with user-set colours, sizes and filters; captions;
  "audio description" cues for blind players (distinct material timbres,
  navigation pings, earcons for UI).

The audio links, checked:
- **WhoStoleMyCoffee/raytraced-audio** (MIT, a Godot/GDScript plugin):
  rays from the listener measure the room for reverb/echo, per-source
  rays muffle sounds behind walls, and "ambient" rays find openings so
  outside sound pans toward doors/windows. A clear, cheap algorithm:
  we port the idea to C++ on our collision layer (Jolt ray casts).
- **JustGoscha/ray-tracing-audio** (MIT, JavaScript): ray-traced
  reflections + binaural rendering + live visualization of the rays;
  its successor (omg-audio, Rust/WASM) adds wall transmission and
  diffraction. Reference for the accessibility visualizer and for
  transmission by material.
- **Vercidium**: blocked here (see above).

So: miniaudio for output, our own ray-traced propagation (occlusion,
transmission by material, reverb, openings) on Jolt ray casts, Steam
Audio (Apache-2.0) as the optional high-end HRTF/reflection backend.

## Hardware profiles & BENCHMARKS.md (2.4) — plan and limits

What Docker can and can't do, honestly:
- **Can**: limit CPU cores and RAM (`--cpus`, `--memory`), pin cores,
  run with no GPU (lavapipe), and pass a real GPU through: Intel Arc
  (`/dev/dri`), NVIDIA (`--gpus`, nvidia-container-toolkit), AMD
  (`/dev/dri`, `/dev/kfd`).
- **Can't**: limit VRAM, or pretend to be a different GPU. So the engine
  gets a **VRAM budget setting** (`KKE_VRAM_BUDGET_MB`) that makes it
  behave as if it had that much (VMA budgets), which is what the
  "512 MB VRAM laptop" profile uses.
- **"Android phone" / "Mac" images** can't reproduce those machines'
  performance (different CPUs, GPUs, drivers). What's honest: an ARM64
  container (QEMU) to check it *runs* on ARM, and real-device numbers
  from the users. macOS needs Apple hardware (CI runner or a Mac).

Profiles (all run the stress test and write one report):
`floor-1c-2g` (the baseline: 1 core, 2 GB, software GPU),
`laptop-1c-2g-vram512`, `dual-2c-2g-vram1g`, `mid-4c-8g`, `high-8c-16g`,
`intel-arc`, `nvidia`, `amd`, and `custom` (cores, RAM, VRAM budget,
GPU passthrough from environment variables). Results go into
**BENCHMARKS.md**: one table per module set ("core", "core + FEMFX",
later "+ Jolt", ...), machine rows from floor to top tier.
