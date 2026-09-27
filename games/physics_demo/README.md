# Physics demo

Soft and breakable objects on a studio floor, simulated with AMD FEMFX,
a finite element method (FEM) library for deformable solids. Six wooden
tetrahedra fall from staggered heights when it starts. From the panel,
the keyboard or a controller you add more tetrahedra of wood, stone,
iron, rubber or glass, and drop in prepared scenes: a glass pane that
shatters in a star, a stone brick that breaks into irregular chunks, a
rubber ball that squashes and bounces, a car that dents as it drives
into a wall that cracks, a cube that shatters in the material you chose,
a cube that dents and stays dented, and a "break test" where iron balls
hit glass, a wooden plank and a stone wall. Every impact and every break
makes a sound of the materials involved.

The demo teaches the three things FEMFX gives a game that ordinary rigid
bodies cannot: **deformation** (an object bends and springs back),
**plasticity** (it dents and stays dented) and **fracture** (it breaks
along cracks). It is the starting point for destruction in any game:
breakable walls and windows, crash damage on vehicles, props that
splinter, and the numbers (materials, thresholds, the debris budget) that
make breaking look right and stay fast. Ordinary crates, the character
and level collision use Jolt instead; mixing the two worlds is
[docs/PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md), which this demo
does not use.

There is no screenshot of this demo itself in `website/static/media/`.
`glass-shatter.webp` there shows the same break-test kind of scene in
`kke_demo`'s breaking yard.

## Run it

The executable is `physics_demo` (`kke_add_game(physics_demo ...)` in
[CMakeLists.txt](CMakeLists.txt)). It needs the CMake option
**`KKE_ENABLE_FEMFX=ON`**, which is **off by default**. The root
`CMakeLists.txt` only adds this folder inside `if(KKE_ENABLE_FEMFX)`, so
in a default build the target does not exist. The `everything` and
`everything-release` presets turn it on; the `low-end-pc` and
`android-arm64` presets turn it off. FEMFX's code is compiled with AVX2
and FMA (`KKE_AVX2_FLAGS`: `-mavx2 -mfma`, or `/arch:AVX2` on MSVC), so
the CPU must have them.

```bash
cmake --workflow --preset everything          # FEMFX, GPU profiler, Lua; build/
# or by hand:
cmake -B build -G Ninja -DKKE_ENABLE_FEMFX=ON && cmake --build build --target physics_demo

cd build/bin
./physics_demo
KKE_SKIP_INTRO=1 ./physics_demo                    # skip the logo intro
KKE_PHYSICS_SCENES=breaktest ./physics_demo        # start with a scene already spawned
```

| Variable | Effect | Read in |
|---|---|---|
| `KKE_PHYSICS_SCENES=a,b,...` | spawns scenes on the first physics tick: `glass`, `brick`, `ball`, `car`, `lava`, `cube`, `plastic`, `breaktest` | `PhysicsModule::fixedUpdate` |
| `KKE_PHYSICS_THREADS=<n>` | FEMFX worker threads (default: the usable cores, within the engine's resource budget) | `PhysicsModule::init` |
| `KKE_PHYSICS_BENCH=<ticks>` | the scripted benchmark: spawns a fixed list of scenes at fixed ticks, logs a `BENCH RESULT:` line, writes a report and quits | `PhysicsModule::init`, `benchTick` |
| `KKE_BENCH_DIR=<dir>` | where the benchmark report goes | `writeBenchReport` |

`cmake -P tools/run_physics_benchmarks.cmake` runs the benchmark once per
thread count and writes a summary ([tools/run_physics_benchmarks.cmake](../../tools/run_physics_benchmarks.cmake),
[docs/PERFORMANCE_NOTES.md](../../docs/PERFORMANCE_NOTES.md)).

Rebindings are saved to `physics_demo_input.json` (the name passed to
`InputModule` in [main.cpp](main.cpp)).

## Controls

The demo's actions are made in `PhysicsDemoModule::init`
([PhysicsDemoModule.cpp](PhysicsDemoModule.cpp)), in the "Physics" group.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Spawn a tetrahedron of the chosen material (`phys.spawn`) | Space | A (south) |
| Spawn the chosen scene (`phys.scene`) | Enter | X (west) |
| Next material (`phys.material`) | `[` | LB |
| Next scene (`phys.next`) | `]` | RB |
| Clear everything (`phys.clear`) | C | Y (north) |
| Turn the camera | left-drag | right stick (`camera.orbit`) |
| Closer / further | mouse wheel | d-pad up / down (`camera.zoom`) |
| Move the camera's target | right-drag | no controller binding |
| Give the panel the controls (`panel.toggle`) | F3 or Esc, or click the panel | View (Back) |
| In the panel: choose a row / change it / press / leave | arrows, Enter, Esc | d-pad or left stick, A, B |
| Developer panels (ImGui) | F1 (developer builds only) | no controller binding |

Notes from the code:

- Button names are positions (SDL's `SOUTH`, `WEST`, `NORTH`), so on a
  PlayStation pad A is Cross, X is Square and Y is Triangle. The panel's
  first line shows the right glyphs for the device you last touched.
- `[` and LB only step forward through the materials, and `]` and RB
  forward through the scenes; the panel's choice rows go both ways.
- All five demo actions and both camera actions are in the `game` input
  context, so they rest while the panel is Active. Enter then presses the
  highlighted row instead of spawning a scene.
- Esc works like a pause menu: it opens the panel with the keyboard on
  it, and Esc again goes back to the game. Esc does not close the window;
  the panel's "Quit" row, just above "Hide panel", does.
- The right stick and d-pad come from
  `OrbitCameraModule::setPadControls(true)` in [main.cpp](main.cpp).

## How it plays

There are no rules. You spawn things and watch them.

The panel ("Physics", on the left) replaced the old mouse-only Material
Grid. Its rows:

- The controls line, with button prompts.
- **Material**: Wood, Stone, Iron, Rubber, Glass
  (`MaterialGridModule::presetLabel`).
- **Spawn tetrahedron**.
- **Scene**: Break test, Glass sheet, Brick, Rubber ball, Car crash,
  Fracturable cube, Plastic cube.
- **Spawn scene**, **Clear all**.
- **Debris budget**: 0..500 pieces (starts at the module's 200).
- A live line: objects, FEMFX step time (average and maximum over the
  last second), ticks per second, pieces and how many are awake, faces
  drawn, frames per second.
- "Simulation can't keep up: running in slow motion", shown only when
  the last frame used the maximum number of fixed steps and physics ran
  below 55 ticks per second.
- A note: "Lava and melting: run melt_demo."

What each scene does (`PhysicsModule::spawnScene`,
[PhysicsModule.cpp](../../engine/src/modules/PhysicsModule.cpp)):

| Scene | What spawns | What it shows | How it breaks |
|---|---|---|---|
| Break test | clears the floor, then a glass pane on two blocks, a wooden plank bridging two blocks and a free-standing stone wall; about 4 s later (240 ticks) iron balls drop on the pane and plank and one is thrown at the wall | three break patterns side by side | KKE breakables, armed after settling |
| Glass sheet | a 2 x 0.15 x 2 m pane (12 x 1 x 12 cells, 864 tets) thrown down at 20 m/s | a radial star: small shards near the centre, long wedges further out | KKE breakable, `FracturePattern::Radial` |
| Brick | a 1 x 0.5 x 0.5 m stone brick (768 tets) thrown down at 20 m/s | irregular chunks in clusters | KKE breakable, `FracturePattern::Voronoi` |
| Rubber ball | a 0.35 m sphere of soft rubber thrown down at 18 m/s | squash and bounce, elastic | does not break (threshold 1e6) |
| Car crash | a 2 x 0.6 x 1 m "car" of aluminium-like metal driven at 22 m/s into a 0.3 x 2 x 3 m stone wall, both on the floor | the car dents (plasticity) and the wall cracks | FEMFX's own fracture for the wall |
| Fracturable cube | a 1 m cube (2 x 2 x 2 cells) of **the chosen material**, thrown down at 25 m/s | glass shatters, rubber survives, iron mostly dents | FEMFX's own fracture |
| Plastic cube | a 1 m cube of 6 tets, thrown down at 25 m/s | a permanent dent: the shape changes and does not spring back | does not break |

"Spawn tetrahedron" drops one tetrahedron of the chosen material from
5 m, with a little sideways jitter so repeated spawns do not stack. A
single tetrahedron cannot crack (it has no inner faces), so the material
changes its weight, stiffness and look. Use "Fracturable cube" to see
the materials break differently.

## How it works

### Startup and the frame

[main.cpp](main.cpp) is compiled with `#if KKE_ENABLE_FEMFX`. Without
FEMFX, `main` prints "physics_demo requires -DKKE_ENABLE_FEMFX=ON" and
returns 1; in practice CMake never builds it then. With FEMFX it creates
the `Application` (1280 x 720), sets the `studio` mood (a neutral
backdrop, `assets/moods/studio.yaml`) and adds the modules in this order:

1. `InputModule` (`physics_demo_input.json`)
2. `OrbitCameraModule` (distance 15, pitch -0.4, yaw -0.6, target
   (0, 1, 1)), with `setPadControls(true)`
3. `PhysicsModule(renderScale = 1.0, initialObjectCount = 6)`, its ImGui
   "Physics" window hidden (`setUiVisible(false)`)
4. `AudioModule` ("impacts and breaks make sound")
5. `SoundVisualizerModule`
6. `UiModule` (RmlUi)
7. `kke_physics_demo::PhysicsDemoModule`, the demo's controls and panel
8. `DemoPanelModule("Physics")`, on the left
9. `DebugControlModule`
10. `StatsModule`

Between 5 and 6 it turns on the second light as a cool fill light:
directional, direction (0.6, -0.3, 0.5), colour (0.55, 0.65, 0.85),
intensity 0.35, "so it softens shadowed faces without washing out the
real directional shading the key light provides".

`PhysicsDemoModule::dependencies()` requires `PhysicsModule` ("the soft
bodies it spawns") and `InputModule` ("controller and keyboard
controls"). Its `init` copies the module's debris budget, applies
material 0 (Wood), defines the five actions, calls
`InputModule::commitDefaults()` (which also loads saved rebindings), and
builds the panel section.

Each frame, `PhysicsDemoModule::update` reads the five actions and calls
the matching `PhysicsModule` function. All the simulation is in
`PhysicsModule`, which runs on the fixed step (60 Hz by default,
`Application`'s `fixedUpdateHz`):

- `frameStart` clears the frame's break and impact lists.
- `fixedUpdate` steps FEMFX (`AMD::FmUpdateScene(m_scene, ctx.fixedDt)`),
  splits KKE breakables whose borders broke, enforces the debris budget,
  collects impacts for sound, and runs the break test's timer, the
  `KKE_PHYSICS_SCENES` list (first tick) and the benchmark.
- `render` and `renderShadow` draw every piece and the floor.

`AudioModule` reads `PhysicsModule::frameImpacts()` and `frameBreaks()`
each frame and plays impacts of the materials involved.

### The demo module is thin on purpose

`PhysicsDemoModule` holds three ints (material, scene, debris budget)
and does nothing but map input and panel rows to calls that
`PhysicsModule` already has:

```cpp
if (m.pressed("phys.spawn")) physics->spawnTetrahedronHere();
if (m.pressed("phys.scene")) spawnChosenScene();
if (m.pressed("phys.material")) {
    m_material = (m_material + 1) % kke::MaterialGridModule::presetCount();
    applyMaterial();
}
if (m.pressed("phys.next")) m_scene = (m_scene + 1) % kSceneCount;
if (m.pressed("phys.clear")) physics->clearAll();
```

`applyMaterial` writes the preset into `PhysicsModule::selectedMaterial()`,
which `spawnTetrahedronHere` and the Fracturable cube scene read. The
scene list `kScenes` leaves out `Scene::LavaMelt`: "LavaMelt is kept for
the scripted benchmark only; real melting lives in games/melt_demo."

### Materials

A `kke::Material` ([Material.h](../../engine/include/kke/Material.h))
holds both physics and look: density, stiffness, Poisson's ratio,
`fractureStressThreshold`, `plasticYieldThreshold`, `plasticCreep`,
metallic, roughness and a texture id. The five presets are in
`MaterialGridModule.cpp`:

| Preset | Density (kg/m3) | Stiffness | Fracture threshold | Yield threshold | Creep |
|---|---|---|---|---|---|
| Wood | 600 | 1e7 | 8,000 | 6,000 | 0.3 |
| Stone | 2500 | 3e7 | 4,000 | 3,000 | 0.1 |
| Iron | 7870 | 2e8 | 150,000 | 100,000 | 0.2 |
| Rubber | 1200 | 1e5 | 1,000,000 | 700,000 | 0.05 |
| Glass | 2500 | 7e7 | 2,000 | 1,800 | 0.02 |

The thresholds were measured, not derived. The comment above the presets
says a first attempt scaled them with stiffness and got rubber and iron
backwards; a temporary log inside FEMFX then measured the real stress
each material reaches in this engine's standard fracturable-cube drop
(rubber about 1,200 to 8,450; iron about 6,690 to 314,820), and each
threshold was placed within its own material's range: glass and stone
near the low end (they break), wood in the middle, iron high (it mostly
dents), rubber above its maximum (it never breaks).

The scenes use their own materials, with values next to them in
`spawnScene`; for example the glass sheet's threshold is 1.5e5 because
breakables compare a different stress (see below).

### The simulation: tetrahedra, elasticity, plasticity

FEMFX represents a solid as a mesh of tetrahedra (tets). Each step it
works out how much each tet is stretched or squashed from its rest shape
and pushes the corners back with a force set by stiffness and Poisson's
ratio. That gives deformation for free: a soft rubber ball flattens when
it lands and springs back.

When the stress in a tet goes above `plasticYieldThreshold` and the
object was spawned with plasticity (`spawnPlasticTetMesh`), part of the
deformation becomes the new rest shape, at a rate set by `plasticCreep`.
The Plastic cube scene uses a very low yield threshold (2.0) and creep
1.0; its comment records how that was checked: the distance between two
corners grew from 1.0000 to over 1.02 and never went back.

Shapes come from `PhysicsModule::buildGridBox` (a box of cells, 6 tets
per cell) and `buildSphere` (the same grid pushed out onto a sphere).
Real meshes can be turned into tets with `kke::voxelizeToTets`
(`VoxelTets.h`) or loaded from `.ktet.json` (`TetMeshAsset.h`).

### Two ways to break

**FEMFX's own fracture** (`spawnFracturableTetMesh`): when a tet's stress
passes `fractureStressThreshold`, FEMFX separates it from its neighbour
along the tet face. The Fracturable cube and the car crash's wall use
it. Without help, every inner face can crack, so objects crumble into
single tets.

**KKE breakables** (`spawnPatternedBox`): the glass sheet, the brick and
the break test. The object is baked into pieces first
(`kke::bakeFracture` with a `FracturePattern`: `Radial` for glass,
`Voronoi` for stone, `Splinters` for wood, see
[FracturePattern.h](../../engine/include/kke/FracturePattern.h)). Each
step the stress in tets on a piece border is compared with that border's
threshold, overloaded borders break (`kke::BreakGraph`), and a part whose
pieces are no longer connected is swapped for one FEMFX body per
connected group, starting from the old vertex positions and velocities.
The reason is in the comment above `struct Breakable` in
[PhysicsModule.h](../../engine/include/kke/modules/PhysicsModule.h):
measured with `tools/physics_lab`, every chunked brick dropped at 20 m/s
blew up under FEMFX's own fracture, whatever the settings.

Two more ideas make breakables behave:

- **Settle, then arm.** The break test spawns its targets unbreakable,
  waits until they rest (at most 3 s), then sets each tet's threshold to
  the material's plus 1.25 times the stress it carried while settling.
  Only the stress an impact adds can break it, so a plank does not snap
  under its own weight (`TetSpawnOptions::armFractureAfterSeconds`).
- **A fracture seed.** Every breakable's pattern comes from the world's
  fracture seed mixed with its handle, so the same world breaks the same
  way on every run, and a network client can rebuild the host's pieces
  ([docs/NETWORKING.md](../../docs/NETWORKING.md) "Breakables"). The seed
  can be changed in the ImGui "Physics" window (F1); the RmlUi panel has
  no row for it.

### The debris budget

A break adds bodies, and FEMFX costs about 0.15-0.2 ms per awake body per
step on one core. `PhysicsModule::enforceDebrisBudget` keeps at most
`debrisBudget()` pieces of broken breakables: past it, it removes the
oldest sleeping piece, and only when none sleeps, the oldest awake one.
Whole objects are not debris. The budget covers KKE breakables only;
pieces from FEMFX's own fracture (the fracturable cube, the car-crash
wall) are not counted. The panel's slider calls `setDebrisBudget`; 0
means no limit. See [docs/OPTIMIZATION.md](../../docs/OPTIMIZATION.md)
(item 24).

### The floor, sleeping and threads

The floor is not a body. It is FEMFX's built-in collision plane at y = 0
(`controlParams.collisionPlanes.minY = 0`). The comment in
`PhysicsModule::init` gives the reason: a kinematic ground box used to
wake every resting piece on each step, because FEMFX treats rigid bodies
as always awake; with the plane, a settled 475-piece pile went from about
95 ms to about 0.2 ms a step on one core. The floor you see is a thin
slab clamped to at most 10 m wide (`groundWidth` in `render`), drawn so
its top is exactly at y = 0; the physics plane is unbounded.

FEMFX needs a task system. `PhysicsModule` gives it a thread pool with
one worker per usable core (capped by the engine's resource budget, or
`KKE_PHYSICS_THREADS`). With one worker it runs tasks inline. The history
is in [docs/HISTORY.md](../../docs/HISTORY.md) "Real multithreading".

### Rendering

`PhysicsModule` draws with the engine's `cube.vert` / `cube.frag` and
`shadow.vert` / `shadow.frag` shaders (listed in
[CMakeLists.txt](CMakeLists.txt)), with back faces culled, and a small
procedural texture per material (wood grain, stone speckle, brushed iron,
rubber, glass, lava), 64 x 64, generated in `init`. Every frame it
rebuilds the drawn surface of objects that still have an awake piece,
from FEMFX's vertex positions; sleeping objects keep their last surface.
Fresh crack faces get an interior colour (`kke/InteriorColor.h`). `renderScale` is 1.0 here,
so physics metres are render metres. The class's default is 0.02, which
was tuned for `kke_demo_game`'s small camera.

### Sound

`AudioModule` has two FEMFX inputs. `frameImpacts()`: FEMFX's collision
report (one contact per object pair per step, approaching faster than
1 m/s), plus landings on the floor, which FEMFX does not report, found
from a piece that was falling fast and stopped at the floor.
`frameBreaks()`: a hard hit of the material plus a few smaller ones for
the pieces. There are no Jolt bodies in this demo, so the audio's room
and occlusion rays hit nothing and everything sounds as if outdoors
([docs/AUDIO.md](../../docs/AUDIO.md) "FEMFX impacts").

### The panel and the developer panels

The panel is `kke::DemoPanelModule`
([docs/DEMO_PANEL.md](../../docs/DEMO_PANEL.md)), built in
`PhysicsDemoModule::init`. The stats line uses
`PhysicsModule::panelStats()`, which exists so a game's own UI can show
what the ImGui panel shows. The slow-motion warning compares
`Application::fixedStepsLastFrame()` with `maxFixedStepsPerFrame()` (2 by
default): when physics cannot keep up the engine drops time instead of
spiralling, so the game runs slower rather than freezing.

The ImGui windows (the "Physics" window with extra buttons and the
fracture seed, "Debug Control", "Performance") start hidden and F1 shows
them in developer builds.

## Design decisions

- **A separate demo instead of more buttons in `kke_demo_game`.** The
  comment at the top of [main.cpp](main.cpp): real physics units (a floor
  at metre scale, gravity 9.88, objects falling from 5 m) "don't share a
  sensible camera" with `kke_demo_game`'s unit cube "without a render-time
  scale hack shrinking everything down to nearly invisible." So
  `PhysicsModule` and `OrbitCameraModule` got constructor parameters
  (`renderScale`, `initialObjectCount`, and the camera's distance, pitch,
  yaw, target) rather than copies ([docs/HISTORY.md](../../docs/HISTORY.md)
  "games/physics_demo").
- **Six objects at the start, spread out.** "Enough to be visibly
  'several things happening,' spread out ... so they don't all land in
  one overlapping pile": `PhysicsModule::init` places them 2.5 m apart on
  a 3-wide grid, 1.5 m higher each, from 5 m.
- **An RmlUi panel instead of the Material Grid and the ImGui window.**
  Commit 944d596: "physics_demo's panel replaces the mouse-only Material
  Grid". The presets moved to static functions
  (`MaterialGridModule::presetCount/presetLabel/presetMaterial`) so any UI
  can use them, and `PhysicsModule` gained `clearAll`,
  `spawnTetrahedronHere` and `panelStats` so the panel calls the same
  code as the ImGui buttons.
- **The demo module only maps input to engine calls.** Every behaviour is
  a public `PhysicsModule` function, so a game, a script or the benchmark
  can do the same thing (the comment above `enum class Scene`: "callable
  from code rather than only from renderUi()'s buttons").
- **Lava is not in the scene list.** Its comment in `spawnScene` calls it
  "an honest, clearly-labeled approximation, not real melting physics";
  the panel sends you to `melt_demo` instead.
- **Thresholds from measurement.** See "Materials": the preset comment
  records the measured stress ranges and why a formula was dropped.
- **KKE breaks breakables, not FEMFX.** See "Two ways to break": FEMFX's
  own fracture exploded on chunked pieces in every setting tried;
  swapping in new plain bodies at a break is also cheaper
  ([docs/OPTIMIZATION.md](../../docs/OPTIMIZATION.md) item 21).
- **The floor is a collision plane.** It lets settled objects sleep
  (item 1 in [docs/OPTIMIZATION.md](../../docs/OPTIMIZATION.md)).
- **Car crash starts on the floor.** "They used to spawn ~6 m up and
  fall first, which is not what a car crash looks like."
- **A fill light.** The second light is turned on in
  [main.cpp](main.cpp) as a cool, dim fill from the opposite side of the
  key light.
- **`main` still checks `KKE_ENABLE_FEMFX`.** CMake already skips the
  folder, but the `#if` keeps `main.cpp` compiling if it is ever built
  without FEMFX, and says why it exits.

## Tuning

| What | Where | Effect |
|---|---|---|
| `renderScale` (1.0), `initialObjectCount` (6) | [main.cpp](main.cpp), `PhysicsModule` constructor | render scale of physics units; objects at start |
| Camera distance 15, pitch -0.4, yaw -0.6, target (0, 1, 1) | [main.cpp](main.cpp) | the starting view; distance limits are the camera's defaults, 0.5..30 m |
| Fill light direction, colour, intensity | [main.cpp](main.cpp) | softer or harder shadows |
| Material presets | `kPresets` in `engine/src/modules/MaterialGridModule.cpp` | lower `fractureStressThreshold` breaks sooner; lower `plasticYieldThreshold` dents sooner; higher `plasticCreep` dents faster |
| Scene materials, sizes, cells, speeds | `PhysicsModule::spawnScene` | more cells: more, smaller pieces, more cost |
| `chunkSize`, `cellsPerCluster`, pattern | `spawnPatternedBox` calls in `spawnScene` | piece size and how pieces group |
| Debris budget (200 default, 0 = no limit) | panel slider, `setDebrisBudget` | fewer pieces kept, cheaper steps |
| Break-test delay (240 ticks) | `m_breakTestTicks` in `spawnScene` | when the iron balls come |
| Spawn height (5 m) | `m_nextSpawnHeight` in `PhysicsModule.h` | drop height for tetrahedra and scenes |
| `KKE_PHYSICS_THREADS` | environment | FEMFX worker threads |
| Fixed-step rate and maximum steps per frame | `Application` (60 Hz, 2) | accuracy versus cost; when the slow-motion warning shows |

## Engine features it uses

| Feature | Header / module | Doc |
|---|---|---|
| FEMFX deformable bodies, plasticity, fracture | `kke/modules/PhysicsModule.h` | [HISTORY.md](../../docs/HISTORY.md) "Physics: AMD FEMFX integration" |
| Materials (physics and look) | `kke/Material.h`, `MaterialGridModule` presets | [HISTORY.md](../../docs/HISTORY.md) "kke::Material" |
| Fracture patterns, pre-baked pieces, break graph | `kke/FracturePattern.h`, `kke/VoronoiFracture.h`, `kke/BreakGraph.h` | [OPTIMIZATION.md](../../docs/OPTIMIZATION.md) |
| Tet meshes from meshes or files | `kke/VoxelTets.h`, `kke/TetMeshAsset.h` | [HISTORY.md](../../docs/HISTORY.md) "Content pipeline" |
| Debris budget, sleeping | `PhysicsModule::setDebrisBudget` | [OPTIMIZATION.md](../../docs/OPTIMIZATION.md), [SCALING.md](../../docs/SCALING.md) |
| Impact and break sounds | `kke/modules/AudioModule.h` | [AUDIO.md](../../docs/AUDIO.md) "FEMFX impacts" |
| Sound visualizer | `kke/modules/SoundVisualizerModule.h` | [AUDIO.md](../../docs/AUDIO.md) |
| Orbit camera with pad controls | `kke/modules/OrbitCameraModule.h` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Settings panel (RmlUi, pad + keyboard + mouse) | `kke/modules/DemoPanelModule.h` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Actions and bindings | `kke/modules/InputModule.h` | [INPUT.md](../../docs/INPUT.md) |
| Moods | `Application::setMood` | [MOODS.md](../../docs/MOODS.md) |
| Ragdolls on FEMFX (not used here) | `IRagdollPhysics` on `PhysicsModule` | [RAGDOLLS.md](../../docs/RAGDOLLS.md) |
| FEMFX and Jolt together (not used here) | `kke/modules/PhysicsBridgeModule.h` | [PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md) |

## Assets

None of its own and no Synty packs ([docs/SCENES.md](../../docs/SCENES.md)
lists `physics_demo` among the demos that use no packs). Shapes are built
from grids of tets in code and textures are generated in
`PhysicsModule::init`. From the engine's shared files it uses the
`studio` mood, the Noto Sans Regular and Noto Color Emoji fonts copied by
its [CMakeLists.txt](CMakeLists.txt), and FEMFX itself (vendored and
patched at `external/FEMFX/`: MIT, with Sony's vector-maths library
inside under BSD-3-Clause, see
[docs/DEPENDENCIES.md](../../docs/DEPENDENCIES.md)).

## Make a game like this

1. **Build with FEMFX.** `cmake --workflow --preset everything`, or add
   `-DKKE_ENABLE_FEMFX=ON`. Put your game's `add_subdirectory` inside an
   `if(KKE_ENABLE_FEMFX)` block like this demo's.
2. **Copy the folder.** `cp -r games/physics_demo games/my_wrecker`,
   rename the target, the namespace `kke_physics_demo`, the module and the
   input file. (For a Lua game, `tools/new_game` and the `breakable`
   table in [docs/SCRIPTING.md](../../docs/SCRIPTING.md) give you
   `breakable.box{...}` and `breakable.ball{...}` without C++.)
3. **Pick the right tool per object.** Jolt (`RigidBodyModule`) for
   crates, the level and the character; FEMFX for the few hero objects
   that must dent or break. Add `PhysicsBridgeModule` so the two collide
   ([docs/PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md)); it also hands
   small debris to Jolt, which is far cheaper.
4. **Break things with `spawnPatternedBox`** (or `spawnTetMeshWithOptions`
   with `chunkOfTet` for your own meshes): choose the pattern by material
   and pass `armSeconds > 0` for anything that rests before it is hit.
5. **Dent things with plasticity.** `spawnPlasticTetMesh`, or
   `TetSpawnOptions::plastic`, with a yield threshold low against the
   stresses your impacts reach.
6. **Tune materials by measuring.** Start from the presets, then change
   one threshold at a time and drop the same object from the same height.
7. **Set a debris budget** that fits your frame on your slowest target
   and show the slow-motion warning while you test.
8. **Read next:** [PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md),
   [RAGDOLLS.md](../../docs/RAGDOLLS.md),
   [OPTIMIZATION.md](../../docs/OPTIMIZATION.md),
   [SCALING.md](../../docs/SCALING.md),
   [HISTORY.md](../../docs/HISTORY.md) "Physics: AMD FEMFX integration",
   and `games/melt_demo` for melting.

Pitfalls the code shows:

- A single tetrahedron cannot fracture or visibly dent; use a grid box.
- The whole-scene capacity is fixed in `PhysicsModule::init` (512 objects,
  4096 pieces); FEMFX logs when a limit is hit.
- A rigid body touching a resting FEMFX piece keeps it awake. Keep
  kinematic bodies away from settled debris.
- Breakable thresholds and FEMFX-fracture thresholds are compared with
  different stresses: the glass sheet's 1.5e5 is not the glass preset's
  2,000.
- `renderScale` must match your camera: 1.0 for metre-scale scenes.
- FEMFX is not deterministic across machines; in multiplayer the host
  decides which borders break and clients follow
  (`setBreakableFollower`, `applyBrokenBorders`).

## Files

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | The FEMFX guard, the application, mood, camera, fill light and module list |
| [PhysicsDemoModule.h](PhysicsDemoModule.h) | The module: controls summary, chosen material, scene and debris budget |
| [PhysicsDemoModule.cpp](PhysicsDemoModule.cpp) | The scene list, the five actions, the RmlUi panel section, input handling |
| [game.json](game.json) | Marketplace manifest (id, title, description, tags, modules, requirements) |
| [CMakeLists.txt](CMakeLists.txt) | The executable, fonts and manifest copies, the cube, shadow and RmlUi shaders |
