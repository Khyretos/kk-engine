# kke_demo: checks to run on soucouyant

The showcase world comes as a series, one commit per round, all on branch `kke-demo-world`:

| Round | Patch | Commit title |
|---|---|---|
| 1 | `kke-demo.patch` | kke_demo: spawn menu, picking things up, grid inventory core |
| 2 | `kke-demo-02.patch` | kke_demo: the bag (grid inventory screen), items to pick up, equipment on the character |
| 3 | `kke-demo-03.patch` | kke_demo: the open world round the yard (terrain, roads, zones, world map) |
| 4 | `kke-demo-04.patch` | kke_demo: guns, grenades and the firing range (+ FEMFX far-from-origin fix) |
| 5 | `kke-demo-05.patch` | kke_demo: cars at the race track, a stunt plane at the airfield |
| 6 | `kke-demo-06.patch` | kke_demo: the nature park and the snow field |

Apply in order with `git am` (`git am -3` after other patches). One PR for the branch is fine;
title it after the newest round, or `kke_demo: showcase world (rounds 1-N)`.
Checked (rounds 1-5): the series applies on Forgejo `main` 9349996 alone, after racing-round, platoon-duel,
climbing-hands and sea-demo, and after goblin-horde. (goblin-horde itself doesn't apply after
the other four; that's between those patches, not this series.)

# Round 1

Patch: `kke-demo.patch`, one commit on top of Forgejo `main` 9349996
("Merge pull request 'Benchmarks: run on Forgejo ...' (#2)"). `git apply --check` passed there.

- Branch: `kke-demo-world`
- PR title: `kke_demo: spawn menu, picking things up, grid inventory core`
- Apply: `git am kke-demo.patch` (or `git am -3` after other patches).

**Queued patches:** applied after racing-round, platoon-duel, climbing-hands and sea-demo
(in that order) it still applies cleanly. The only shared file is
`engine/src/modules/GameShellModule.cpp`: racing-round adds a hunk in `frameStart`
just after the touch-editing block this patch touches; they merge by themselves.
No overlap with goblin-horde.

## What changed (18 files)
- `games/showcase/`: new `Spawner.cpp` (spawn menu), `Carry.cpp` (pick up, carry, throw),
  `Geometry.cpp/.h` (box, sphere, barrel meshes), `ui/showcase_spawn.rml`; edits in
  `ShowcaseModule.cpp/.h`, `Hud.cpp`, `CMakeLists.txt`, `README.md`, `scripts/toys.lua`.
- `engine/include/kke/Inventory.h`, `engine/src/Inventory.cpp`, `tests/test_inventory.cpp`:
  grid inventory core (no screen yet).
- `engine/src/modules/GameShellModule.cpp`: the hidden menu gets `pointer-events: none`.
  Before, RmlUi hovered its hidden body, `uiCapturesMouse()` was always true, and
  clicking the view in kke_demo never grabbed the mouse. Check other games' click-to-play too.
- `docs/SHOWCASE.md`: controls table.

## Already checked in the cloud (lavapipe, 4 cores, GCC RelWithDebInfo, -Werror)
- `kke_demo` and `kke_tests` built with zero warnings.
- clang `-fsyntax-only -Werror -Wall -Wextra` on every changed .cpp: clean.
- `python3 tools/ci/check_std_includes.py`: "Every std:: name used has its header included."
- `kke_tests`: 999 passed, 23 skipped (storage servers, packs), 0 failed. The 8 new `Inventory.*` pass.
- Headless runs (Xvfb, `KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0`): spawn menu opens on G, rows spawn,
  dummy ragdolls, clear works; `KKE_DEMO_CARRY=1` logs lift at 2.1 s, carried at chest height
  (y 0.93) at 4.3 s, thrown forward by 5.1 s. Only warning: the sky HDR missing in the sandbox.
- Screenshots: `kke-demo-shots/` (spawn.png, dummy.png, carrying.png).

## Run on soucouyant
```sh
cmake --build build --target kke_demo kke_tests -j4
build/bin/kke_tests --gtest_filter='Inventory*'
tools/check_game kke_demo --seconds 10            # expect OK, no new warnings
cd build/bin && KKE_DEMO_CARRY=1 ./kke_demo        # watch the crate get lifted and thrown; log has "carry demo:"
```

## Play list for Kees (controller and keyboard)
1. Click the view: the mouse is grabbed (this was broken).
2. RB (G): the spawn menu on the right. D-pad or stick to choose, A to spawn. Spawn a crate, a barrel,
   a tower, a dummy. The game keeps running while it's open. B or RB closes it.
3. Walk up to a crate, X (F): it's lifted in both hands. Walk, turn, bump it into a wall. X puts it down,
   RT (click) throws it. Try the iron crate: "too heavy", push it with Y (E).
4. Shoot or push the dummy: it falls like a ragdoll.
5. Spawn menu > "Clear what I spawned", then "Reset the world". Pause menu > "Reset the world".
6. Split screen still works (players 2-4: X no longer resets them; R on the keyboard resets).

## Not in this round (next rounds, Kees chose one open world)
Zones around the yard, the inventory screen, equipment, guns, explosions, cars, plane, nature park,
snow trails, bigger parkour park and traversal clips. Spawning and carrying are offline (and host) only.

# Round 2: the bag, items, equipment

## What changed (17 files)
- `games/showcase/Items.cpp` (new): item meshes, items lying in the world (Jolt debris bodies:
  you walk through them), picking up into the bag, equipment drawn on the character
  (`kke::Equipment`: hands, back, hips, head; fingers closed round held things), weight slows you.
  `KKE_DEMO_ITEMS=1` script (`=2` leaves the bag open).
- `games/showcase/InventoryScreen.cpp` + `ui/showcase_inventory.rml` (new): the bag screen.
- `games/showcase/data/items.json` (new): 14 kinds of item (copied to `bin/data/`).
- Supply table by the start (-4.5, 2.5); HUD prompt ("F Pick up Wood axe") and toast line.
- `engine/.../GameShellModule`: `selectIsTheGames`, so View opens the bag and only Start/Esc pause.
  `docs/GAME_SHELL.md` documents it. Other games are unchanged (unset = old behaviour).
- Reset the world empties the bag and puts everything back on the table.

## Already checked in the cloud
- GCC -Werror build of `kke_demo` and `kke_tests`: zero warnings. clang `-fsyntax-only -Werror -Wall -Wextra`
  on every changed .cpp: clean. `check_std_includes.py`: OK.
- `kke_tests`: 999 passed, 0 failed.
- `tools/check_game kke_demo --headless --seconds 10`: OK (only the sandbox's missing sky HDR).
- Headless runs: `KKE_DEMO_ITEMS=1` picks up 8-9 things, equips axe (right hand), rifle (back),
  helmet, canteen (left hip), lantern (left hand); 5 things worn. With `=2` and xdotool: keys take,
  turn, refuse a cell where it doesn't fit, drop; mouse click takes and places, right click equips
  (pistol went to the right hip). Screenshots: `kke-demo-shots/bag.png`, `bag-moving.png`, `worn.png`.

## Run on soucouyant
```sh
cmake --build build --target kke_demo kke_tests -j4
tools/check_game kke_demo --seconds 10
cd build/bin && KKE_DEMO_ITEMS=1 ./kke_demo     # watch it pick up, equip and wear things
```

## Play list for Kees (round 2)
1. Walk to the supply table left of the start. Face an item: the HUD says "Pick up ..."; X (F).
2. View (Tab or I): the bag. Move with the d-pad or stick (arrows, mouse), A (Space, click) takes an
   item; the cells go green where it fits, red where not; Y (R) turns it; A places it.
3. Put the axe on "Right hand", the rifle on "Back", the helmet on "Head", the pistol on a hip.
   X (E, right-click) equips straight away. Close with B or View: they're on the character.
4. RB (Q) in the bag drops the item in front of you. T / R3 sorts.
5. Fill the bag with firewood: past 15 kg you slow down, near 30 kg no sprinting.
6. Check other games' pause: View/Back still opens it there (only kke_demo gives View to the bag).

## Not yet
The axe doesn't chop and the guns don't shoot yet (rounds 4 and 6). Items are local only online.
Synty meshes for items come with the nature/guns rounds; for now they're built from shapes.

# Round 3: the open world

## What changed (12 files)
- `games/showcase/World.cpp` (new): 1.4 km terrain round the yard (one mesh, vertex every 5 m,
  ~158k triangles, one static Jolt mesh body, invisible walls at the edge), a mountain to the
  north west, six roads, a runway, a flag per zone, signposts at the gates; `groundHeight(x, z)`.
  The world map (M, or pause menu > World map): zones, roads, you; pick a zone to travel there.
  `KKE_DEMO_WORLD=1` tours every zone.
- `ui/showcase_map.rml` (new). `Layout.h`: gates and the zone table (`kZones`).
- Yard walls now have a gate in each side. HUD names the zone you're in.
- The Synty scenes moved to x/z 3000+ (out of the world); far plane 1500 m.

| Zone | Where |
|---|---|
| Parkour park | (0, -160), north gate |
| Firing range | (170, -150) |
| Airfield | (380, 40), east gate, runway x 185-575 |
| Race track | (0, 255), south gate |
| Nature park | (-230, 40), west gate |
| Snow field | (-300, -280), 30 m up the mountain, dirt trail |

The zones are empty level ground for now: their games come in rounds 4-8.

## Already checked in the cloud
- GCC -Werror build: zero warnings. clang `-fsyntax-only -Werror -Wall -Wextra` on every changed
  .cpp: clean. `check_std_includes.py`: OK (caught a missing `<cctype>`, fixed).
- `kke_tests`: 999 passed, 0 failed. `check_game kke_demo --headless --seconds 10`: OK (only the sky HDR).
- `KKE_DEMO_WORLD=1`: travels to all six zones; each logs "standing" on the ground with the right zone.
- Screenshots: `kke-demo-shots/map.png`, `zone1.png` to `zone6.png`, `zone-tour.png`.
- Speed on lavapipe was 9 FPS in the open (software rendering); check the frame time on the 9070 XT.

## Run on soucouyant
```sh
cmake --build build --target kke_demo kke_tests -j4
tools/check_game kke_demo --seconds 10
cd build/bin && KKE_DEMO_WORLD=1 ./kke_demo     # map, then every zone; log has "world demo:"
```

## Play list for Kees (round 3)
1. Walk out of any yard gate and follow the road: the signpost boards are the zone colours.
2. M (pause menu > World map with a controller): the map. Choose a zone, A travels there.
3. Run up the mountain to the snow field; look back at the yard from there (fog, far plane).
4. Check the frame time out in the open (Performance panel) and in split screen.

# Round 4: guns, grenades and the firing range

**Touches the engine:** `engine/src/modules/PhysicsModule.cpp` (BUG-088). Every FEMFX game uses
that code, so check them too (list below).

## What changed (16 files)
- `games/showcase/Guns.cpp` (new): rifle (automatic, 30) and pistol (12) fire from the right
  hand; aim (right mouse / LT) brings the camera in with a crosshair and turns the character
  with it; IK holds the gun (rifle to the shoulder, pistol in both hands, grenade raised).
  Shots raycast Jolt and FEMFX, the nearer hit wins; FEMFX hits break for real. Grenades
  (RT throws). Explosions: `physicsBlast` on both worlds, fireball, smoke, shockwave, flash,
  shake, synthesized boom. All sounds synthesized, no files. Reloads from the bag.
- `games/showcase/Range.cpp` (new): bench with the guns, 10 steel plates at 15/28/45 m,
  FEMFX glass, plank and two stone walls, 5 red barrels that chain-explode, a crate pyramid,
  two ragdoll dummies. `KKE_DEMO_GUNS=1` runs it all and logs each hit.
- HUD: crosshair, ammo "loaded / bag" (or grenades), "Plates down N of 10".
- Picking up a gun or tool with an empty right hand puts it in that hand.
- Effect shaders copied by the game's CMakeLists (without them the module turned itself off).
- **Engine fix BUG-088:** FEMFX rest positions were absolute world coordinates;
  `FmComputeShapeParams` lost float precision when x and z were both ~100 m+, so objects out
  there shook apart on spawn. Rest positions are now mesh-local, the spawn position goes to
  `FmInitVertState` as a translation.
- The terrain body is ignored by the FEMFX-Jolt bridge (its mirror box was the whole world).

## Already checked in the cloud
- GCC -Werror build of everything: zero warnings. clang `-fsyntax-only -Werror -Wall -Wextra`
  on every changed .cpp incl. PhysicsModule.cpp: clean (caught an unused constant, fixed).
  `check_std_includes.py`: OK.
- `kke_tests`: 999 passed. `check_game --headless` OK for kke_demo, physics_demo, melt_demo,
  tennis, sandbox (only the sky HDR warning, which the cloud has no file for).
- `KKE_DEMO_GUNS=1`: rifle reloads from the bag, 4 plates down, glass breaks into ~28 pieces,
  plank 5, stone wall 4-6; the red barrel sets off all 5 (crates thrown ~9-12 m); pistol
  brings it to 7 of 10 plates; grenade thrown and blows up where it lands.
- The pane at the range is steady now (before the fix it flew up on spawn).
- Screenshots: `kke-demo-shots/range-fire.png`, `range-boom.png`, `range-pistol.png`, `range-end.png`.

## Run on soucouyant
```sh
cmake --build build --target kke_demo kke_tests -j4 && build/bin/kke_tests
tools/check_game kke_demo --seconds 10
for g in physics_demo melt_demo tennis sandbox racing synty_demo first_lua_game; do tools/check_game $g --seconds 8; done
cd build/bin && KKE_DEMO_GUNS=1 ./kke_demo     # log: "guns demo:" and "guns: explosion"
cd build/bin && KKE_DEMO_BRIDGE=1 ./kke_demo   # yard glass: did the crates move? (0.000 m on lavapipe, before AND after the fix)
```
Things the cloud could not judge: how the guns sound on speakers, recoil and shake feel at
full frame rate, and whether FEMFX scenes in the other games look the same as before the fix.

## Play list for Kees (round 4)
1. M > Firing range. Pick up the rifle (F / X) from the bench: it goes in your hand.
2. Hold right mouse (LT) to aim, fire (left click / RT) at the plates, the glass, the walls.
3. Shoot a red barrel. Then take the pistol and the grenades; RT throws a grenade.
4. Run out of rounds: the click and the toast; pick up more from the bench.

Known: "Clear what I spawned" also removes the range dummies (reset the world brings them
back). No reload key yet (it reloads by itself when empty; R is reset). The range is local
only online (round 8).

# Round 5: cars and the plane

Game code only (no engine change). kke_demo now also compiles `games/flying_demo/Flight.cpp`
(the flight model, like kke_tests already does); `Flight.h` got a one-line comment saying so.

## What changed (11 files)
- `games/showcase/Vehicles.cpp` (new): the race track (oval 585 m, kerbs, start gantry, 8 cones,
  a jump), three cars on Jolt vehicle physics (hatch FWD, coupe RWD, truck AWD), the stunt
  plane (Flying demo flight model on a kinematic body), getting in/out with F / X, chase camera,
  lap timer (physics time), engine sound (`kke::EngineSound`), crash = explosion + wreck + new
  plane after 4 s. `KKE_DEMO_DRIVE=1` (or `=2`: plane only).
- New actions `drive.gas` (Shift, RT) and `drive.brake` (Ctrl, LT). Cars also use W/S, A/D.
- HUD: vehicle name, controls and live line (km/h, gear, lap / height, throttle, STALL).
- README "Cars and the plane", SHOWCASE.md, zone texts.

## Already checked in the cloud
- GCC -Werror build of everything: zero warnings. clang -Werror syntax on every changed .cpp
  (and Flight.cpp in this target): clean. `check_std_includes.py` OK. `kke_tests` 999 passed.
  `check_game kke_demo --headless`: OK (only the sky HDR).
- `KKE_DEMO_DRIVE=1`: the hatch drives a full lap (33.4 s), stops, you get out; at the airfield
  the plane rolls, lifts off at ~100 km/h, flies all four waypoints (up to 100 m, 206 km/h),
  dives into the runway: crash, explosion, wreck, a new plane, you beside it.
- Real keys (xdotool): F gets in, W drives, F gets out at the driver's door.
- `KKE_DEMO_GUNS=1` unchanged (same plates, barrels, grenade).
- Screenshots: `kke-demo-shots/car-park.png`, `car-lap.png`, `plane-flying.png`, `plane-wreck.png`.

## Run on soucouyant
```sh
cmake --build build --target kke_demo flying_demo kke_tests -j4 && build/bin/kke_tests
tools/check_game kke_demo --seconds 10
cd build/bin && KKE_DEMO_DRIVE=1 ./kke_demo    # log: "vehicles:" and "drive demo:" (lap, waypoints, crash)
```
Not judged in the cloud: how the cars feel at full frame rate (grip, the truck on the jump),
the engine sound, the plane with a pad or the T.16000M sticks (no stick bindings here yet).

## Play list for Kees (round 5)
1. M > Race track. Walk to a car, F / X gets in. Drive a lap (RT gas, LT brake); try the jump.
2. Knock the cones over on the far straight. Roll the truck, R puts it back on its wheels.
3. M > Airfield. Get in the plane, hold RT (Shift): it lifts off by itself. Loop, then land.
4. Crash it on purpose: the fireball, then the new plane on the runway.

Known: vehicles are local only online (round 8). Flight sticks aren't bound in the showcase
(the Flying demo has them).

# Round 6: the nature park and the snow field

Game code only (no engine change).

## What changed (11 files)
- `games/showcase/Nature.cpp` (new): ~85 trees round a meadow, ~520 Synty plants, grass blades
  near you that sway with gusts and bend away from your legs, drifting leaves. Synty POLYGON
  Nature trees/stumps/plants when installed (all or none), shape-built trees otherwise
  (`KKE_NATURE_ART=0` forces those). The axe (chopping block at the park gate, plus the supply
  table's): fire swings it with IK hands; four hits fell a tree (tips away from you, crash,
  shake), it becomes 2-4 log props and a stump; a swing at a log splits it into firewood.
  Flowers picked with F / X into the bag. Snow field: a 116 m grid of quarter-metre cells that
  keeps footprints (every stride, left/right) and tyre tracks, rebuilt in chunks; snow falling.
  `KKE_DEMO_NATURE=1`.
- HUD: park text changes with the axe in hand, "Trees felled: N"; prompt "Pick the Cornflower".
- Reset the world grows the forest back and lays fresh snow. README section, SHOWCASE.md rows.

## Already checked in the cloud
- GCC -Werror build: zero warnings. clang -Werror syntax on every changed .cpp: clean.
  `check_std_includes.py` OK. `kke_tests` 999 passed. `check_game kke_demo --headless`: OK
  (only the sky HDR).
- `KKE_DEMO_NATURE=1`, with and without POLYGON Nature: axe taken, four chops, the tree falls,
  lies as logs, one log split into firewood, three flowers picked (1 each in the bag), a walk
  through the snow leaves a trail of footprints.
- The series still applies alone on 9349996 and with `git am -3` after racing-round,
  platoon-duel, climbing-hands and sea-demo.
- Screenshots: `kke-demo-shots/forest-chop.png`, `tree-down.png`, `meadow.png`, `snow-trail.png`.

## Run on soucouyant
```sh
cmake --build build --target kke_demo kke_tests -j4 && build/bin/kke_tests
tools/check_game kke_demo --seconds 10
cd build/bin && KKE_DEMO_NATURE=1 ./kke_demo    # log: "nature:" and "nature demo:" lines
```
Not judged in the cloud: frame rate in the park at full resolution with the Synty undergrowth,
how the sway and the swing look in motion, the sounds.

## Play list for Kees (round 6)
1. M > Nature park. Walk through the grass: it parts round your legs. Watch the trees in the wind.
2. Take the axe from the chopping block (F / X). Fire four times at a tree: timber.
3. Carry a log, or swing at it to split it into firewood; pick it up into the bag.
4. Pick flowers in the meadow (F / X). Open the bag: they're in it.
5. M > Snow field. Walk a loop and look back at your footprints. Reset (R) for fresh snow.

Known: the forest and the snow are local only online (round 8).
