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
| Test suite | 🟢 | ~800 GoogleTest tests in 78 files covering the pure-logic systems (AI, locomotion, climbing, net, orders, packs, audio, data files, ...) and the cookbook recipes. GPU/Vulkan code is verified by build-and-run: every demo runs headless in CI under lavapipe with virtual controllers. Line coverage is reported on every CI run but not enforced during the POC (target 85%). |
| Icon / branding | 🟡 | Kreative Kompas logo is the window icon (SDL3, embedded) and the Windows `.exe` icon (`.rc`, not yet built on Windows); every game opens with the animated 3D logo intro (`kke::LogoIntro`, skippable, `KKE_SKIP_INTRO=1`). Missing: macOS `.icns` + bundle. |
| Shader path resolution | 🟢 | Games switch to the executable's folder when the working directory has no `shaders/` (`enterRuntimeDirectory`, docs/RELEASES.md), so packaged builds start from anywhere. |
| Platform layer + hardware targets (`kke/Platform.h`, `kke/HardwareTarget.h`) | 🟡 | One interface for the OS services SDL3 doesn't cover; targets desktop, desktop-low, steam-deck, handheld-pc, android, ios pick per-device default settings (the player's own settings always win), custom `targets.yml`, a CMake preset per target (docs/PLATFORMS.md). **Not yet:** render backends beside Vulkan (#68), console basics (#69), Steam Deck Verified (#70). |
| Resource governor (`kke/ResourceGovernor.h`) | 🟢 | Turns the Performance settings and the machine into one budget: worker threads (affinity-aware), fps cap, background fps, render scale; `KKE_USE_EVERYTHING`. |
| Data files: JSON or YAML (`kke/DataFile.h`) | 🟢 | Every file the engine reads accepts JSON or YAML (settings, input, packs, scenes, server, licences, rules, moods...); newest wins on conflict with a warning; a YAML file stays YAML when saved; alias-bomb and depth limits (docs/DATA_FILES.md). Network messages stay JSON. |

## Rendering (3D pipeline)

| System | Status | Notes |
|---|---|---|
| Vulkan core (device, swapchain, render passes) | 🟢 | |
| Vulkan validation layers in dev builds | 🟢 | Every demo swept with validation active every session; this is how several real GPU-resource-lifetime bugs (see `BUGS.md` BUG-002) were actually found, not guessed. |
| Mesh rendering, cube/box geometry | 🟢 | `kke::Mesh::createCube()`, and the more general `PhysicsModule::buildGridBox()` for arbitrary box shapes (see Physics section). |
| Vertex format | 🟢 | `position/color/normal/uv` — real UV mapping, flat-shading-correct (see `BUGS.md` BUG-006 for the smooth-shading bug this replaced). |
| PBR materials (Cook-Torrance) | 🟢 | Real metallic/roughness, per-object, verified via real pixel-value comparisons (not just visual impression) after finding and fixing a methodology bug in the first verification attempt. |
| Material albedo textures | 🟢 | The demo cubes' six procedural textures (wood, stone, iron, rubber, glass, lava), UV-mapped for `CubeModule` and `PhysicsModule` objects. Models use their packs' own texture files (next row). |
| Real image/texture loading from files | 🟢 | `kke::Texture` loads any stb_image file with CPU-built mips and anisotropic filtering (one engine-wide cache); models get their pack textures, skies load `.hdr`, RmlUi `<img>`/`background-image` (BUG-004). **Not yet:** block compression (#39). |
| Particle system | 🟡 | One GPU compute fountain (`ParticleModule`, the compute-to-graphics reference, 20,000 particles), used only by `kke_basics`: no emitters, no Lua. Liquids, debris and sparks use `kke::SphereImpostorRenderer` (thousands of lit balls in one draw). Particle system v2 is #22. |
| Orbit camera | 🟢 | Doesn't use locked/relative cursor mode — dragging past the window edge stalls rather than wrapping (documented limitation, not a bug). |
| Reference grid | 🟢 | |
| Moods and skies (`kke::Mood`, `kke::Sky`, `kke::SkyRenderer`) | 🟡 | One data file sets sky, sun, SH ambient, height fog, colour grade, exposure and an ambience loop; 14 built-in moods, Poly Haven CC0 HDR skies fetched at build time with checksums (gradient sky offline). `app.setMood`, Lua `mood.set`, `KKE_MOOD` (docs/MOODS.md). **Not yet:** sky reflections, moving clouds, physical atmosphere, volumetric fog (#21). |
| Tone mapping and exposure | 🟡 | AgX by default, ACES or Reinhard selectable (`Lighting::toneMapper`, `KKE_TONEMAP`), one `shaders/tonemap.glsl` in every lit shader, `Lighting::exposure`; no dithering anywhere. **Not yet** (#35): HDR scene target with a single post pass, 10-bit output; the curve still runs per surface before blending. |
| Anti-aliasing and texture filtering | 🟢 | MSAA as a setting (4x default, 1x on software rasterisers), specular AA, coverage-preserving alpha mips, 8x anisotropic. **Not yet:** SMAA (#36); not measured on a real GPU. |
| Split screen and picture-in-picture (`kke/Viewports.h`) | 🟡 | 1-4 views (side by side, stacked, quarters) plus picture-in-picture, every module renders per view; used by `kke_demo` (`KKE_SPLIT`) and Climb Race (docs/INPUT.md "Split screen"). **Not yet:** more than 4 views; the screen-space liquid surface draws only without split screen. |
| Gameplay cameras (`kke::CameraRig`) | 🟢 | First person, third-person spring arm (shortens on walls, grows back smoothly), orbit, Catmull-Rom cinematic track; the cookbook game has eight cameras and screen shake. |

## Lighting & shadows

| System | Status | Notes |
|---|---|---|
| Multi-light system (up to 4 lights) | 🟢 | Real, verified visually — see docs/HISTORY.md "Lighting" for the full account. |
| Shadow mapping | 🟢 | Single directional light, single shadow-casting pass (BUG-010 was a regression here). |
| Soft shadows (PCF) | 🟢 | 9 hardware depth-compared bilinear taps, slope-scaled bias, a texel-snapped region that follows the camera. |
| Shadow casting scope | 🟡 | Every model placed through `ModelModule` (light-frustum culled, instanced), FEMFX objects, script-spawned bodies and most game modules cast. A module drawing its own geometry still has to implement `renderShadow()`. |
| Point-light shadows | 🔴 | Not started — only the single directional light casts. |
| Cascaded / multiple shadow maps | 🔴 | One shadow map whose region follows the camera; cascades planned (#20). |
| Image-based ambient lighting | 🟡 | Sky ambient as order-2 spherical harmonics from the mood's image or gradient sky, evaluated per pixel in every lit shader (docs/MOODS.md). **Not yet:** reflections (prefiltered environment map), local probes / cheap GI (#20, #21). |
| Normal maps | 🔴 | Not started — roughness/metallic are still one value per object, not per-pixel textures either. |
| Dynamic light add/remove at runtime | 🟡 | The 4 light slots can be enabled, moved and recoloured any frame from C++ (moods do this). **Not yet:** more than 4 lights, a Lua `light.*` API. |

## Physics (AMD FEMFX integration)

| System | Status | Notes |
|---|---|---|
| Tetrahedral deformable-body simulation | 🟢 | Real FEMFX integration, not a stub — see docs/HISTORY.md "Physics: AMD FEMFX integration." |
| Real fracture | 🟢 | Verified with real, measured piece counts across many real drops — see `BUGS.md` BUG-007 for the shape-resolution fix that made this visually convincing. Fractured pieces now all actually render (they mostly didn't — BUG-026). |
| Natural fracture patterns (Voronoi, KKE-driven breaking) | 🟢 | `kke::VoronoiFracture` (random-angle cracks, same tet count) + `kke::BreakGraph` + `PhysicsModule::Breakable` (plain FEMFX bodies swapped at break time, not FEMFX's own fracture: BUG-046). Seeds: world x object (`kke::fractureSeed`). Damage scales with the hit (break window + grace, BUG-051). Physics demo "Break test" scene. Thresholds measured with `tools/physics_lab`. |
| Rigid bodies + world collision + character controller (Jolt) | 🟡 | `kke::RigidWorld` / `RigidBodyModule` on Jolt 5.6 (MIT): static/kinematic/dynamic bodies (box, sphere, capsule, convex hull, triangle mesh), ray casts, contact events with material ids, `CharacterVirtual` controller (walk, slopes, stairs, jump, push). 1,000 falling boxes: 1.56 ms/step avg on one thread (FEMFX: ~0.2 ms *per body*). FEMFX <-> Jolt collision through `kke/PhysicsBridge.h` (docs/PHYSICS_BRIDGE.md). **Not yet:** rubble can't break again, heavy FEMFX bodies push Jolt one step late, FEMFX pieces pass through rubble, Jolt-native breakables. |
| FEMFX on ARM / WebAssembly | 🔴 | FEMFX's vector math is x86 AVX intrinsics: no Android, Apple Silicon or browser build until a SIMDe port (docs/SCALING.md D). |
| Real plasticity | 🟢 | Verified via real vertex-distance measurement (not FEMFX's own rest-position API, which doesn't track this — see `BUGS.md` BUG-003). |
| Material toughness tuning | 🟢 | Real, measured stress ranges per material, not a guessed formula — see `BUGS.md` BUG-020 for the full account, including the wrong approach that preceded it. |
| Scene-scale robustness (many objects) | 🟢 | Verified up to ~57 simultaneous objects without falling through the ground — see `BUGS.md` BUG-005. Scene capacities are now sized for fracture pieces too (4096 pieces), and any FEMFX limit that is hit gets logged — see BUG-027. |
| General box-shape generator | 🟢 | `PhysicsModule::buildGridBox(cellsX, cellsY, cellsZ, sizeX, sizeY, sizeZ)` — arbitrary per-axis cell counts and physical dimensions, used by every scene below. |
| Purpose-built physics scenes | 🟢 | Five real scenes, each independently verified: **Glass Sheet** (shatters into ~13-58 real pieces depending on threshold tuning at time of test), **Brick** (real 2:1:1 proportions, breaks into chunks), **Rubber Ball** (honest approximation — a box, not a true tetrahedralized sphere, see its own in-code comment; bounces without fracturing), **Car Crash** (two real objects: a plastic-deforming "car" and a fracturable "wall"), **Lava Melt** (honest approximation — real plasticity under sustained real weight, not true phase-change physics; FEMFX has none). |
| Real sphere / curved-shape tetrahedralization | 🟢 | `buildSphere` (spherified cube) and `kke::voxelizeToTets` + `fitSurfaceToMesh` for any mesh. |
| Performance at scale | 🟡 | Much better, measured, not yet checked on real hardware. Sleeping works (settled piles cost ~0.2 ms/step), FEMFX always optimized, only exterior faces drawn, catch-up capped at 2 ticks/frame. Min-spec emulation (1 core): 0.6 → 11.0 FPS average over the scripted benchmark, with 6x more fracture pieces than before (the old build was silently capping fracture). Remaining gap: while a big break is still flying, cost scales with awake piece count (~55 ms/step for ~475 pieces on 1 core) — no debris budget yet. See `docs/PERFORMANCE_NOTES.md` "Status" and `docs/HARDWARE_TESTS.md` HW-001..HW-004. |
| Jiggle physics (bones, skin, soft bodies) | 🟢 | `kke/JigglePhysics.h`, core (no FEMFX needed): `JiggleRig` (verlet bone chains on any rig), `addJiggleBone` / `inflateSkin` / `addHumanoidSoftTissue` (soft tissue and curves for rigs that have none), `JiggleSkin` (boneless skin zones via `ModelModule::setSkinJiggle`), `JellyBody` (lattice shape matching with two-way ball contact). 14 unit tests; `games/jiggle_demo`. See docs/JIGGLE.md. **Not yet:** jiggle rigs spread over worker threads and a GPU skinning path for crowds (one character: ~36 µs; jelly: ~0.4 ms per frame on lavapipe, not worth it yet), jelly vs Jolt/FEMFX bodies. |
| Cloth (capes, flags, blankets, nets) | 🟡 | `kke/Cloth.h`, `RigidWorld::addCloth`: Jolt soft bodies (XPBD) plus the engine's clipping protection (Full by default: cloth-vs-cloth and self collision, vertex-triangle and edge-edge, continuous, repeated until nothing is crossed, run again after each step; Basic; Off), air drag and wind, cloth friction, skinned capes with back-stops, character colliders only cloth sees, 11 fabric presets with a fabric shader (weave, sheen, thread gloss, fuzz). 10 unit tests incl. an exact crossing count; `games/cloth_demo`; `kke_bench` cloth cases. See docs/CLOTH.md. **Not yet:** layers squeezed between a hard edge and each other while sliding (the demo's bed) can cross for a moment after a hard landing (rigid and cloth collision are solved in turns, not together); no Lua API yet.
| Hair | 🟡 | `kke/Hair.h`, `RigidWorld::addHair`, `kke::HairRenderer`: guide strands on Jolt soft bodies (stretch and bend distance constraints; Cosserat rods and Jolt's in-development GPU hair were tried and rejected, see docs/HAIR.md), roots skinned to the head, tethers, `hold` for styles, wind; split into 64-guide bodies so heads step in parallel; 7 style presets; drawn hairs built on the GPU around the guides (Catmull-Rom, camera-facing ribbons at least a pixel wide) with Kajiya-Kay plus two shifted Marschner highlights, and shadows. 5 unit tests; `kke_bench` hair cases; the Hair scene in `games/cloth_demo`. **Not yet:** strand-strand and hair-cloth collision; GPU cost measured on real hardware; no Lua API. |
| Free climbing + generated rock faces | 🟢 | `kke/ClimbWall.h` (seeded mountain face: lean bands, relief, ledges, jug/crimp/sloper/edge holds on an always-climbable line, render mesh + Jolt collision) and `kke/Climber.h` (`Climber`: pick each hold with bumpers / triggers for precise, lunge and quick moves, hanging body, feet on holds, stamina, loose holds, mantling; `ClimbBot`). 26 unit tests (`test_climb_wall`, `test_climb_mountains`: every shipped mountain is climbable); `games/climb_race` (race a rival, split screen, IK limbs, rockfall). See docs/MOVEMENT.md. **Not yet:** Lua and node blocks for climbing, climbing on arbitrary scene meshes (only generated faces), climbing animation clips beyond IK. |
| Melee combat + crowds | 🟢 | `kke/Combat.h` (attack timing, stamina, blocks, parries, poise, knockdowns, sweeps; `CombatWorld` with a grid for hordes), `kke/Sidekick.h` (Synty SIDEKICK modular characters), `kke/MeshLod.h` (meshoptimizer crowd LODs), skinning over worker threads. `games/duel` (1v1, bot on the AI core, 2 players) and `games/goblin_horde` (waves, crowd AI, ragdolls). docs/COMBAT.md. **Not yet:** GPU skinning, weapons as items, Lua and node blocks for combat, network play. |
| Debris budget | 🟢 | `PhysicsModule::setDebrisBudget` (default 200 broken pieces, oldest sleeping piece removed first), slider in the Physics panel. |
| Network authority / reconciliation | 🟡 | Host is authoritative for Jolt bodies; clients simulate their copies and steer them to the host's (docs/NETWORKING.md). FEMFX breakables are still per machine. |
| FEMFX <-> Jolt bridge (`kke/PhysicsBridge.h`) | 🟡 | Jolt bodies near moving FEMFX pieces become kinematic proxies with impulses handed back; small pieces become Jolt convex-hull rubble (at most 6 per step, budget 800) (docs/PHYSICS_BRIDGE.md, #7, #31). **Not yet:** rubble breaking again, same-step pushes from heavy FEMFX bodies, FEMFX pieces colliding with rubble. |

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
| Player settings | 🟢 | `kke::EngineSettings` (JSON or YAML, unit-tested) + `kke::SettingsModule` (applies fullscreen, VSync, FPS cap, FOV, shadows, brightness, UI scale, dev overlay, sensitivity, physics steps, MSAA, render scale, worker threads and background fps through `ResourceGovernor`, with per-device defaults from the hardware target). Master / music / effects volumes and mute-when-unfocused reach `AudioModule`'s mixer (they were stored but never applied until 2026-09-27). |
| ImGui integration | 🟢 | Full canonical demo confirmed working (buttons, sliders, color pickers, drag/drop, tables, trees, tabs, plotting, text editing). |
| ImGui + RmlUi coexistence | 🟢 | Both receive every SDL event; no conditional capture-blocking exists between them (confirmed directly while investigating BUG-012, ruled out as a cause). |
| Font fallback chain | 🟢 | Noto Sans + Noto Color Emoji, documented in docs/HISTORY.md. |
| Marketplace UI (game browsing) | 🟢 | Real `MarketplaceIndex` backing it (see Content pipeline), not just static demo content. |
| Start menu and local players (`kke::LobbyModule`, `kke/Lobby.h`) | 🟢 | Press A to join (up to 4 seats), per-player name and look, 0-5 CPU players with difficulty, hot-plug "press A to join" toast, choices saved per game; Climb Race uses it (docs/LOBBY.md). **Not yet:** used by the other multiplayer demos (kke_demo, duel). |

## Input

| System | Status | Notes |
|---|---|---|
| Input actions and devices (`kke::InputMap`, `kke::InputDevices`, `InputModule`) | 🟢 | Bind anything to anything: chords, double tap, hold, analog shaping, contexts; a map per player, twin devices (HOSAS flight sticks), rebinding capture, left-handed preset, controller menu navigation; `input.json`/`.yml`; virtual devices for CI (`KKE_VIRTUAL_INPUT`) (docs/INPUT.md). **Not yet:** controller support in `sea_demo`, `melt_demo`, `jiggle_demo`, `physics_demo`, `audio_demo` and `synty_demo` (they read keys and mouse directly). |
| Button prompts (`kke::ButtonPrompts`) | 🟢 | Xelu CC0 glyphs that follow the device used last (keyboard, Xbox, PlayStation, Switch, Steam Deck, touch) and the player's rebinding; `<prompt>` RML element, Lua `input.prompt*`. |
| Touch gestures (`kke::TouchGestures`) | 🟡 | One finger is the mouse; two-finger drag, pinch and twist drive `OrbitCameraModule`. Needs a real touchscreen check (HARDWARE_TESTS.md HW-016); no phone build yet (#56). |

## Animation, characters and AI

| System | Status | Notes |
|---|---|---|
| Clip animation (`kke::Animator`, `kke::AnimRig`) | 🟡 | Clip sampling, 1D blend spaces, crossfading state machine, two-bone IK, biped foot placement, retargeting (docs/MOVEMENT.md). **Not yet:** 2D blend spaces, CCD for long chains, A-pose/T-pose rest matching, an animation demo (#24). |
| Character movement (`kke::Locomotion`) | 🟢 | Walk, run, jump, vault, climb, ledge hang and shimmy, ledge leaps, wall run; replayable from inputs for the network (`LocomotionReplay`) (docs/MOVEMENT.md). |
| Procedural animation (`kke/ProceduralAnim.h`) | 🟢 | Gaits for any number of legs (walk/trot/gallop by speed), leg placement, look-at, FABRIK, masked and additive pose blending, active ragdolls on Jolt motors that stagger and get up; play blocks Shove / Look at; `games/procedural_demo` (docs/PROCEDURAL_ANIMATION.md). FEMFX-only builds fall back to a plain knock-over. |
| AI core (`kke::ai`) | 🟡 | Perception, needs, utility decisions, steering and flocking, Recast/Detour navmesh, teaching by example, species in JSON/YAML, Lua `ai.*`; deterministic per seed. Used by farm_demo, pet_companion, platoon, duel and goblin_horde (docs/AI.md). **Not yet:** learning from rewards, tiled/dynamic navmesh, off-mesh links, area costs, DetourCrowd for hundreds. |
| Orders and squads (`kke/Orders.h`, `OrderBridge.h`, `OrderScript.h`) | 🟢 | Selection, click/tap/wheel meanings, formations, standing orders carried out by the AI core; picture palette, nodes and Lua `order.*`; pet_companion and platoon (docs/COMMANDS.md). |

## Play-to-make (docs/PLAY_TO_MAKE.md)

| System | Status | Notes |
|---|---|---|
| Simple: play blocks (`kke/PlayBlocks.h`, `kke/PlayScript.h`) | 🟡 | Picture palette in the sandbox's Play mode: drag out people and props, a bat that ragdolls, finger and gamepad play; Lua `play.*` with Hit/Clicked/Placed/FellOver/StoodUp events. **Not yet** (#32): more blocks, a "play my level" button, a palette from data. |
| Intermediate: node graph (`kke/NodeGraph.h`, sandbox `GraphEditor`) | 🟡 | Nodes generated from the bound Lua API and compiled to Lua ("Show Lua", errors shown on the node), per-thing and per-level graphs saved with the level, RmlUi editor in Drawflow's look, mouse/touch/gamepad. **Not yet** (#33): more blocks, grouping blocks into your own block, a real touchscreen check. |
| Advanced: Lua | 🟡 | The same blocks from plain Lua (`play.*`). **Not yet** (#34): loading a hand-written script onto one placed thing. |

## Content pipeline

| System | Status | Notes |
|---|---|---|
| Game manifest format (`game.json`) | 🟢 | Real parser, real unit-tested (`GameManifest`). |
| Marketplace index (multi-game discovery) | 🟢 | Real, unit-tested (`MarketplaceIndex`). |
| Mesh -> physics volume (`kke::voxelizeToTets`) | 🟢 | Runtime, any mesh (open ones too), fitted to the surface; the offline CGAL tool was removed (GPL, superseded) — see docs/HISTORY.md "Content pipeline". |
| FBX/OBJ model import (`kke::loadModel`) | 🟢 | ufbx-based: meshes, materials, texture resolution, skeletons, skin weights, sampled clips. Unit-tested (incl. real Synty character when installed). |
| Model rendering (`kke::ModelModule`) | 🟡 | Instances, PBR lit/textured, shadows, CPU skinning, clip playback, bone posing, bone overlay. Mip maps, texture variants per instance, world-space overlay (Synty Prototype grid). Frustum culling, instanced draws, front-to-back order, skinning over worker threads. **Not yet:** GPU skinning, normal maps, LOD picked by screen size (#23). |
| Synty packs | 🟢 | POLYGON Prototype, Town, Nature, Farm, City and Fantasy characters, Dogs and SIDEKICK used by real scenes and games (docs/SCENES.md), found by name through `AssetCatalog`. Loaded from the git-ignored `assets/synty/` or `KKE_ASSETS_DIR` — see `assets/README.md`; never committed. |
| Ragdolls | 🟢 | Physics-agnostic `kke::RagdollDesc` + `buildHumanoidRagdoll()` behind `IRagdollPhysics`. On Jolt (`RigidBodyModule`, preferred by `bestRagdollPhysics()`): swing-twist cone and twist limits, hinged knees and elbows with bend ranges, limbs that collide with each other; FEMFX (`PhysicsModule`) remains a fallback. `blendPoses()` blends back to animation (the Synty demo stands up where the body landed). Four-legged preset and per-joint overrides (docs/RAGDOLLS.md); active ragdolls with motors in docs/PROCEDURAL_ANIMATION.md. |
| Asset browser / sandbox editor | 🟡 | `games/sandbox`: `kke::AssetCatalog` finds every pack on disk (any layout), filter by pack/category/search, thumbnail grid (`kke::ThumbnailModule`: offscreen render to an ImGui atlas, background loading, disk cache per pack), ghost placement with grid snap + rotate + stacking, select/move/duplicate/delete, multi-select, move/rotate/scale gizmo, undo/redo, levels saved as kke.scene (spawn, lights, per-object Jolt collision) that kke_demo loads and walks. Opens in **Play mode** (Simple level of docs/PLAY_TO_MAKE.md): big-picture palette, drag people/props into the world, grab to move, a bat that ragdolls whoever it hits (`kke/PlayBlocks.h`; Jolt ragdolls in the default build, landing on the floor and on placed pieces' boxes), playable with a finger (two-finger turn/pinch via `kke::TouchGestures` in `OrbitCameraModule`) or a gamepad (ring cursor, A drags, LB/RB along the palette), Build/F2 for the editor (its panels are RmlUi too, and work with the pad's pointer and buttons). Built on engine pieces: `kke::DebugDrawModule`, `kke/Picking.h`, `kke::SceneFile`, `OrbitCameraModule::Controls::Editor`. **Not yet:** iOS/Android builds (#56), FEMFX collision with placed statics inside the sandbox. |
| Destructible Synty props | 🟢 | Any prop: voxelized + surface-fitted tets, material fracture patterns (splinters / Voronoi chunks / radial glass / shards / metal dents), prop's own mesh embedded and drawn deforming/breaking, settle-then-arm thresholds, runaway guard. Debris budget and rubble handoff to Jolt; break sounds are synthesized (Audio row). **Not yet:** collision for static placed meshes, impact-point-aware radial glass, particles on break. |
| Liquids (`kke::ParticleFluid`) | 🟡 | PBF particle liquid with temperature, per-material viscosity/solidification, SDF colliders, budget; `games/melt_demo`. Smooth surface via `kke::FluidSurfaceRenderer` (screen-space fluid). **Not yet:** transparency/refraction and thickness, half-res option for min-spec, GPU compute simulation path, two-way coupling with FEMFX bodies, water body / buoyancy (next: sea demo). |
| Meltable solids (`kke::MeltVolume`) | 🟡 | Voxel density+temperature, latent-heat melting into liquid particles, marching-tetrahedra surface, chamfer SDF. **Not yet:** arbitrary shapes from meshes (voxelizer exists), refreezing into solid, burning/charring. |
| Ocean + buoyancy (`kke::OceanWaves`, `kke::FloatingBodies`, `kke::OceanRenderer`) | 🟡 | Gerstner swell from wind (CPU = GPU), point-sampled Archimedes buoyancy with drag, heave damping, ballast; `games/sea_demo`. **Not yet:** refraction/underwater view, Synty props and FEMFX bodies as floaters, wakes/interactive ripples, shore/depth colour. |
| Lua scripting | 🟢 | `kke::ScriptVM` + `kke::ScriptModule` (SCRIPTING.md): GMod-style hooks/timers, hot reload, per-script globals, runaway scripts hard-stopped (coroutine yield past `pcall`), memory cap, per-script CPU time. Bindings: physics, models/animation, FEMFX breakables, RmlUi documents, scenes, input, audio, camera, net (`sv_` realm, `net.send`), mood, store (saves), server, `play.*` blocks, `order.*`, `ai.*`; the API reference is generated from the bindings (`tools/docs_site/lua_api.py`). Example game: `games/first_lua_game` (break-the-targets, in kke_demo). What `sv_` scripts spawn is replicated (bodies, breakables, balls). **Not yet:** character bindings, replicated script models. |
| Audio (`kke::AudioMixer`, `kke::AudioModule`) | 🟢 | docs/AUDIO.md: synthesized impacts per material (Jolt and FEMFX contacts, breaks), footsteps from the character's feet, ray-traced room reverb with wall echoes (turning probes that find narrow doors, into the next room too), sound round through openings with diffraction, occlusion through several walls by material and thickness, air absorption, a per-frame ray budget, stereo, binaural or Steam Audio's measured HRTF (`KKE_ENABLE_STEAM_AUDIO`, custom SOFA files) for headphones, UI earcons, navigation pings, sound visualizer with captions, WAV recording of the mix (`KKE_AUDIO_RECORD`); `games/audio_demo` with a station per case. **Not yet:** per-source room reverb, streaming music, Doppler, a Steam Audio build in CI. |
| Scene files (`kke::SceneFile`, `kke::SceneLoader`) | 🟢 | `*.scene.json` names pack assets (never ships them); the loader places models and adds Jolt mesh or box collision; Lua `scene.*`; Town block, Forest trail, Farm and the showcase course (docs/SCENES.md). |

## Networking

| System | Status | Notes |
|---|---|---|
| Replication measurement/demo | 🟢 | `NetworkModule` genuinely measures and displays what *would* be sent. |
| Real network transport | 🟢 | `kke::net::EnetTransport` (UDP, reliable + unreliable channels, LAN discovery), loopback and lag/jitter/loss simulation for tests; `NetModule` hosts and joins games (docs/NETWORKING.md); up to 8 local players per connection (`addLocalPlayer`). Join codes with UDP hole punch and relay fallback; every connection encrypted end to end (X25519 + XChaCha20-Poly1305, `kke/net/SecureTransport.h`, #44). **Not yet:** remembering a server's key per address, region relays picked by ping. |
| Authority / reconciliation model | 🟡 | `NetServer`/`NetClient`: owner-predicted players checked against speed limits, walls and flying (`WorldMoveCheck`) and corrected, or (competitive, `KKE_NET_REPLAY=1`) server-side input replay: the host runs every player's `Locomotion` from their inputs, clients predict and rewind (`kke/net/InputReplay.h`, docs/NETWORKING.md "Input replay"); server-authoritative bodies with snapshot interpolation, reliable events, host-spawned objects, breakables that break into the host's pieces everywhere. The old `kke_demo_game` `NetworkModule` is still only a measurement demo. **Not yet** (docs/NETWORKING.md "Not yet"): rollback/lockstep (#28), lag-compensated hits and player-vs-player collision under input replay, predicted breaks on clients. |
| Dedicated servers | 🟡 | `kke_server` (docs/SERVER_HOSTING.md): settings file + env + flags, password, admin/ban/allow list, console, clean stop; roles `physics` (scene collision + move checks), `leaderboard`, `directory` (self-hostable server list); Docker image + compose. Saves (#45): player records, leaderboards in the store, rotating backups; the game's Network panel lists a directory's servers. `relay` (join codes, #44) and `scripts` (server Lua) roles. Next: world saves (#45), shared roles (#46), rollback/lockstep roles (#28). |
| Storage | 🟢 | `kke::storage` (docs/STORAGE.md): one interface, SQLite built in (default), Valkey and PostgreSQL optional, same conformance tests on all four (checked against real Valkey 8 and PostgreSQL 17); kke_server `storage` setting. Lua `store.*` for games and server scripts (one collection per game); backups (SQLite, memory). Next: world saves on servers (#45). |
| Voice chat | 🟢 | `VoiceModule` (docs/NETWORKING.md "Voice"): Opus with FEC, push-to-talk / voice activated / open mic, server-routed nearby / team / everyone channels with flood caps and mutes, jitter buffer and loss concealment, 3D playback through the mixer. Microphone cleaned before coding: RNNoise noise suppression, SpeexDSP echo cancellation on the mixer's output (HW-018 waits for a real mic). |
| Anti-cheat (non-invasive) | 🟡 | `KKE_SHIPPING` builds compile out dev panels, Lua console, hot reload and debug switches; per-role server authority (`AuthorityPolicy`, `checkEvent`); fog of war for players (`Visibility`, `NetModule::fogOfWar`); game rules (`GameRules`); optional Kreative DRM licences (`kke::license`, `kke_license`, docs/DRM.md); `InputSanity` flags turbo, macros, noiseless sticks, inhuman reactions; sealed data (`kke::seal`, `kke_seal`). Server-side input replay (`KKE_NET_REPLAY`). Next (docs/ANTI_CHEAT.md): fog of war for projectiles and sounds (#49), input sanity on the server (#51), data digest at join (#52), reports and review queue (#53), aim analysis (#54), Lua net checks (#55), DRM activation service (#58), game rules in all tiers (#59). |
| DLC and mods | 🟡 | Content packs (`kke::packs`, `kke_packs`, docs/MODDING.md): `pack.json` folders laid out like the game's data (the Nexus/Vortex convention), Factorio-style dependencies, the player's load order (`mods.json`), DLC ownership through entitlements, host-shared DLC for guests, a layered case-insensitive mount with conflicts, and the session content a join compares. Wiring into games and the mods screen, .zip and Vortex, Steam Workshop, mod.io, multiplayer sync, selling DLC: #61. |
| Kreative DRM (`kke/License.h`, `kke_license`) | 🟡 | Optional, never on by default: one activation then a signed offline licence, machine ids hashed per game, an unlock licence for when a service shuts down, DLC entitlements for content packs (docs/DRM.md). **Not yet** (#58): the activation service as a `kke_server` role and the "is this you?" screens. |

## Tooling / dev experience

| System | Status | Notes |
|---|---|---|
| GPU profiler (`VulkanProfiler`) | 🟢 | Real integration, not a stub. |
| `StatsModule` | 🟢 | Real GPU timing per pass. |
| Headless verification workflow (Xvfb + lavapipe) | 🟢 | This is how every visual claim in this project's whole history has actually been checked — screenshot or log-inspect, never "should work." See docs/HISTORY.md "Build" and `AI_GUIDE.md`. |
| `BUGS.md` (this pair, bug side) | 🟢 | Defect log by ID (BUG-001...), symptom -> root cause -> fix; updated in the same session as each fix. |
| `ROADMAP.md` (this file) | 🟢 | Per-system status; updated with every status change. |
| Scripted physics benchmark (`KKE_PHYSICS_BENCH`) | 🟢 | Same scenes at the same simulation ticks on every machine, one comparable `BENCH RESULT:` line. `KKE_PHYSICS_THREADS` overrides the worker count; the pool respects CPU affinity, so `taskset -c 0` really is 1 thread. |
| Benchmark suite (`kke_bench` + stress test in CI) | 🟢 | Headless CPU cases plus the showcase stress test on every push to main, history and trend charts on the `benchmark-data` branch; Docker hardware profiles and `KKE_VRAM_BUDGET_MB`. docs/BENCHMARKS.md. |
| `docs/HARDWARE_TESTS.md` | 🟢 | Checklist of everything only real hardware can answer, with what to send back. |
| `tools/physics_lab` (`kke_physics_lab`) | 🟢 | Headless FEMFX experiments, no window/GPU: `fracture` (stability of every pattern), `shoot` (threshold tuning), `volcano` (worst-case load), `freefall`, `rest`. |
| Cross-platform builds | 🟡 | `docker compose run --rm linux / windows / android` (docker/). Linux: builds + all tests pass in the container. Windows: MinGW-w64 cross build of every game and tool, unit tests pass under Wine. Android: native build with FEMFX off. macOS / Windows-MSVC: manual "Platforms" GitHub workflow. Browser: needs a WebGPU renderer (docs/SCALING.md). |
| Starter template + `tools/new_game` | 🟢 | `games/template`: a character that walks, runs, jumps, vaults and climbs, a third-person camera and a Lua level; `tools/new_game NAME` copies it; the tutorials grow it (docs/tutorials/). |
| Cookbook (`docs/cookbook/`, `games/cookbook`) | 🟢 | ~25 tested Lua recipes from hello to A*, flocking and IK, with screenshots; every recipe runs headless in CI (`tools/docs_site/run_recipes.py`). |
| AI assistant support (`AGENTS.md`, `skills/`, `tools/check_game`, `llms.txt`) | 🟢 | Five self-contained skills (make a game, Lua, explain, check and debug, C++); `tools/check_game` runs a game headless and ends with OK or FAILED (checked in CI); `llms-full.txt` on the docs site (docs/AI_ASSISTANTS.md). |
| Docs site (MkDocs) and showcase website (Hugo) | 🟢 | Docs built with `--strict` to GitHub Pages on every push, Lua API reference generated from the bindings (docs/DOCS_SITE.md); engine.kreative-kompas.com is the Hugo site in Docker (`website/`). |
| Releases and packaging (`tools/packaging`, release.yml) | 🟡 | `v*` tags build a Windows zip and a Linux tarball with checksums and third-party licences (v0.1.0-alpha published) (docs/RELEASES.md). **Not yet** (#57): macOS `.app`, code signing, a bundled software-Vulkan fallback. |
| Playtest checklist (`docs/PLAYTEST_CHECKLIST.md`) | 🟢 | The look-and-feel pass through every demo for a person to tick off, grouped by demo. |

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
- **`docs/PLAYTEST_CHECKLIST.md`** — the look-and-feel pass a person makes
  through every demo, grouped by demo, with the thread each area came from.
  Notes written there become `BUGS.md` rows or fixes.
- **`docs/GO_TO_MARKET.md`** — the free-only plan for launching and funding
  the engine, with the launch gate (which milestones must be closed).
  Open work is tracked as GitHub issues labelled `M1`/`M2`/`M3`.
- **`docs/HISTORY.md`**'s own "Immediate next slices" — the fuller narrative
  of *how* and *why*, kept for depth/reasoning. Not a replacement for
  the files above, which exist specifically so that depth doesn't
  have to be re-read (or re-derived) just to answer "does X work yet?"
