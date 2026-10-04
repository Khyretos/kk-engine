# kke_demo: checks to run on soucouyant

The showcase world comes as a series, one commit per round, all on branch `kke-demo-world`:

| Round | Patch | Commit title |
|---|---|---|
| 1 | `kke-demo.patch` | kke_demo: spawn menu, picking things up, grid inventory core |
| 2 | `kke-demo-02.patch` | kke_demo: the bag (grid inventory screen), items to pick up, equipment on the character |
| 3 | `kke-demo-03.patch` | kke_demo: the open world round the yard (terrain, roads, zones, world map) |

Apply in order with `git am` (`git am -3` after other patches). One PR for the branch is fine;
title it after the newest round, or `kke_demo: showcase world (rounds 1-N)`.
Checked (rounds 1-3): the series applies on Forgejo `main` 9349996 alone, after racing-round, platoon-duel,
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
