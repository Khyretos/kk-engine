# GAPS.md: what kk-engine still misses, in the order it gets closed

Written 2026-10-12 from ROADMAP.md (🟡 and 🔴 rows), BUGS.md (open rows),
ACTION_PLAN.md, the open pull requests on Forgejo and the engine part of
the Kreative Kompanion big-projects plan (E0 and E1; its E2 feature list,
demo games, docs rewrite, MCP server and assistant model come after this
list). Each gap is one task: a branch, a pull request, and a row here that
moves to "Done" with its PR number.

How a task is done: the local Coder (Ollama `qwen3.5:9b-q8_0` on
soucouyant) drafts every code unit, up to 3 attempts fed with its own
build and test errors; Claude reviews, corrects only what is left, and
records each correction as a numbered lesson in
[docs/lessons-learned.md](lessons-learned.md) with the exact situation in
`docs/qwen-situations/<lesson>/`. Per-task metrics go in
`docs/qwen-log/<task>.jsonl` and the task's row in
[docs/qwen-log/README.md](qwen-log/README.md). A pull request merges only
when every CI check is green on its final head.

Status: ⬜ not started, 🔨 in progress, ✅ done.

## E0: align the copies

| ID | Gap | Done when | Status |
|---|---|---|---|
| ENG-A01 | Kees's checkout on soucouyant (`/media/development/Software/kk-engine`) is 17 commits behind Forgejo main. | It fast-forwards to Forgejo main; his untracked animation folders are untouched. | ✅ |
| ENG-A02 | Open PR #24 (Android build with FEMFX off) says the FEMFX-on build and an Android build were not verified. | Both builds pass with `-Werror`; the PR is merged with green CI. | ✅ PR #27 |
| ENG-A03 | The nightly lint bot's PRs #23, #25 and #26 have no CI status (bot pushes start no workflows). | Each one gets a lint run and is merged, or is closed because a newer one replaces it. | ✅ PRs #28, #29, #30 |

## E1a: open bugs (BUGS.md)

| ID | Bug | Done when | Status |
|---|---|---|---|
| ENG-B067 | rmlui_demo trader: "Fine, thirty-five" charges 40 gold, the inventory gold does not refresh after paying, the Mana Draught does nothing. | The deal charges 35, the gold label updates, the draught restores 30 mana; a check covers the price. | ✅ |
| ENG-B068 | `SM_Prop_Barrel_01` is in both the `crate` and `barrel` play blocks, so Barrel recipes never fire. | `blockOf("SM_Prop_Barrel_01")` is `barrel`; a test checks no mesh is in two blocks. | ✅ PR #27 |
| ENG-B069 | Sandbox graph editor: a change and a Done click in one frame return `Closed`, so the graph is not recompiled. | A changed graph closed in the same frame is marked dirty and recompiled; a check covers it. | ✅ |
| ENG-B072 | duel: a round won on time still shows "K.O."; two-player mode re-adds both AI agents every frame; only player 1 can skip the wait between rounds. | "Time" shows for a timeout, the agents are set once, either player skips. | ✅ |
| ENG-B074 | `KKE_UI_ROOT`, `KKE_SHOWCASE_START` and most `KKE_CLIMB_*` switches use `std::getenv`, so shipping builds keep them. | They go through `kke::dev::env`; the shipping build ignores them. | ✅ |
| ENG-B075 | Command kit: `CommandInput` has a fixed 8-slot wheel while `CommandHud` lays out any count; `Scenery` scans every pack; a tree's collider includes its canopy. | The wheel follows `setWheel`'s count, `Scenery` takes `onlyPacks`, trees collide on the trunk only. | ✅ |
| ENG-B076 | The `modules` lists in sandbox, showcase, rmlui_demo and imgui_demo's game.json leave out modules the games add; some descriptions are stale. | Every list matches the modules the game adds; `tools/ci/check_game_modules.py` compares them in CI. | ✅ |
| ENG-B087 | GCC 16 Release: third-party opus and sqlite still warn (`-Wstringop-overflow`, `-Wstringop-overread`). | A GCC 16 Release build prints no warnings, without hiding warnings in engine code. | ✅ |
| ENG-B090 | A car with a flat tyre rocks for ever: anti-roll bars were about 58 times stiffer than their N/m (found by the macOS job). | Bars are true N/m; a flat car settles; tests cover both. | ✅ |
| ENG-B091 | `VoiceCleaner.TakesOutSteadyNoise` depends on the standard library's normal distribution and fails on macOS. | The test's noise is the same on every platform and the cleaner passes it there. | ✅ |
| ENG-B092 | `DedicatedServer.BacksUpTheStoreAndKeepsTheNewest` fails when its two backups fall in different seconds. | The test passes whichever second each backup lands in. | ✅ |

## E1b: engine gaps (ROADMAP 🟡/🔴)

| ID | Area | Gap | Status |
|---|---|---|---|
| ENG-G03 | Rendering | Normal maps and per-pixel roughness/metallic textures (today one value per object). | ✅ PR #31 |
| ENG-G05 | Lighting | Cascaded shadow maps: 4 cascades per view out to 80 m, each in its own tile of the shadow map (ShadowMap::kCascades). | ✅ |
| ENG-G04 | Lighting | Point-light shadows: a point light with `castsShadows` gets six cube faces, each in its own 512-texel tile of a second shadow map, out to `shadowRange` (default 20 m); the sandbox and showcase lamps cast. | ✅ |
| ENG-G07 | Lighting | 16 lights (`Lighting::kMaxLights`); point lights can have a range (`Light::range`, smooth falloff, 0 = everywhere); the first 4 that cast shadows get a point-shadow slot; a Lua `light.*` API (`point`, `move`, `set`, `remove`, `count`). | ✅ |
| ENG-G06 | Lighting | Sky reflections: the fill light prefiltered once per sky into a 128 x 64 equirect map with 6 roughness levels (`kke::EnvMap`, GGX importance sampling), sampled per pixel along the reflection and weighted by Karis's analytic environment BRDF; flat ambient when there is no sky. | ✅ |
| ENG-G08 | Models | GPU skinning in `ModelModule`: `shaders/skin.comp` writes each visible skinned part's vertex buffer every drawn frame (paused too), jiggle zones included; the CPU builds only the bone matrices (`kke/GpuSkin.h`). `KKE_CPU_SKINNING=1` keeps the CPU path. | ✅ |
| ENG-G09 | UI | RmlUi Vulkan `filter`, `box-shadow`, `backdrop-filter` and `mask-image`: RmlUi's layer API in `RmlVulkanRenderInterface` (full-frame layer images, filters as full-screen passes in `shaders/rml_filter.frag`, `kke/RmlFilters.h`); rmlui_demo's glass panels use them. | ✅ |
| ENG-G17 | Rendering | Tone mapping as one post pass on an HDR scene target: the 3D pass draws scene light into an RGBA16F image and `shaders/tonemap_post.frag`, the first draw of the UI pass, applies the curve, exposure and look once per pixel and does the render-scale upscale (`kke/ToneMap.h` mirrors it on the CPU for clear colours and thumbnails). 10-bit output where the display offers it (A2B10G10R10; the tone-map pass and the UI encode sRGB themselves on such a UNORM swapchain). | ✅ |
| ENG-G18 | Rendering | Particle system v2: emitters and a Lua API (#22). Part 1 done: `fx.*` in Lua (play, start, move, stop, define, wind). Part 2 done: `kke::ParticleSim` (the GPU-free half, tested), curves over life, vortex and attract, physics colliders, sub-effects (`onDeath`). Next: instanced and GPU paths. | 🔨 |
| ENG-G12 | Physics | Liquids: transparency, refraction and thickness; meltable solids from meshes and refreezing. | ⬜ |
| ENG-G10 | Physics | FEMFX <-> Jolt bridge leftovers: rubble breaking again, same-step pushes from heavy FEMFX bodies. | ⬜ |
| ENG-G13 | Network | FEMFX breakables per machine; relay and join codes (#44); world saves (#45); shared roles (#46). | ⬜ |
| ENG-G14 | Mods | Pack system in every game and a mod screen (#61). | ⬜ |
| ENG-G19 | Animation | 2D blend spaces, CCD for long chains, rest-pose matching, an animation demo (#24). | ⬜ |
| ENG-G11 | Physics | FEMFX on ARM/WebAssembly through a SIMDe port. | ⬜ |
| ENG-G15 | Platforms | macOS bundle and `.icns`, Windows `.exe` icon build, phone player builds (#56), code signing (#57). | ⬜ |
| ENG-G16 | Content | Synty packs beyond POLYGON Prototype in the demos; asset browser leftovers. | ⬜ |

ENG-G01 (bugs) is E1a above. ENG-G02 (textures from files) is done: ROADMAP
"Real image/texture loading from files" is 🟢.

## E2: feature parity with other engines (85 features)

The list from the big-projects plan, compared with Godot, Unity, Unreal,
Bevy and O3DE (editor UI left out: the engine is used from the IDE, in
game or through the node scripter). The status column is a first pass
from ROADMAP.md and docs/; each task re-checks it before starting. ✅
exists, 🟡 partial, ⬜ missing. Order: rendering and lighting first, then
by area. The goal is parity with the big engines and more.

| ID | Area | Feature | Status |
|---|---|---|---|
| ENG-F01 | Rendering | Real-time global illumination | ⬜ |
| ENG-F02 | Rendering | Ray-traced reflections and refractions | ⬜ |
| ENG-F03 | Rendering | Volumetric lighting and fog (height fog only today) | 🟡 |
| ENG-F04 | Rendering | Screen-space ambient occlusion (GTAO/SSAO) | ⬜ |
| ENG-F05 | Rendering | Temporal anti-aliasing (MSAA only today) | ⬜ |
| ENG-F06 | Rendering | Foveated rendering | ⬜ |
| ENG-F07 | Rendering | Ray-traced shadows | ⬜ |
| ENG-F08 | Rendering | Deferred rendering path | ⬜ |
| ENG-F09 | Rendering | Virtual texture streaming | ⬜ |
| ENG-F10 | Rendering | Post stack: bloom, depth of field, motion blur, LUT grading (tone map, exposure and grade exist) | 🟡 |
| ENG-F11 | Rendering | GPU particle/VFX system (one compute fountain today; G18) | 🟡 |
| ENG-F12 | Rendering | Decals | ⬜ |
| ENG-F13 | Rendering | Occlusion culling at scale, GPU-driven, HZB (frustum culling exists) | 🟡 |
| ENG-F14 | Rendering | Upscalers such as FSR (render scale exists) | 🟡 |
| ENG-F15 | Lighting | Light probes with baking | ⬜ |
| ENG-F16 | Lighting | Contact shadows | ⬜ |
| ENG-F17 | Lighting | Light portals | ⬜ |
| ENG-F18 | Lighting | Screen-space reflections | ⬜ |
| ENG-F19 | 2D | 2D toolkit: tilemaps, sprite animation, 2D lights, 2D physics, parallax | ⬜ |
| ENG-F20 | XR | OpenXR (VR/AR) on the Vulkan renderer | ⬜ |
| ENG-F21 | Animation | 2D blend trees (1D blend spaces exist; G19) | 🟡 |
| ENG-F22 | Animation | IK solver (two-bone, foot, hand and look IK exist; CCD missing) | 🟡 |
| ENG-F23 | Animation | Motion capture import and clean-up | ⬜ |
| ENG-F24 | Animation | Procedural animation (docs/PROCEDURAL_ANIMATION.md) | ✅ |
| ENG-F25 | Animation | Facial animation, blend shapes | ⬜ |
| ENG-F26 | Animation | Retargeting between skeletons (by bone name; rest-pose matching missing) | 🟡 |
| ENG-F27 | Animation | Runtime sequencer and cutscenes from Lua or the node scripter | ⬜ |
| ENG-F28 | Animation | State machine with events and root motion (crossfading state machine and root motion exist; events missing) | 🟡 |
| ENG-F29 | Physics | Soft bodies: cloth and jelly (docs/CLOTH.md, docs/JIGGLE.md) | ✅ |
| ENG-F30 | Physics | Fluid simulation (PBF liquids; G12 leftovers) | 🟡 |
| ENG-F31 | Physics | Vehicle physics (docs/VEHICLES.md, racing) | ✅ |
| ENG-F32 | Physics | Character controller with advanced locomotion (docs/MOVEMENT.md) | ✅ |
| ENG-F33 | Physics | Ragdolls with full-body constraints (docs/RAGDOLLS.md) | ✅ |
| ENG-F34 | Physics | Debris with secondary collisions (G10 leftovers) | 🟡 |
| ENG-F35 | Physics | Cloth-to-cloth collision | ✅ |
| ENG-F36 | Physics | Strand hair (docs/HAIR.md) | ✅ |
| ENG-F37 | Physics | Joints and constraints API in Lua (hinge, slider, spring, motor) | ⬜ |
| ENG-F38 | Audio | HRTF spatial audio | ⬜ |
| ENG-F39 | Audio | Dynamic, adaptive music | ⬜ |
| ENG-F40 | Audio | Spatial voice chat (proximity voice exists) | ✅ |
| ENG-F41 | Audio | Occlusion and diffraction (ray occlusion exists; diffraction missing) | 🟡 |
| ENG-F42 | Audio | Mixer buses, snapshots, ducking, reverb zones | 🟡 |
| ENG-F43 | UI | Widgets with drag and drop (RmlUi; inventory drag exists) | 🟡 |
| ENG-F44 | UI | Data binding (RmlUi data models) | ✅ |
| ENG-F45 | UI | Animation curves for UI | 🟡 |
| ENG-F46 | UI | Multi-monitor | ⬜ |
| ENG-F47 | Input | Gesture recognition beyond pinch, pan and twist | 🟡 |
| ENG-F48 | Input | Haptics API (rumble on impacts is next in INPUT) | 🟡 |
| ENG-F49 | Input | Virtual joystick for phones | 🟡 |
| ENG-F50 | Input | Action maps, rebinding, glyphs, per-player devices (docs/INPUT.md) | ✅ |
| ENG-F51 | Scripting | Visual scripting (node graph; #33 leftovers) | 🟡 |
| ENG-F52 | Scripting | Hot reload of scripts | ✅ |
| ENG-F53 | Gameplay | Save and load: versioned files, slots, cloud hook | 🟡 |
| ENG-F54 | Gameplay | Replays of game sessions | 🟡 |
| ENG-F55 | Networking | Rollback networking for fighting games (GGPO-style) | ⬜ |
| ENG-F56 | Networking | Client-side prediction | ✅ |
| ENG-F57 | Networking | Dedicated servers (docs/SERVER_HOSTING.md) | ✅ |
| ENG-F58 | Networking | Matchmaking (directory exists; relay and join codes #44) | 🟡 |
| ENG-F59 | Networking | Cross-platform play | 🟡 |
| ENG-F60 | Content | Texture compression ASTC/BCn (#39) | ⬜ |
| ENG-F61 | Content | Texture streaming | ⬜ |
| ENG-F62 | Content | LOD generation | ⬜ |
| ENG-F63 | Content | Animation compression | ⬜ |
| ENG-F64 | Content | Level streaming | ⬜ |
| ENG-F65 | Platforms | Consoles (#69) | ⬜ |
| ENG-F66 | Platforms | Steamworks: achievements, cloud saves (optional module) | ⬜ |
| ENG-F67 | Tooling | Runtime CPU/GPU/memory profiler with capture files (frame stats exist) | 🟡 |
| ENG-F68 | Tooling | Opt-in crash reporting | ⬜ |
| ENG-F69 | Tooling | Game test harness: input playback, screenshot compare (virtual input and check_game exist) | 🟡 |
| ENG-F70 | AI | Pathfinding around dynamic obstacles (tiled/dynamic navmesh missing) | 🟡 |
| ENG-F71 | AI | Behaviour trees | ⬜ |
| ENG-F72 | AI | Utility AI (docs/AI.md) | ✅ |
| ENG-F73 | AI | Crowds of hundreds of agents (flocking exists) | 🟡 |
| ENG-F74 | AI | Navmesh baking (Recast) | ✅ |
| ENG-F75 | World | Terrain sculpting | ⬜ |
| ENG-F76 | World | Heightmap terrain | ⬜ |
| ENG-F77 | World | Procedural terrain (ACTION_PLAN 4.1) | ⬜ |
| ENG-F78 | World | World streaming (ACTION_PLAN 3.6) | ⬜ |
| ENG-F79 | World | Foliage | ⬜ |
| ENG-F80 | World | Water with waves (ocean and buoyancy exist) | ✅ |
| ENG-F81 | World | Snow and weather | ⬜ |
| ENG-F82 | World | Day/night cycle (moods exist, no clock) | 🟡 |
| ENG-F83 | World | Biomes | ⬜ |
| ENG-F84 | Localization | String tables, plurals, font fallback, RTL, language switch | ⬜ |
| ENG-F85 | Accessibility | Subtitles, colour-blind modes, UI scale, remapping, screen-reader hooks (captions and remapping exist) | 🟡 |
