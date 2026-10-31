# GAPS.md: what kk-engine still misses, in the order it gets closed

Written 2026-10-12 from ROADMAP.md (🟡 and 🔴 rows), BUGS.md (open rows),
ACTION_PLAN.md, the open pull requests on Forgejo and the engine part of
the Kreative Kompanion big-projects plan (E0 and E1; its E2 feature list,
demo games, docs rewrite, MCP server and assistant model come after this
list; after them, OpenGL and the editor follow [plans/backends-and-editor.md](plans/backends-and-editor.md)). Each gap is one task: a branch, a pull request, and a row here that
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
| ENG-G18 | Rendering | Particle system v2: emitters and a Lua API (#22). Part 1 done: `fx.*` in Lua (play, start, move, stop, define, wind). Part 2 done: `kke::ParticleSim` (the GPU-free half, tested), curves over life, vortex and attract, physics colliders, sub-effects (`onDeath`). Part 3 done: instanced draw (36% less CPU recording). Part 4 done: `gpu` layers moved by the GPU in closed form, written once at birth (a 28,000-drop downpour 6x faster), the CPU as fallback. | ✅ |
| ENG-G12 | Physics | Liquids: transparency, refraction and thickness; meltable solids from meshes and refreezing. Part 1 done: MLS-MPM water (Hu et al. 2018) on the GPU and the CPU (`kke::MpmWater`, `kke::MpmWaterGpu`) with two-way coupling to Jolt bodies (`kke::WaterBodyLink`: crates float, sink, get carried), clear water with thickness, refraction and foam (`FluidSurfaceRenderer::Settings::clearWater`), games/water_demo (OPTIMIZATION.md #46). Left: meltable solids from meshes and refreezing. | ⬜ |
| ENG-G10 | Physics | FEMFX <-> Jolt bridge leftovers: rubble breaking again, same-step pushes from heavy FEMFX bodies. | ⬜ |
| ENG-G13 | Network | FEMFX breakables per machine; relay and join codes (#44); world saves (#45); shared roles (#46). | ⬜ |
| ENG-G14 | Mods | Pack system in every game and a mod screen (#61). | ⬜ |
| ENG-G19 | Animation | 2D blend spaces, CCD for long chains, rest-pose matching, an animation demo (#24). Part 1 done: 2D blend spaces (`Animator::addBlendState2D`, gradient band, polar), goblin_horde heroes strafe with one. Part 2 done: CCD with bend limits and damping (`solveCcd`, `ccdPoints`). Part 3 done: rest-pose matching in retargeting (limbs onto the source's rest directions). Part 4 done: games/anim_demo shows all three. | ✅ |
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
| ENG-F01 | Rendering | Real-time global illumination. Screen-space done: a visibility bitmask, one frame late, off by default (`globalIllumination`, docs/RENDERING_PRINCIPLES.md). Not yet: light from what is off screen (probes, ENG-F15). | 🟡 |
| ENG-F02 | Rendering | Ray-traced reflections and refractions. Reflections done: `reflections = screen`, `kke` (screen first, then rays through simplified stand-ins within a budget) and `classic` (every triangle, sun shadows), inline ray queries on any vendor (`kke::RayScene`, a GPU self-test in CI). Refractions: see-through surfaces bend the scene drawn before them by their index of refraction, never smearing in what is in front of them; `kke` traces rays only where the screen can't show what's behind, `classic` for every refracting pixel. | ✅ |
| ENG-F03 | Rendering | Volumetric lighting and fog: `volumetricFog` lights the height fog in a grid over the view with the sun's shadow (light shafts) and the point lights (halos), stable without TAA. | ✅ |
| ENG-F04 | Rendering | Screen-space ambient occlusion (GTAO/SSAO). Done with ENG-F01's screen-space GI: its visibility shades the sky light and reflections. | ✅ |
| ENG-F05 | Rendering | Anti-aliasing beyond MSAA: SMAA over MSAA, on by default (`smaa`), one frame, no ghosting; TAA as an option over the clean image (`taa`: jittered frames, reprojected and clipped history). | ✅ |
| ENG-F06 | Rendering | Foveated rendering. Deferred until the engine has VR or eye tracking: on a flat screen it means variable-rate shading, which needs a shading-rate image in every scene pass and pipeline for small gains in a forward renderer at our frame times (decided 2026-10-18, F07 first). | ⬜ |
| ENG-F07 | Rendering | Ray-traced shadows: `shadowRays = kke` (rays only past the shadow map's reach, through stand-ins) or `classic` (every pixel, full geometry), ray-query GPUs. | ✅ |
| ENG-F08 | Rendering | Many lights. Instead of a deferred path (which loses MSAA and glass): clustered forward lighting, up to 1024 lights past the 16 key ones, each pixel walking only its cell's list. | ✅ |
| ENG-F09 | Rendering | Virtual texture streaming. Deferred (Kees, 2026-10-18): our games' textures fit in memory, and it needs a page table, feedback pass and streaming in every material for no gain yet. Revisit for open worlds whose textures don't fit. | ⬜ |
| ENG-F10 | Rendering | Post stack: bloom, depth of field, motion blur, LUT grading (tone map, exposure and grade exist). Bloom done (`bloom`, on by default); LUT grading done (a mood's `lut:`, .cube files, tetrahedral); depth of field done (`depthOfField`, off by default, auto focus); motion blur done (`motionBlur`, off by default, the camera's movement from the depth, no velocity buffer). | ✅ |
| ENG-F11 | Rendering | GPU particle/VFX system. GPU particles in closed form (G18), and since F11 stepped on the GPU when they attract, swirl (`turbulence`: divergence-free curl waves) or collide (against the last frame's depth, no colliders needed); 100,000 for +0.08 ms. Not yet: ribbons and trails, sorted see-through GPU particles. | 🟡 |
| ENG-F12 | Rendering | Decals: applied inside the shading of every lit surface from per-cell lists (no G-buffer, no clipped meshes), so they are lit, shadowed and reflected like the surface, keep MSAA and cost nothing where there are none; six built-in textures, PNGs from assets/decals, `decal.*` in Lua (docs/DECALS.md). | ✅ |
| ENG-F13 | Rendering | Occlusion culling: a small farthest-depth map of each frame read back by the CPU two frames later, so models hidden behind others are not skinned, uploaded or drawn, with the CPU's instancing kept (`occlusionCulling`, off by default; worth it in dense scenes). Not yet: GPU-driven indirect draws. | ✅ |
| ENG-F14 | Rendering | Upscalers such as FSR (render scale exists) | 🟡 |
| ENG-F15 | Lighting | Light probes with baking | ⬜ |
| ENG-F16 | Lighting | Contact shadows | ⬜ |
| ENG-F17 | Lighting | Light portals | ⬜ |
| ENG-F18 | Lighting | Screen-space reflections. Done with ENG-F02: `reflections = screen`, a fixed-step march along the ray's line on screen, one frame late. | ✅ |
| ENG-F19 | 2D | 2D toolkit: tilemaps, sprite animation, 2D lights, 2D physics, parallax | ⬜ |
| ENG-F20 | XR | OpenXR (VR/AR) on the Vulkan renderer | ⬜ |
| ENG-F21 | Animation | 2D blend trees (1D and 2D blend spaces; G19) | ✅ |
| ENG-F22 | Animation | IK solver (two-bone, foot, hand, look, FABRIK and CCD with bend limits; G19) | ✅ |
| ENG-F23 | Animation | Motion capture import and clean-up: BVH files load as a skeleton with the motion as its clip (`loadModel("x.bvh")`, `kke::loadBvh`; channel order, units, Z-up, line-numbered errors) for the retargeter, and `cleanAnimation` trims, fixes quaternion flips, removes spikes, smooths, resamples, takes root travel out and crossfades a loop (`kke/Mocap.h`, docs/MOVEMENT.md "Motion capture"). Not yet: foot-contact clean-up. | ✅ |
| ENG-F24 | Animation | Procedural animation (docs/PROCEDURAL_ANIMATION.md) | ✅ |
| ENG-F25 | Animation | Facial animation, blend shapes (docs/FACIAL_ANIMATION.md): glTF morph targets applied before skinning (GPU and CPU paths), `kke::FaceRig` (expressions, mouth shapes, blinking, text and voice lip-sync; ARKit, VRM, VRoid and Oculus names), Lua `face.*`. Not yet: weights animated inside a clip, VRM presets, morphs on unskinned meshes, eye look. | ✅ |
| ENG-F26 | Animation | Retargeting between skeletons (by bone name, with rest-pose matching, ENG-G19) | ✅ |
| ENG-F27 | Animation | Runtime sequencer and cutscenes from Lua or the node scripter: `kke::Timeline` (tracks of keyframes with eases for scalars, vectors, rotations and colours, events, markers, spans, animation clips, nested timelines; once, loop, ping-pong, reverse, scrub; JSON files) and Lua `timeline.*` with hooks, next to `camera.path` for the camera (`kke/Timeline.h`, docs/TIMELINE.md). Not yet: an editor, the node scripter's timeline node. | ✅ |
| ENG-F28 | Animation | State machine with events and root motion (crossfading state machine, root motion, and animation events in `Animator`, `ModelModule` and Lua `models.event` + the `AnimEvent` hook) | ✅ |
| ENG-F29 | Physics | Soft bodies: cloth and jelly (docs/CLOTH.md, docs/JIGGLE.md) | ✅ |
| ENG-F30 | Physics | Fluid simulation (PBF liquids; G12 leftovers) | 🟡 |
| ENG-F31 | Physics | Vehicle physics (docs/VEHICLES.md, racing) | ✅ |
| ENG-F32 | Physics | Character controller with advanced locomotion (docs/MOVEMENT.md) | ✅ |
| ENG-F33 | Physics | Ragdolls with full-body constraints (docs/RAGDOLLS.md) | ✅ |
| ENG-F34 | Physics | Debris with secondary collisions (G10 leftovers) | 🟡 |
| ENG-F35 | Physics | Cloth-to-cloth collision | ✅ |
| ENG-F36 | Physics | Strand hair (docs/HAIR.md) | ✅ |
| ENG-F37 | Physics | Joints and constraints API in Lua (hinge, slider, spring, motor): `RigidWorld::addJoint` (fixed, ball, hinge, slider, spring/rope) with motors, Lua `physics.joint`/`motor`/`jointValue` | ✅ |
| ENG-F38 | Audio | HRTF spatial audio: a built-in binaural mode (Woodworth's interaural delay and Brown & Duda's head shadow) and measured HRTFs through Steam Audio when built with `KKE_ENABLE_STEAM_AUDIO` (elevation, SOFA files) (docs/AUDIO.md "Binaural", "Steam Audio") | ✅ |
| ENG-F39 | Audio | Dynamic, adaptive music: segments of layered stems that follow parameters, transitions on the beat, bar, grid or segment end (optionally through a bridge), stingers, and beat and bar events, with the moment found inside the mixer to the sample (`kke/MusicDirector.h`, `AudioMixer::playScheduled`, Lua `music.*`, docs/AUDIO.md "Adaptive music"). Not yet: tempo changes inside a segment, entering a segment part way. | ✅ |
| ENG-F40 | Audio | Spatial voice chat (proximity voice exists) | ✅ |
| ENG-F41 | Audio | Occlusion and diffraction (ray occlusion exists; diffraction missing) | 🟡 |
| ENG-F42 | Audio | Mixer buses, snapshots, ducking, reverb zones. Done: buses on top of the player's volumes, gliding snapshots, ducking by what is heard, fades, pitch, and Lua sound files and crossfading music (docs/AUDIO.md). Not yet: authored reverb zones (the ray-traced room covers most). | 🟡 |
| ENG-F43 | UI | Widgets with drag and drop (RmlUi; inventory drag exists) | 🟡 |
| ENG-F44 | UI | Data binding (RmlUi data models) | ✅ |
| ENG-F45 | UI | Animation curves for UI | 🟡 |
| ENG-F46 | UI | Multi-monitor | ⬜ |
| ENG-F47 | Input | Gesture recognition beyond pinch, pan and twist | 🟡 |
| ENG-F48 | Input | Haptics API (rumble on impacts is next in INPUT) | 🟡 |
| ENG-F49 | Input | Virtual joystick for phones: on-screen sticks and buttons from the game's actions, player-editable (docs/TOUCH.md) | ✅ |
| ENG-F50 | Input | Action maps, rebinding, glyphs, per-player devices (docs/INPUT.md) | ✅ |
| ENG-F51 | Scripting | Visual scripting (node graph; #33 leftovers) | 🟡 |
| ENG-F52 | Scripting | Hot reload of scripts | ✅ |
| ENG-F53 | Gameplay | Save and load: versioned files, slots, cloud hook. Done: save slots with titles, play time, versions and Lua migrations, self-checking files, and a cloud hook that keeps the newer save (docs/SAVING.md). | ✅ |
| ENG-F54 | Gameplay | Replays of game sessions | 🟡 |
| ENG-F55 | Networking | Rollback networking for fighting games (GGPO-style): guesses the other players' inputs, rolls back and re-runs when wrong, loss-tolerant packets with validation, input delay, a prediction limit, checksum desync detection, time sync, disconnects, 1 to 8 players, a driver for games that save and load their state, and a sync test for determinism (`kke/Rollback.h`, docs/NETWORKING.md "Rollback"). Not yet: a peer-to-peer unreliable transport, spectators. | ✅ |
| ENG-F56 | Networking | Client-side prediction | ✅ |
| ENG-F57 | Networking | Dedicated servers (docs/SERVER_HOSTING.md) | ✅ |
| ENG-F58 | Networking | Matchmaking (directory exists; relay and join codes #44) | 🟡 |
| ENG-F59 | Networking | Cross-platform play | 🟡 |
| ENG-F60 | Content | Texture compression ASTC/BCn (#39): BC7 in KTX2 done (`kke_texcook`, loaded instead of the PNG, decoded where the GPU lacks BC7); ASTC not yet | 🟡 |
| ENG-F61 | Content | Texture streaming | ⬜ |
| ENG-F62 | Content | LOD generation. `kke::simplifyModel` existed (crowds); now automatic: `kke/LodChain.h` builds up to three levels per big part over its own vertices (skinned and morphed parts work), `ModelModule` draws the one that suits the on-screen size, with hysteresis, in every pass (docs/LOD.md). Not yet: cross-fades, impostors, cooked levels. | ✅ |
| ENG-F63 | Content | Animation compression: translation, rotation and scale tracks with only the keys the curve needs (a tolerance in mm and degrees) stored as 16-bit numbers (smallest-three rotations), a validated file format, and `AnimationSet::compress`: 17x smaller on the shipped animation library within 0.5 mm / 0.05 degrees (`kke/AnimCompress.h`, docs/PROCEDURAL_ANIMATION.md "Compression"). Not yet: `ModelModule`'s own playback from the packed form. | ✅ |
| ENG-F64 | Content | Level streaming: cells load and unload around foci by radius with hysteresis, a load budget, retries and timeouts, persistent, pinned and forced cells, and progress for a loading screen (`kke/WorldStream.h`, Lua `world.*`, docs/SCENES.md "Streaming a big world"). Not yet: worker-thread loading inside the engine. | ✅ |
| ENG-F65 | Platforms | Consoles (#69) | ⬜ |
| ENG-F66 | Platforms | Steamworks: achievements, cloud saves (optional module) | ⬜ |
| ENG-F67 | Tooling | Runtime CPU/GPU/memory profiler with capture files: every module call timed every frame (Performance panel table), captures of zones from all threads plus GPU time and memory to a Perfetto trace (panel button, `KKE_PROFILE`, Lua `profile.*`, `KKE_PROFILE_SCOPE`); `kke::Profiler` (docs/PROFILING.md). Not yet: GPU time per pass, allocation tracking. | ✅ |
| ENG-F68 | Tooling | Opt-in crash reporting: `kke::crash` writes a report (signal or uncaught exception, backtrace, context, last log lines) to the crash folder; sending needs the player's opt-in (`privacy.sendCrashReports`, off) and a game's uploader; docs/CRASH_REPORTS.md. Not yet: Windows, an engine-side server. | 🟡 |
| ENG-F69 | Tooling | Game test harness: fixed frame time, input tapes (record/playback), engine screenshots at chosen frames, `kke_imgdiff` compare; `check_game --frames/--record/--replay/--shots/--expect` ([TEST_HARNESS.md](TEST_HARNESS.md)) | ✅ local |
| ENG-F70 | AI | Pathfinding around dynamic obstacles. Done: tiled navmeshes with box and cylinder obstacles (DetourTileCache), agents re-path when it changes (docs/AI.md); Lua `nav.*` builds one from the level's collision and finds paths, and Lua `ai.*` creatures walk it with their clips. Not yet: off-mesh links, area costs. | 🟡 |
| ENG-F71 | AI | Behaviour trees: sequence, selector, reactive (pre-empting) variants, parallel, random, decorators (repeat, retry, cooldown, timeout, delay...) and leaves over a typed blackboard, built in C++ or from JSON with errors by path (`kke/ai/BehaviorTree.h`), and Lua `bt.*` with Lua functions as leaves (docs/AI.md "Behaviour trees"). Not yet: a node-graph editor, a utility selector node. | ✅ |
| ENG-F72 | AI | Utility AI (docs/AI.md) | ✅ |
| ENG-F73 | AI | Crowds of hundreds of agents (flocking exists) | 🟡 |
| ENG-F74 | AI | Navmesh baking (Recast) | ✅ |
| ENG-F75 | World | Terrain sculpting. Done: raise, lower, smooth, flatten and paint brushes, from Lua while the game runs (docs/TERRAIN.md). Not yet: an editor tool for them, saving sculpted terrain with the level. | 🟡 |
| ENG-F76 | World | Heightmap terrain. Done: chunks with distance LOD and skirts, a Jolt height field for collision, four colour layers with rock on slopes, from a seed or a greyscale picture, Lua `terrain.*` (docs/TERRAIN.md). Not yet: textured layers, holes. | 🟡 |
| ENG-F77 | World | Procedural terrain (ACTION_PLAN 4.1). Done: seeded gradient-noise hills or ridged mountains (docs/TERRAIN.md). Not yet: erosion, rivers, biomes. | 🟡 |
| ENG-F78 | World | World streaming for large worlds: the same cell streamer, plus `OriginShifter` (a floating origin that moves the world in whole steps so far travel keeps float precision). Not yet: streaming of the terrain's own chunks, view-direction priority. | ✅ |
| ENG-F79 | World | Foliage. Done: trees, rocks and bushes scattered on the terrain by layer, slope and height, solid trunks, kept on the ground when sculpted (Lua `terrain.scatter`, docs/TERRAIN.md). Not yet: GPU grass, wind sway. | 🟡 |
| ENG-F80 | World | Water with waves (ocean and buoyancy exist) | ✅ |
| ENG-F81 | World | Snow and weather: types blended smoothly (clear to blizzard), random weather by transition weights, what falls (snow is rain above 2 degrees), gusting wind, day and night temperature, ground wetness and snow cover that build, dry and melt, lightning with flashes and thunder delayed by distance, rain, downpour and snow particles that follow the camera, and lighting, grey and fog that respect the day/night clock and moods (`kke/Weather.h`, `kke/ScriptWeather.h`, Lua `weather.*`, docs/MOODS.md "Weather"). Not yet: wet and snowy surfaces in the lit shader (the numbers are there), moon and stars, visible clouds. | ✅ |
| ENG-F82 | World | Day/night cycle. Done: a clock that moves the sun, sky, light and exposure through dusk to moonlight, Lua `clock.*` with Dawn/Dusk/Hour hooks (docs/MOODS.md "Time of day"). Not yet: stars and moon in the sky, multiplayer clock sync. | 🟡 |
| ENG-F83 | World | Biomes | ⬜ |
| ENG-F84 | Localization | String tables, plurals, font fallback, RTL, language switch: `kke::Localization` (JSON tables, CLDR plurals for ~70 languages, placeholders, fallback chain, locale numbers), `kke::bidi` (Arabic shaping, UAX #9 reordering), fonts per language, every UI text node translated through RmlUi's `TranslateString`, Lua `loc.*`, language row in Settings, mirrored menu (docs/LOCALIZATION.md). Shipped: en, nl, de, fr, es, ar, he. Not yet: CJK/Thai/Indic fonts, IME, a string-extraction tool. | ✅ |
| ENG-F85 | Accessibility | Subtitles (`kke::Subtitles`, Lua `subtitles.*`), colour-blind modes (daltonisation matrix in the tone-map pass), UI scale, remapping (existing), high-contrast class, reduce motion, screen-reader hook with `spd-say`/`say` (docs/ACCESSIBILITY.md). Not yet: Windows speech, the platform accessibility tree (AT-SPI/UIA), focus announcements from the menu. | 🟡 |
| ENG-F86 | Rendering | Ray-traced shadows without ray-tracing hardware: a compute-shader BVH tracer as the fallback where the GPU has no ray query (Kees, 2026-10-18; ideas from ThreatInteractive's Apache-2.0 SWRT prototype: 4-wide compressed nodes, binned SAH, GPU refits, per-tile caster lists), measured against ray query on the RX 9070 XT. After the RHI/web work. | ⬜ |
| ENG-F87 | Content | Biome showcase scenes from Kees's Synty biome packs, each in its full glory with our water and ray tracing, and a comparison of every rendering mode (shadow map, KKE and classic rays, software rays, GI, reflections) with frame data and pictures: is each mode faster, does it look better (Kees, 2026-10-18). | ⬜ |
