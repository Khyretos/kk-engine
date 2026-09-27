# Synty demo

A small room built from Synty POLYGON Prototype pieces (floor tiles, walls,
a door, stairs, columns, crates, barrels, cones, trees, a flag), with four
skinned Prototype characters standing in a row in the middle. Each one
shows a different way to move a skeleton: the FBX file's own animation
clip, a procedural wave written in code, idle breathing, and a pose you
build bone by bone from the "Characters" panel. With a physics module in
the build you can knock any of them over as a ragdoll, have them stand up
where they landed, and (in a FEMFX build) throw one through a pane of
glass that shatters. If the Quaternius Farm Animals pack is in the same
asset folder, a horse and a pug join them and ragdoll with animal joints.

The demo teaches how to use bought art in KKE: finding a pack on disk
whatever its layout, loading its FBX files with their textures, placing
pieces whose pivots are not where you expect, skinning and animating its
characters, and handing a character to physics and back. Start here for
any game built from an asset store kit: a level made of modular pieces,
characters that play clips or are posed in code, a character creator or
pose tool, or hit reactions and knockdowns.

There is no screenshot of this demo on the website yet.

## Run it

The executable is `synty_demo` (`kke_add_game(synty_demo ...)` in
[CMakeLists.txt](CMakeLists.txt): `add_executable` on desktop, a shared
library on Android). The root `CMakeLists.txt` always adds it, so every
preset builds it. What it can do depends on the physics in the build:

| Build | Ragdolls | Glass pane |
|---|---|---|
| `default` (`KKE_ENABLE_JOLT` on, `KKE_ENABLE_FEMFX` off) | Jolt (`RigidBodyModule`) | no |
| `everything` (Jolt and FEMFX on) | Jolt, meeting FEMFX glass through `PhysicsBridgeModule` | yes |
| FEMFX only | FEMFX (`PhysicsModule`), no joint limits | yes |
| neither | none: the panel says "Ragdolls need a physics module (Jolt or FEMFX)." | no |

It needs the Synty POLYGON Prototype pack on disk (see [Assets](#assets)).
Without it the demo still starts and the panel says where it looked.

```bash
cmake --build build --target synty_demo
cd build/bin
./synty_demo
KKE_SKIP_INTRO=1 ./synty_demo                       # skip the logo intro
KKE_ASSETS_DIR=/path/to/my/synty ./synty_demo       # packs somewhere else
```

| Variable | Effect |
|---|---|
| `KKE_ASSETS_DIR` (or the older `KKE_SYNTY_DIR`) | the folder that holds the packs; else `assets/synty` is searched upward from the working folder and from the executable's folder |
| `KKE_SKIP_INTRO=1` | skip the logo intro (developer builds) |
| `KKE_MOOD=<name>` | try another sky and light ([docs/MOODS.md](../../docs/MOODS.md)) |

Rebindings are saved to `synty_demo_input.json`.

## Controls

The actions are made in `SyntySceneModule::defineInput`
([SyntySceneModule.cpp](SyntySceneModule.cpp)) for player 1, in the
"Characters" group and the default `game` context.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Ragdoll the selected character (`synty.ragdoll`) | R | X (west) |
| Ragdoll everyone | Shift+R | the panel's "Ragdoll everyone" button |
| Everyone stands up (`synty.stand`) | T | Y (north) |
| Throw the selected one through a glass pane (`synty.glass`), FEMFX builds | G | A (south) |
| Show bones on / off (`synty.bones`) | B | left stick click |
| Previous / next character (`synty.prev`, `synty.next`) | [ / ] | LB / RB |
| Turn the camera (`camera.orbit`) | left-drag | right stick |
| Pan the camera | right-drag | no controller binding yet |
| Zoom (`camera.zoom`) | mouse wheel | d-pad up (closer) / down (further) |
| Settings panel (`panel.toggle`) | F3 or Esc, or click it | View (Back) |
| Engine developer panels (ImGui) | F1 | no controller binding yet |
| Quit | the panel's Quit row (Esc opens the panel) | the panel's Quit row |

Notes from the code:

- Shift+R is a raw key event in `onEvent`, not an action, so it cannot be
  rebound. `readInput` ignores `synty.ragdoll` while Shift is held, so
  Shift+R does "everyone" and not also "the selected one".
- F1 is also handled in `onEvent`. `main.cpp` calls
  `setDeveloperPanelsKey(false)` on the panel so the demo's own F1 switch
  is the only one; it shows and hides the camera, physics, debug control
  and stats windows listed in `setEnginePanels`. It works in any build,
  not only developer builds.
- The camera is `OrbitCameraModule` in its default `Viewer` controls with
  `setPadControls(true)` ([main.cpp](main.cpp)). Pad panning
  (`setPadPan`) is not turned on, so the left stick does nothing here.
- Button names are positions: on a PlayStation pad A is Cross, B is
  Circle, X is Square, Y is Triangle.

### The "Characters" panel

The panel is a `kke::DemoPanelModule` titled "Characters" on the left
edge, drawn with RmlUi ([docs/DEMO_PANEL.md](../../docs/DEMO_PANEL.md)).
It starts open with the game keeping the controls; the mouse can click
and drag any row. `panel.toggle` (View on a pad, F3 on the keyboard) makes
it Active: up and down pick a row, left and right change it (hold to
sweep), A presses, B hands control back. While it is Active, player 1's
`game` context is off, so A does not also throw someone through glass.
The last row, "Hide panel", collapses it.
Esc works like a pause menu: it opens the panel with the keyboard on
it, and Esc again goes back to the game. So Esc does not close the
window; the panel's "Quit" row, just above "Hide panel", does.
`setEscapeMenu(false)` gives Esc back to a game that needs it.

`buildPanel` adds these rows, top to bottom:

| Row | What it does |
|---|---|
| hint | the keys for the device you hold (a keyboard line and a controller line, `hint()`) |
| status | props, characters, draw calls and culled instances last frame |
| Show bones | the same switch as B |
| Ragdoll the selected one, Ragdoll everyone, Everyone stands up, Through a glass pane | shown only when a ragdoll module exists (`showIf`) |
| Character | pick who the rows below act on (the same as LB / RB) |
| model line | bones, clips and triangles of the selected character's model |
| Play the FBX clip | play clip 0 of the model, looping; shown only if it has clips |
| Rest pose (pose bones) | stop any clip, return to the rest pose, and switch this character to posing |
| Bone | every bone of the model by name; shown only while posing |
| X, Y, Z | that bone's rotation from rest, -180 to 180 degrees in steps of 5; shown only while posing |

Without the pack the panel shows only "Synty POLYGON Prototype pack not
found.", where to put it, and every folder it looked in.

## How it plays

It is a viewer and a sandbox, with no goal. The four characters stand
1.6 m apart along X:

| Character | Label | Behaviour |
|---|---|---|
| `SK_Character_Dummy_Male_01` | Dummy (male) - FBX clip | plays the FBX's first clip, looping |
| `SK_Character_Dummy_Female_01` | Dummy (female) - procedural wave | right arm up, forearm swinging, head turning |
| `SK_Character_Male_Face_01` | Male - idle breathing | arms down, spine breathing, head looking around |
| `SK_Character_Female_Face_01` | Female - hand-posed | starts selected, in its rest pose, ready for the Bone and X/Y/Z rows |

Any character can be switched to "clip" or "pose" from the panel.
Ragdolled characters fall away from the camera. "Everyone stands up" (T)
gets each ragdoll back on its feet where its pelvis landed, facing the
way it faced before, and it goes back to what it was doing.

## How it works

### Startup and the frame

[main.cpp](main.cpp) creates the `Application` (1280 x 720), sets the mood
`morning` and a far plane of 200 m, then adds the modules in this order:

1. `InputModule` (`synty_demo_input.json`)
2. `UiModule` (RmlUi, for the panel)
3. `OrbitCameraModule` (distance 9 m, pitch -0.35, yaw 2.85, target (0, 0.9, -1.5)), `setPadControls(true)`
4. `ModelModule`: loads, skins and draws every FBX
5. `PhysicsModule` (FEMFX), only with `KKE_ENABLE_FEMFX`: render scale 1,
   no starting objects, and `setDrawGround(false)` because the level
   draws its own floor
6. `RigidBodyModule` (Jolt), only with `KKE_ENABLE_JOLT`
7. `PhysicsBridgeModule`, only when both are on: Jolt ragdoll limbs and
   FEMFX glass push each other
8. `kke_demo::SyntySceneModule`, the demo; it declares `ModelModule` as a
   required dependency ("loads and draws the Synty FBX models")
9. `DemoPanelModule("Characters")` with `setDeveloperPanelsKey(false)`
10. `DebugControlModule` and `StatsModule`

The camera, physics, bridge, debug control and stats modules are passed
to `scene.setEnginePanels(...)`, which hides their ImGui windows until F1.

Each frame, `ModelModule::update` runs first (it was added first) and
advances every playing clip into the instance's bone locals. Then
`SyntySceneModule::update`:

1. `readInput()` acts on the pressed actions.
2. `setShowBones` passes the bone switch to `ModelModule`.
3. For each character: if it is a ragdoll, read the bodies from physics
   and override the skeleton with them; else apply its procedural
   behaviour and, if it is standing up, blend from the ragdoll pose.

`ModelModule` then skins and draws everything. The demo has no `render`
of its own.

### Finding the pack

`init` looks for the folder with `kke::findAssetFolder`
([kke/AssetCatalog.h](../../engine/include/kke/AssetCatalog.h)):

```cpp
const char* base = SDL_GetBasePath();
m_packDir = kke::findAssetFolder("assets/synty", { "KKE_ASSETS_DIR", "KKE_SYNTY_DIR" }, base ? base : "", &m_searched);
if (!m_packDir.empty()) m_catalog = kke::AssetCatalog::scan(m_packDir);
if (m_packDir.empty() || !m_catalog.find("SM_Buildings_Floor_5x5_01")) {
```

`findAssetFolder` tries the environment variables first, then
`assets/synty` under the working folder and up to four parents, then the
same from the executable's folder. `m_searched` keeps every place it
tried, which is what the panel lists when nothing is found.

`AssetCatalog::scan` walks the folder and indexes every model by file name,
whatever the pack's layout (`Characters/`, `StaticMeshes/`, `FBX/`,
`_SourceFiles/`...). Each pack also records its texture folders, its
default atlas (`*_Texture_01*`) and its overlay images (`*Grid*`). The
demo checks for one known file, `SM_Buildings_Floor_5x5_01`, to decide
whether the Prototype pack is there. If not, it logs at info level (a
missing pack is the normal case in CI), still defines the input and builds
the panel, and returns.

### Loading an FBX: `load`

```cpp
std::string name = fs::path(relative).stem().string();
const kke::CatalogAsset* asset = m_catalog.find(name);
...
kke::ModelLoadOptions opts;
opts.textureSearchPaths = pack->textureDirs;
opts.fallbackTexture = pack->defaultTexture;
return m_models->load(asset->path, opts);
```

The paths in the source (`"StaticMeshes/SM_Prop_Crate_01.fbx"`) are only
for reading: `load` keeps the file name and lets the catalog find it. A
missing file logs a warning and returns 0, and `place` skips it, so one
missing prop never stops the scene.

`ModelModule::load` caches by path and calls `kke::loadModel`
([kke/ModelAsset.h](../../engine/include/kke/ModelAsset.h)), which reads
FBX and OBJ through ufbx and converts whatever the file used into meters,
right-handed, +Y up, triangles only. It returns a `ModelData`: meshes,
materials, bones (parents always before children, each with `localRest`
and `inverseBind`), up to four bone weights per vertex, and animation
clips sampled at 30 frames a second into per-bone local matrices.

Synty FBX files name their textures with the artist's own paths
(`U:/Dropbox/...`). The loader looks the file name up in the model's
folder, `Textures` folders next to it, and the `textureSearchPaths` the
demo passes. The comment explains `fallbackTexture`: "Some meshes reference
textures from other Synty packs (the trees point at POLYGON Military's
atlas); fall back to this pack's own." All Synty meshes are UV-mapped onto
one atlas, so the pack's atlas is the right guess.

### Placing a piece: `place`

```cpp
// Synty pivots vary: building pieces sit at a corner, props at their
// center (so a crate would be half in the floor). Place every piece by
// its bounds instead: centered on `position` in X/Z, bottom at its Y.
glm::vec3 center = (d->boundsMin + d->boundsMax) * 0.5f;
glm::vec3 offset(-center.x, -d->boundsMin.y, -center.z);
```

The transform is translate to `position`, turn by `yawDegrees` about +Y,
scale, then the offset. So a position in `init` always means "the middle
of the piece's footprint, standing on this height".

The level is a 20 x 20 m floor of sixteen 5 x 5 m tiles (at y = -0.1),
four wall pieces along z = -10 (one with a door), two along x = -10,
stairs, two columns, and props. The Prototype look comes from one call:

```cpp
m_models->setWorldOverlay(proto->overlayTextures.front(), 2.0f, 1.0f);
```

That multiplies the pack's grid image over every instance in world
space, one tile per 2 m, which is what Synty's own Prototype shader does.
Because the grid is in world space, the lines stay 2 m apart on every
piece, whatever its scale.

`ModelModule` draws copies of the same model as one instanced draw per
mesh part, skips instances outside the view (frustum culling, and
culled characters are not skinned), and sorts opaque draws nearest first.
The panel's status line shows the draw calls and culled count so you can
watch this work while you turn the camera.

### The characters: clips, procedural bones, posing

The four characters are spawned in `init` from a small table (`Spec`: the
file, the label, the behaviour). The one with behaviour `"clip"` gets
`m_models->playAnimation(c.instance, 0, true)`.

`ModelModule` keeps, per instance, the local (parent-relative) matrix of
every bone. A playing clip overwrites them every frame (linear blend
between the two nearest sampled frames). When no clip plays, the game
can edit them through `boneLocals()` and the next frame is skinned from
them. Skinning is on the CPU: world matrices from the locals, times each
bone's `inverseBind`, applied to each vertex's four weights. The header
says why: it "keeps this simple and needs no new shader or vertex
format; a Synty character is ~2k triangles".

The demo edits bones with one helper:

```cpp
// Rotates a bone relative to its rest pose, in the bone's own local axes.
void SyntySceneModule::rotateBone(Character& c, const char* boneName, glm::vec3 euler) {
    ...
    (*locals)[b] = d->bones[b].localRest * r;
}
```

`r` rotates about X, then Y, then Z, in degrees. Because it multiplies
the rest matrix, (0, 0, 0) is always the rest pose, and the angles are
in the bone's own frame, not the world's.

- **Wave:** `UpperArm_R` at Z 70, `lowerarm_r` at Z 40 plus 30 x
  sin(6t), `head` at Y 12 x sin(1.3t). The comment is a lesson in
  itself: the axes "were found by trying them on this rig (Synty bones'
  local frames aren't aligned with the world), which is exactly what the
  Pose panel is for", and "Left/right arm bones are mirrored in this rig:
  +Z raises the right arm but lowers the left."
- **Breathe:** `spine_02` and `spine_03` sway 2.5 and 2 degrees on
  sin(1.8t), the head turns 20 degrees on sin(0.5t), both upper arms at
  Z -65 so they hang down from the bind pose.
- **Pose:** nothing every frame. The X, Y and Z sliders call `rotateBone`
  on the chosen bone when they change, and the result stays in the
  locals. `m_poseEuler` holds the three angles for every bone of the
  selected character; `select()` resets it to zero.

"Rest pose (pose bones)" calls `playAnimation(instance, -1)`, which stops
the clip and resets every local to `localRest`, and sets the behaviour to
`"pose"` so the Bone and X/Y/Z rows appear (`Ref` functions return null to
hide them otherwise). Bone names come from the model, so the same panel
poses any rig.

"Show bones" is `ModelModule::setShowBones`: every skinned instance's
skeleton drawn on top with the depth test off.

### Ragdolls

The scene never names a physics engine. In `init`:

```cpp
m_physics = kke::bestRagdollPhysics(app.findCapability<kke::IRagdollPhysics>());
```

`IRagdollPhysics` ([kke/Capabilities.h](../../engine/include/kke/Capabilities.h))
is an interface both physics modules implement. `bestRagdollPhysics`
picks the one with the highest `ragdollQuality()`: FEMFX is 0 (no joint
limits, limbs pass through each other), Jolt is 1 (cone and twist limits,
limbs collide). So with both in the build, Jolt ragdolls win.

`addJoltLevel` gives Jolt the level, because "Jolt only sees what it's
given": a static floor box (60 x 1 x 60 m, top at y = 0) and the two
walls as thin boxes. The Synty meshes themselves are not physics.

`ragdoll(c, push)` in steps:

1. Take the character's current bone world matrices (`boneWorld`, times
   the instance transform), so the ragdoll starts in whatever pose it is
   in, mid-clip or mid-wave.
2. Build a `kke::RagdollDesc` from the skeleton:
   `kke::buildHumanoidRagdoll` (11 boxes: pelvis, torso, head, upper and
   lower arms, thighs, calves) or, for an animal,
   `kke::buildQuadrupedRagdoll`. Both look bones up by name and fill in
   human or animal joint limits ([docs/RAGDOLLS.md](../../docs/RAGDOLLS.md)).
   A missing bone means no ragdoll and a warning naming it.
3. `kke::bindSkeletonToRagdoll` records which bones ride which body.
4. `m_physics->createRagdoll(desc, 0)` creates it.
5. Push it: the torso (the chest on an animal) and the head get the full
   push, the pelvis 40% of it. The comment: "Shove the upper body harder
   than the legs so it topples, not slides."

The push is away from the camera, flat, 4 m/s plus 1 m/s up
(`ragdollAll`), "so you see them fall".

Every frame after that, `update` reads the bodies and drives the skin:

```cpp
if (c.ragdoll && m_physics && m_physics->ragdollBodyTransforms(c.ragdoll, bodies)) {
    const kke::ModelData* d = m_models->model(c.model);
    glm::mat4 worldToModel = glm::inverse(m_models->transform(c.instance));
    m_models->setBoneWorldOverride(c.instance, kke::poseFromRagdoll(*d, c.binding, bodies, worldToModel));
    continue;
}
```

While a world override is set, `ModelModule` ignores the clip and the
locals, so the physics pose is what you see.

### Standing up

`standUp` (T, or the panel) does four things:

1. Moves the character's instance to where the ragdoll's pelvis landed
   (X and Z; its height and facing stay).
2. Converts the lying pose into the new spot's model space and stores it
   in `blendFrom`, with `blendAge = 0`.
3. Destroys the ragdoll.
4. Restores the behaviour it had before.

Then `blendToAnimation` runs each frame for 0.6 s (`kStandUpSeconds`): it
builds the animated pose from the locals, eases a weight with
`kke::blendWeight`, and sets the override to
`kke::blendPoses(blendFrom, animated, w)` (rotations slerped, positions
lerped, bone by bone). At weight 1 it clears the override and the
animation drives the skeleton again. A new knockdown during the blend
cancels it.

### Through glass (FEMFX builds)

`throughGlass` only exists when `KKE_ENABLE_FEMFX` is on (`kHasGlass`);
in other builds there is no glass action, prompt or panel button at all. It stands a breakable pane 1.8 m in front of
the selected character, away from the camera, centred 1.21 m up and
turned to face the camera, then ragdolls the character into it:

```cpp
glass.density = 2500.0f;
glass.stiffness = 7.0e7f;
glass.poissonsRatio = 0.22f;
glass.fractureStressThreshold = 2000.0f; // same measured glass value as PhysicsModule's Glass Sheet scene
...
physics->spawnFracturableBox(glm::ivec3(6, 6, 1), glm::vec3(2.2f, 2.4f, 0.1f), pane, glass, yaw);
ragdoll(c, away * 11.0f + glm::vec3(0, 1.5f, 0));
```

The pane is a FEMFX deformable body of 6 x 6 x 1 cells (6 tetrahedra
each) that breaks where the stress passes the threshold. The ragdoll is
still whatever `m_physics` is: with Jolt, `PhysicsBridgeModule` mirrors
the Jolt limbs into FEMFX as boxes and feeds FEMFX's pushes back, and
small pieces are handed to Jolt as rubble once they break off
([docs/PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md)). The comment
notes the one place the demo needs a concrete module: the pane needs
"the concrete PhysicsModule", while "the ragdoll itself only goes through
IRagdollPhysics."

### Animals

After the four people, `init` looks for `Horse` and `Pug` in the catalog.
When found, each is spawned at its spot, turned and scaled (0.3 and 0.2),
marked `animal` with its mass (500 kg and 8 kg), and plays the first clip
whose name contains "Idle". They are ordinary characters from then on:
they can be selected, posed and ragdolled (with `buildQuadrupedRagdoll`).

### Input and the panel

`defineInput` makes six button actions, each with one key and one pad
button, then `commitDefaults()` so a saved rebinding file can override
them. `readInput` reads them with `map(0).pressed(...)` every frame.
`buildPanel` builds the rows described under [Controls](#controls);
values are read live every frame, so pressing B also flips the "Show
bones" row. The "Character" choice uses a `Ref` that copies `m_selected`
into `m_selectedIndex` each frame, so LB and RB move the panel's choice
too.

## Design decisions

- **Find assets by name, not by path.** The comment on `load`: "only the
  file name matters: the catalog finds it wherever the pack keeps it".
  Synty's packs are laid out differently (`FBX/`, `StaticMeshes/`,
  `_SourceFiles/`), and people unzip them in different places.
- **Packs are never in the repository.** [CMakeLists.txt](CMakeLists.txt):
  the pack is found at runtime instead of copied into the build because
  "it's large, and licensed per user". The panel's message says the same.
- **A missing pack is not an error.** Commit 34fadf2 moved the log line
  to info level: it is "the one normal case in CI and for anyone without
  the packs", and the demo already says so on screen. A single missing
  asset inside a present pack still warns.
- **Place by bounds.** Synty pivots are not consistent (building pieces
  at a corner, props at the centre), so `place` centres every piece on
  its footprint and stands it on its lowest point.
- **CPU skinning.** `ModelModule`'s header: simple, no new shader or
  vertex format, and a Synty character costs a fraction of a millisecond.
  GPU skinning is named there as the upgrade path for crowds.
- **Four characters, four techniques.** The header of
  [SyntySceneModule.h](SyntySceneModule.h): "each showing something
  different: the FBX's own animation clip, a procedural wave, idle
  breathing, and a hand-posed skeleton". One scene shows every way a
  skeleton can be driven.
- **Posing in the bone's own axes, relative to rest.** `rotateBone`
  multiplies `localRest`, so zero is always the rest pose. The wave's
  comment shows the cost: the axes differ per bone and are mirrored left
  to right, which is why a pose panel exists to find them.
- **Ragdolls through an interface.** The comment in `init`: "Optional and
  loosely coupled: any module implementing IRagdollPhysics enables
  ragdolls; Jolt's (RigidBodyModule) is preferred over FEMFX's when both
  are there." Commit 14fa5dc added Jolt for joint limits and limbs that
  collide, which FEMFX's own rigid solver does not have.
- **Stand up where you landed.** Commit 14fa5dc: "T stands a ragdoll up
  where its pelvis landed, blending from the lying pose back to its
  animation over 0.6 s", instead of snapping back to the spot it was
  knocked from.
- **A morning sky.** Commit 3b2bd78 changed the mood from `golden_hour`
  to `morning` so the Prototype grid keeps its true colours.
- **An RmlUi panel a controller can drive.** Commit a9d24ad replaced the
  ImGui "Characters" window with `kke::DemoPanelModule` and gave every
  action a pad button. The rule in [docs/DEMO_PANEL.md](../../docs/DEMO_PANEL.md):
  ImGui is for developers (F1); what a player uses is RmlUi.
- **The engine panels behind F1.** `setEnginePanels` hides the ImGui
  windows at start ("so the scene stays visible"), and the demo handles F1
  itself, so the panel's own F1 key is off.

## Tuning

| What | Where | Effect |
|---|---|---|
| overlay tile 2 m, strength 1 | `init`, `setWorldOverlay` | grid size on every piece; strength 0 shows the plain atlas |
| character spacing 1.6 m | `init`, `(i - 1.5f) * 1.6f` | where the row stands |
| wave: 70 / 40 + 30 sin(6t) / 12 sin(1.3t) degrees | `update` | arm height, forearm swing and speed, head turn |
| breathe: 2.5 and 2 degrees at 1.8 rad/s, head 20 degrees | `update` | how deep and how fast |
| ragdoll push 4 m/s away + 1 up | `ragdollAll` | how hard R and Shift+R shove |
| pelvis share 0.4 | `ragdoll` | lower = topple, higher = slide |
| human mass 70 kg (`Character::mass`), horse 500, pug 8 | [SyntySceneModule.h](SyntySceneModule.h), `animals[]` | ragdoll weight |
| joint limits | `c.ragdollDesc.findJoint(...)` or `scaleLimits` before `createRagdoll` (the comment in `ragdoll` says where) | stiffer or looser bodies ([docs/RAGDOLLS.md](../../docs/RAGDOLLS.md)) |
| `kStandUpSeconds = 0.6` | [SyntySceneModule.cpp](SyntySceneModule.cpp) | how long the getting-up blend takes |
| glass: 6 x 6 x 1 cells, 2.2 x 2.4 x 0.1 m, `fractureStressThreshold` 2000 | `throughGlass` | more cells = more, smaller shards at more cost; lower threshold = breaks easier |
| glass throw 11 m/s + 1.5 up, pane 1.8 m ahead | `throughGlass` | how hard the character hits it |
| pose slider step 5 degrees | `buildPanel` | finer or coarser posing |

## Engine features it uses

| Feature | Header / module | Doc |
|---|---|---|
| Finding and indexing asset packs | `kke::findAssetFolder`, `kke::AssetCatalog` ([kke/AssetCatalog.h](../../engine/include/kke/AssetCatalog.h)) | [SCENES.md](../../docs/SCENES.md) |
| FBX / OBJ loading, texture resolving | `kke::loadModel`, `kke::ModelLoadOptions` ([kke/ModelAsset.h](../../engine/include/kke/ModelAsset.h)) | |
| Instances, CPU skinning, clips, bone locals, bone overlay, world overlay, instancing, culling | `kke::ModelModule` ([kke/modules/ModelModule.h](../../engine/include/kke/modules/ModelModule.h)) | [OPTIMIZATION.md](../../docs/OPTIMIZATION.md) |
| Ragdoll presets, skin binding, pose blending | `buildHumanoidRagdoll`, `buildQuadrupedRagdoll`, `bindSkeletonToRagdoll`, `poseFromRagdoll`, `blendPoses`, `blendWeight` ([kke/Ragdoll.h](../../engine/include/kke/Ragdoll.h)) | [RAGDOLLS.md](../../docs/RAGDOLLS.md) |
| Physics by capability | `kke::IRagdollPhysics`, `kke::bestRagdollPhysics` ([kke/Capabilities.h](../../engine/include/kke/Capabilities.h)) | [RAGDOLLS.md](../../docs/RAGDOLLS.md) |
| Jolt bodies and ragdolls | `kke::RigidBodyModule`, `kke::RigidWorld` | |
| FEMFX fracture | `kke::PhysicsModule::spawnFracturableBox`, `kke::Material` | |
| Jolt and FEMFX together | `kke::PhysicsBridgeModule` | [PHYSICS_BRIDGE.md](../../docs/PHYSICS_BRIDGE.md) |
| Actions, bindings, button prompts | `kke::InputModule` | [INPUT.md](../../docs/INPUT.md) |
| Settings panel | `kke::DemoPanelModule` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Orbit camera with pad controls | `kke::OrbitCameraModule` | [DEMO_PANEL.md](../../docs/DEMO_PANEL.md) |
| Sky and light | `Application::setMood("morning")` | [MOODS.md](../../docs/MOODS.md) |

## Assets

Listed in [docs/SCENES.md](../../docs/SCENES.md) "Synty demo". The room is
placed in code, not loaded from a scene file.

| What | Pack | In the repo? |
|---|---|---|
| Level: `SM_Buildings_Floor_5x5_01`, `SM_Buildings_Wall_5x3_01`, `SM_Buildings_WallDoor_5x3_01`, `SM_Buildings_Stairs_1x3_01`, `SM_Buildings_Column_1x3_01` | Synty POLYGON Prototype | no |
| Props: `SM_Prop_Crate_01`, `SM_Prop_Crate_02`, `SM_Prop_Crate_Question_01`, `SM_Prop_Barrel_01`, `SM_Prop_Chest_Wood_01`, `SM_Prop_Cone_01`, `SM_Prop_Barrier_01`, `SM_Prop_FlagPole_01`, `SM_Generic_Tree_01` to `_03`, `SM_Generic_Small_Rocks_01` | Synty POLYGON Prototype | no |
| Characters: `SK_Character_Dummy_Male_01`, `SK_Character_Dummy_Female_01`, `SK_Character_Male_Face_01`, `SK_Character_Female_Face_01` | Synty POLYGON Prototype | no |
| The atlas (`*_Texture_01*`) and the grid overlay (`*Grid*`) | Synty POLYGON Prototype | no |
| Optional animals: `Horse`, `Pug` | Quaternius Farm Animals (CC0) | no |

Synty packs are never committed (they are licensed per user, and
`assets/synty/` is in `.gitignore`). Put the extracted pack folder in
`assets/synty/` in any layout, for example
`assets/synty/POLYGON_Prototype/Characters/...` (a symlink works), or set
`KKE_ASSETS_DIR` ([assets/README.md](../../assets/README.md)).

What happens when something is missing:

- **No folder, or no Prototype pack in it:** an info line in the log, and
  the panel says "Synty POLYGON Prototype pack not found." with where to
  put it and every folder it looked in. Only the sky shows.
- **One file missing from a pack that is there:** a warning
  ("asset '...' not found in any pack under '...'") and that piece or
  character is left out.
- **No Farm Animals:** no horse and no pug, nothing logged.

The `morning` mood's sky picture (Qwantani Mid Morning, CC0 Poly Haven)
and ambience (`meadow_day`) are fetched or shipped for every demo; they
are not packs. Only `AudioModule` plays a mood's ambience, and this demo
does not add one, so the meadow loop is not heard (sea_demo and melt_demo
add one for this reason).

## Make a game like this

1. **Copy the folder.** `cp -r games/synty_demo games/my_game`, rename the
   target in [CMakeLists.txt](CMakeLists.txt) (and the `game.json` copy
   path), the namespace `kke_demo`, the module's `name()`, the input file
   name in [main.cpp](main.cpp), give `game.json` a new id, and add
   `add_subdirectory(games/my_game)` to the root `CMakeLists.txt`.
   `tools/new_game` makes Lua-only games from `games/template`; if your
   game is mostly rules and levels, start there and load packs from a
   scene file instead ([docs/SCENES.md](../../docs/SCENES.md)).
2. **Keep the pack finding.** `findAssetFolder` plus
   `AssetCatalog::scan` plus `load` is the whole pattern. Change the
   check file (`SM_Buildings_Floor_5x5_01`) to one from your pack.
3. **Build your level with `place`.** Swap the Prototype pieces for your
   kit. Keep placing by bounds, or your pieces will float or sink.
4. **Drive your characters.** Use `playAnimation` for clips. For clips
   from another rig (the engine's UAL library), see how
   [jiggle_demo](../jiggle_demo/README.md) retargets them with
   `kke::matchBones` and `kke::retargetAnimations`, and use `kke::Animator`
   for blending.
5. **Find bone axes with the panel.** Before writing procedural motion
   like the wave, select the character, press "Rest pose (pose bones)",
   pick the bone and move X, Y and Z to learn which axis does what.
6. **Add knockdowns.** Copy `ragdoll`, `standUp`, `blendToAnimation` and
   the ragdoll branch of `update`. Give Jolt your level's floor and walls
   (`addJoltLevel`), or ragdolls fall forever.
7. **Read next:** [RAGDOLLS.md](../../docs/RAGDOLLS.md),
   [DEMO_PANEL.md](../../docs/DEMO_PANEL.md),
   [INPUT.md](../../docs/INPUT.md), [SCENES.md](../../docs/SCENES.md).

Pitfalls the code shows:

- Bone names are exact and case-sensitive (`findBone`), and Synty mixes
  cases in one rig (`UpperArm_R` but `lowerarm_r`). A wrong name does
  nothing, silently.
- Editing `boneLocals()` has no effect while a clip plays or a world
  override is set. Stop the clip first (`playAnimation(instance, -1)`).
- The ragdoll builders need the named bones. A different rig gives
  "can't ragdoll: skeleton has no '...' bone"; pass your names
  (`kke::QuadrupedBones` for animals) or rename.
- Start the ragdoll from the current pose (`boneWorld` times the
  instance transform), not the rest pose, or the character snaps before
  it falls.
- Jolt only collides with what you add to it. Render meshes are not
  physics.

## Files

| File | What is in it |
|---|---|
| [main.cpp](main.cpp) | The application, the mood, the module list (physics modules by build option), the camera, which ImGui panels hide behind F1 |
| [SyntySceneModule.h](SyntySceneModule.h) | The module, the `Character` struct (behaviour, ragdoll state, stand-up blend), all state |
| [SyntySceneModule.cpp](SyntySceneModule.cpp) | Finding the pack, loading and placing pieces, the level, the characters and animals, procedural bones, ragdolls, standing up, the glass pane, input actions, the "Characters" panel |
| [game.json](game.json) | Marketplace manifest (id, title, tags, modules) |
| [CMakeLists.txt](CMakeLists.txt) | The executable, its shaders (models, instancing, shadows), the manifest, and `kke_use_ui` (the RmlUi shaders and fonts) |
