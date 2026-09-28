# Cloth and hair

Six scenes of things that are soft and thin:

- six fabrics falling on tables and flying as banners
- a bed made of three layers of cloth
- a hammock and a tennis net catching balls
- a runner in a cape going through a curtain
- a hair catalog: every hair type from 1A to 4C, and an afro, a puff, a
  high-top fade, a twist-out, bantu knots, box braids, cornrows, locs and
  two-strand twists, on heads turning in the wind
- a stress test that shows what cloth costs

Cloth never passes through cloth, not even through itself, and hair never
goes into the head. That is the engine's default, and a panel switches it
off so you can see and time the difference.

The demo teaches:

- cloth on Jolt soft bodies and the engine's clipping protection
  ([docs/CLOTH.md](../../docs/CLOTH.md))
- fabric presets, and a fabric shader with weave, sheen and thread gloss
- guide-strand hair drawn on the GPU ([docs/HAIR.md](../../docs/HAIR.md))
- skinned cloth with back-stops, colliders only cloth sees, and wind
- a settings panel that works with a controller

Start here for capes, flags, curtains, bedding, sails, nets, trampolines,
hair and fur, or any game where something soft has to meet something
solid without clipping. Everything is built from simple shapes, so no
Synty assets are needed.

## Run it

The executable is `cloth_demo` (`kke_add_game(cloth_demo ...)` in
[CMakeLists.txt](CMakeLists.txt)). The root `CMakeLists.txt` only adds it
when `KKE_ENABLE_JOLT` is on, which is the default.

```bash
cmake --build build --target cloth_demo
cd build/bin
./cloth_demo
KKE_SKIP_INTRO=1 KKE_CLOTH_SCENE=hair ./cloth_demo       # straight to the hair
KKE_BENCHMARK=1 ./cloth_demo                              # tours every scene and records frame times
```

| Variable | Effect |
|---|---|
| `KKE_CLOTH_SCENE=fabrics\|bed\|nets\|cape\|stress\|hair` | Start in that scene |
| `KKE_CLOTH_PROTECTION=full\|basic\|off` | Start with that protection |
| `KKE_CLOTH_WIND=<m/s>` | Wind speed (default 4, and 3 in the hair scene) |
| `KKE_CLOTH_COUNT`, `KKE_CLOTH_RES` | Stress scene: sheets, vertices per side (default 16, 24) |
| `KKE_HAIR_GUIDES`, `KKE_HAIR_PER_GUIDE` | Hair scene: guide strands per head, hairs drawn per guide (default 160, and 0 = as the style draws) |
| `KKE_HAIR_MOTION=0..1` | Hair scene: hair physics, 1 natural (default), 0 solid (no simulation) |
| `KKE_HAIR_DETAIL=0.05..1` | Hair scene: the share of hairs drawn (default 1, 0.5 on phones), for phones and slow GPUs |
| `KKE_HAIR_SHOW=types34\|styles\|braids\|types12\|classic` | Hair scene: the catalog's page (default types 3 and 4) |
| `KKE_HAIR_STYLES=4c,afro` | Hair scene: exactly these heads, from `hairStyleNames()` and `hairstyleNames()` |
| `KKE_CLOTH_GPU=0\|shared` | Search for cloth pairs on the CPU only, or on the graphics queue (docs/CLOTH.md, "On the GPU") |
| `KKE_CLOTH_GPU_CHECK=1` | Search on the GPU and the CPU and log any difference |
| `KKE_CLOTH_TOUR=1` | Visit every scene for 14 s each (on by itself under `KKE_BENCHMARK`) |

Rebindings are saved to `cloth_demo_input.json` (the name passed to
`InputModule` in [main.cpp](main.cpp)).

## Controls

Every binding is a rebindable action in the "Cloth" group, made in
`ClothDemoModule::defineInput` ([ClothDemoModule.cpp](ClothDemoModule.cpp)).

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Next scene (`cloth.scene`) | Tab | RB |
| Previous scene (`cloth.scene_back`) | \` | LB |
| A scene directly (`cloth.scene1`..`6`) | 1 to 6 | |
| Drop everything again (`cloth.redrop`) | R | A |
| Clipping protection: Full, Basic, Off (`cloth.protection`) | P | X |
| Gusts on or off (`cloth.gusts`) | G | Y |
| Orbit, pan, zoom | left drag, right drag, wheel | right stick, d-pad |
| Settings panel | F3, or click it | View |

The settings panel (RmlUi, on the right) has the scene, the protection,
the wind, each scene's own settings, and a Cost section. The Cost section
shows the physics step, the protection pass and the mesh rebuild, the
contacts and undone crossings, and for hair the guide and hair counts and
the upload. The hair scene also has "Hair physics" (1 natural, 0 solid:
no simulation, the hair turns with the head as styled) and "Hair detail"
(the share of hairs drawn), both live, for slower devices. F1 shows the engine's developer panels.

## The scenes

| # | Scene | What to look at |
|---|---|---|
| 1 | Fabrics | Satin, leather, wool, denim, cotton and silk (left to right). Each is dropped on a little table, and each flies as a banner in gusty wind. Silk floats down last and drapes softest. Denim and leather fold stiffly. The weave (twill, satin, knit) shows up close. |
| 2 | Bed | A wool blanket, then a silk sheet (2.5 s), then a denim throw (5 s): layers on layers, folding on themselves. This is the hardest case for the protection ([CLOTH.md](../../docs/CLOTH.md), "Known limit"). |
| 3 | Nets | A hammock catching balls, and a tennis net stopping shots at 15 to 21 m/s. |
| 4 | Cape | A runner with a satin cape running through a linen curtain. The cape is skinned to the body, and back-stops keep it off the back. |
| 5 | Stress | Sheets of cotton over balls, dropped again every 5 s. Set the count and resolution in the panel. |
| 6 | Hair | A hairdresser's catalog on heads that turn, nod and now and then shake, in gusting wind. Show picks the page: types 3A to 4C (ringlets to tight coils that shrink and spring), hairstyles (afro, puff, high-top fade, twist-out, bantu knots), braids and locs (box braids, cornrows, locs, two-strand twists), types 1A to 2C, or the classic long, wavy, curly and short. Set the guides per head and the hairs drawn per guide in the panel. |

## How it works

### Startup and the frame

[main.cpp](main.cpp) sets the `studio` mood (an even light, which fabric
reads best in), then adds the modules: input, RmlUi, an orbit camera with
controller controls, `ClothDemoModule` and the `DemoPanelModule`.
`ClothDemoModule::init` reads the variables, builds the first scene, then
defines the input actions and the panel.

Each scene is a new `kke::RigidWorld` (`clear()`), so nothing of the last
scene lingers in Jolt. `fixedUpdate` runs at 60 Hz. It sets the wind
(steady or gusting: two slow waves and a flutter), runs each scene's
events (the bed's later layers, the nets' balls and shots, the runner, the
heads), then steps the world. `update` rebuilds the cloth meshes from
`clothPositions` and `clothNormals`, and sends each head's guide strands
to its `HairRenderer`. `render` and `renderShadow` draw it all.

### Cloth

`addCloth` passes a `ClothDesc` to `RigidWorld::addCloth` with the protection
chosen in the panel. Changing the protection rebuilds the scene, because
tethers and back-stops are built with the cloth. Sheets are drawn with
`DynamicMeshRenderer::drawCloth` (the fabric shader, both sides). Nets
are threads with no triangles, so `updateMeshes` builds a ribbon per
thread, facing the camera and at least a pixel wide.

- **Fabrics:** `clothFabric(name)` presets. The drapes get
  `ClothDesc::wind = 0.1` (sheltered, to show how each fabric falls). The
  banners are pinned along their top edge and get the full wind.
- **Bed:** boxes for the frame, mattress, headboard and pillows. The
  blanket is 46 x 46 vertices, the sheet 32 x 32 and the throw 22 x 22,
  added as the scene runs.
- **Nets:** `clothNet` grids pinned along their frame. `contactMass`
  makes the hammock (8 kg) and the tennis net (5 kg) feel as heavy as
  their frame holds them, so a ball stops instead of punching through.
- **Cape:** the runner's torso, legs and arms are kinematic capsules with
  `clothOnly` set, moved every step (`stepRunner`). The cape is skinned to
  the torso with `maxDistance` 2 m and a 1 cm back-stop, and its top edge
  is pinned. `setClothJoints` passes the torso matrix each step.

### Hair

`buildHair` makes a head per style on the catalog's page (the panel's
Show: types 3 and 4, hairstyles, types 1 and 2, or the classic four), with
skin tones and hair colours varied along the row. Each head is three
things:

- a kinematic sphere with `clothOnly` set: only cloth and hair feel it
- a static neck capsule, and solid shoulders
- a `HairDesc` from `hairstyleOnHead` (160 guides, off the face): the
  type, and for a hairstyle its cut (`lengths`) and ties

`stepHeads` turns each head about its neck (yaw, nod, tilt, and a quick
shake now and then). It then moves the sphere with `moveKinematic` and
calls `setHairJoint` with the head's matrix. The world does the rest: the
strands' roots follow the head, the strands swing and blow, and the
sphere and shoulders keep them out.

Drawing: a `HairRenderer` per head is built once from the `HairDesc`. Every
frame `update` passes it `hairPositions` and the head matrix, and the GPU
builds the hairs around the head's 160 guides (32 or 40 each), and for
types 3 and 4 the coils round every hair. The head mesh paints the scalp
the hair's root colour near every root, so no skin shows between hairs
(but the parts between bantu knots do), and paints the high-top's fade.
Under braids, cornrows and locs it paints only where they lie on the
scalp (the rest pose's pieces within a braid's width of it), so the parts
between them and the skin between cornrows show; those heads get a finer
mesh for it.

### The panel and the input

`buildPanel` makes one section per concern. Rows that belong to one scene
hide with `sectionIf`. The scene and protection choices are `Ref`s that
read the live value, so a key press shows in the panel too. `readInput`
turns the actions into scene changes, drops and toggles.

## Design decisions

- **One solver for cloth and hair: Jolt's soft bodies.** They already
  collide with every rigid body and character in a `RigidWorld`, and they
  run on the job system. FEMFX (volumes of tetrahedra) suits neither thin
  sheets nor strands.
- **No clipping by default.** Kees's rule: clipping is off unless a
  developer turns it on to save time. So `ClothProtection::Full` is the
  default, and the demo can show Basic and Off side by side on the same
  scene.
- **Hair is distance constraints, not rods.** Cosserat rods were tried:
  a quick head turn could flip a strand's rest curve upward, because
  nothing turns the first rod's twist with the head. Distances have no
  twist to lose, and cost 2.5 times less ([HAIR.md](../../docs/HAIR.md)).
- **Simulate a few, draw many.** 160 guides a head are simulated. The
  thousands of hairs are built on the GPU, so their only CPU cost is the
  0.1 ms upload.
- **Everything visible is anti-aliased without tricks.** Threads and
  hairs are at least a pixel wide, and the weave fades to its average
  where a thread gets smaller than a pixel. There is no dithering and no
  temporal accumulation.
- **A new world per scene.** It is simpler than removing everything, and
  nothing from one scene can wake or slow another.

## Tuning

| What | Where | Effect |
|---|---|---|
| `clothFabric(...)` presets | [engine/src/Cloth.cpp](../../engine/src/Cloth.cpp) | density, stretch, shear, bend, drag, thickness, look of each fabric |
| `hairStyle(...)` presets | [engine/src/Hair.cpp](../../engine/src/Hair.cpp) | length, segments, curl, coils, shrinkage, bend, hold, drawn hairs of each type |
| `hairstyleOnHead(...)` | [engine/src/Hair.cpp](../../engine/src/Hair.cpp) | each hairstyle's type, cut and ties |
| `drape.wind = 0.1` | `buildFabrics` | how sheltered the table drapes are |
| grid sizes (28 x 28 drapes, 46 x 46 blanket...) | each `build*` | detail against cost (cost grows with vertices) |
| `contactMass` 8 and 5 kg | `buildNets` | how hard the hammock and net stop balls |
| `maxDistance` 2, `backStop` 0.01 | `buildCape` | how far the cape may swing, how close to the back it may come |
| `m_hairGuides` 160, `m_hairsPerGuide` 0 (the style's) | panel, `KKE_HAIR_*` | simulation cost against how full the hair looks |
| `m_hairMotion` 1, `m_hairDetail` 1 | panel, `KKE_HAIR_MOTION`, `KKE_HAIR_DETAIL` | CPU and GPU cost against how the hair moves and how full it is |
| shake and turn amplitudes | `stepHeads` | how hard the heads move (the panel's Head motion scales them) |
| gust waves | `fixedUpdate` | how the wind varies |

## Engine features it uses

| Feature | Header / module | Doc |
|---|---|---|
| Cloth, fabrics, clipping protection | `kke/Cloth.h`, `RigidWorld::addCloth` | [CLOTH.md](../../docs/CLOTH.md) |
| Hair: guide strands, styles, scalp | `kke/Hair.h`, `RigidWorld::addHair` | [HAIR.md](../../docs/HAIR.md) |
| Hair drawn on the GPU | `kke/HairRenderer.h` (`shaders/hair.*`) | [HAIR.md](../../docs/HAIR.md) |
| Rigid bodies, kinematic and cloth-only colliders | `kke/RigidWorld.h` | [MOVEMENT.md](../../docs/MOVEMENT.md) |
| Meshes drawn from code, fabric shader, sphere impostors | `kke/SphereImpostors.h` (`DynamicMeshRenderer`, `SphereImpostorRenderer`) | [CLOTH.md](../../docs/CLOTH.md) |
| Settings panel with controller support | `DemoPanelModule` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Rebindable actions, button prompts | `InputModule`, `kke/InputMap.h` | [INPUT.md](../../docs/INPUT.md) |
| Mood: sky, light | `Application::setMood` | [MOODS.md](../../docs/MOODS.md) |
| Benchmark recording | `KKE_BENCHMARK`, `BenchRecorder` | [BENCHMARKS.md](../../docs/BENCHMARKS.md) |

## Assets

None: shapes and the `studio` mood only.

## Make a game like this

1. **Copy the folder.** `cp -r games/cloth_demo games/my_game`, rename the
   target and namespace, and add `add_subdirectory(games/my_game)` inside
   an `if(KKE_ENABLE_JOLT)` block in the root `CMakeLists.txt`.
2. **Give your character a cape or hair.** A cape: copy `buildCape`
   (a grid pinned at the top, skinned to the torso, `setClothJoints`
   every step). Hair: copy one head from `buildHair` and `stepHeads`, and
   use your character's head bone matrix as the head matrix.
3. **Give cloth something to lean on.** Put `clothOnly` capsules on the
   limbs that cloth should feel. Characters from `addCharacter` already
   carry one, so walking into a curtain pushes it aside.
4. **Pick fabrics and styles.** Start from a preset, change the colour,
   and change one behaviour number at a time.
5. **Budget it.** Keep a cape around 16 x 20 and a blanket around 32 x
   32. Use 100 to 200 hair guides per head. Use Basic protection for
   background cloth that never folds onto itself. Check the Cost section,
   or `kke_bench --filter cloth_` / `hair_`.
6. **Read next:** [CLOTH.md](../../docs/CLOTH.md),
   [HAIR.md](../../docs/HAIR.md), [DEMO_PANEL.md](../../docs/DEMO_PANEL.md).

Pitfalls the code shows:

- Move kinematic colliders with `moveKinematic` (not `setTransform`) once
  the scene runs. A teleport gives the cloth no velocity to react to, and
  it can end up inside.
- Call `setClothJoints` or `setHairJoint` every step, before `step`,
  with the same matrix the collider got. Otherwise cloth or hair lags one
  step behind its body.
- Changing the protection means rebuilding the cloth: tethers and
  back-stops are part of it.
- Paint the scalp the hair's colour. At any density there are gaps
  between drawn hairs.

## Files

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | The application, the mood and the module list |
| [ClothDemoModule.h](ClothDemoModule.h) | The module, the scene list, pieces, solids, balls, the runner, heads |
| [ClothDemoModule.cpp](ClothDemoModule.cpp) | The scenes, the wind, the runner and the heads, mesh rebuilds, drawing, input and the panel |
| [game.json](game.json) | Marketplace manifest (id, title, tags, modules) |
| [CMakeLists.txt](CMakeLists.txt) | The executable, its shaders and the UI files copied next to it |
