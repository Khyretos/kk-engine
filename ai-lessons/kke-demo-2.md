# kke-demo, part 2: growing a demo into an open world

Follows kke-demo.md (rounds 1-2). This covers round 3: terrain, roads, zones and a world map
in `games/showcase/`. Verified in the cloud (GCC -Werror, clang, kke_tests 999 passed,
check_game OK, a headless tour that stands at every zone).

## What happened
1. The yard (60 m, origin) became the centre of a 1.4 km world. Zones sit round it at fixed
   places in one table, `layout::kZones` in `Layout.h` (name, text, centre, radius, colour,
   where you arrive, which way you face). HUD, map, travel and later rounds all read that table.
2. Terrain is ONE mesh with a vertex every 5 m (281 x 281, about 158k triangles) and ONE static
   Jolt mesh body. Height = gentle swells + a mountain + a ring of hills at the edge, then
   "pads" flatten each zone and roads are levelled across their width.
3. `groundHeight(x, z)` reads the same grid with the same triangle split, so things placed
   later sit exactly on what the player walks on.
4. The world map is RmlUi: one data model, zone dots and road strips placed with
   `data-attr-style` strings built in C++ (left/top/width/rotate).
5. A demo script (`KKE_DEMO_WORLD=1`) opens the map, travels to each zone and logs
   "standing, zone here: X". That log line is the test.

## Why it was done this way
- One table for zones: no number is typed twice, so nothing drifts apart.
- One terrain body: thousands of small bodies cost more and leave seams to trip on.
- Same triangle split in `groundHeight` as in the mesh: bilinear on a quad is NOT what the
  physics sees. Pick the triangle (`fx + fz < 1`) the mesh uses, or objects float or sink.
- Flat pads for zones: cars, a runway and a parkour park need level ground; noise can stay
  everywhere else.
- Far plane raised to 1500 m so the far side is visible; fog hides the edge.

## Mistakes and fixes (learn these)
- The "you" arrow on the map used the character ▲; the font (Noto Sans) has no such glyph and
  showed a box. Build shapes from divs (a dot plus a "nose") instead of special characters.
- First map was too zoomed out and labels piled up: map only the part of the world that has
  zones, and clamp circle sizes (12-70 dp).
- The yard's "trick course" HUD hint fired out at the airfield: it only checked `x > 22`.
  Bound every station check on all sides once the world is bigger than the yard.
- Old Synty test scenes lived at x 400+, which is now inside the world. Moved them to 3000+.
- check_std_includes caught `std::tolower` without `<cctype>`. GCC 16 does not pull it in
  through other headers; always run the check before committing.
- Grey 0.23 for asphalt looked black under AgX tone mapping; 0.38 reads as asphalt.

## Exact commands (verified)
```sh
cd /home/user/build && ninja kke_demo kke_tests           # zero warnings expected
python3 tools/ci/check_std_includes.py                     # "Every std:: name used has its header included."
build/bin/kke_tests | grep -E "PASSED|FAILED"              # [  PASSED  ] 999 tests.
tools/check_game kke_demo --bin /home/user/build/bin --seconds 10 --headless
cd build/bin && ./drive.sh "wait1" tour.png 26 KKE_DEMO_WORLD=1   # Xvfb run, log in drive.log
grep "world demo:" drive.log                               # one "standing" line per zone
```
Do NOT set KKE_ASSETS_DIR to the shared assets cache for tests: the asset scan at startup
then takes minutes and the run looks hung.

## Patch series workflow
Each round is one commit on the branch; `git format-patch -1 HEAD --stdout >
patches/kke-demo-0N.patch`. Before saying it is ready: `git worktree add /tmp/wt <main>` and
`git am` the whole series there, alone and after the other queued patches (`git am -3`).

## Note for a 9B model
Doable: adding a zone to `kZones`, moving a road point, changing colours, adding a map row.
Hard: the terrain/ground-height match and the map's coordinate maths; copy the existing
functions, change one number at a time, and re-run the KKE_DEMO_WORLD tour after each change.

## Round 4: guns, grenades, the firing range (Guns.cpp, Range.cpp)
Verified the same way plus `KKE_DEMO_GUNS=1` (logs every hit and blast) and check_game on
four other FEMFX games after an engine change.

Lessons:
- FEMFX far from the origin: objects at (152, -154) shook apart on spawn; at (160, 0) they
  were fine. Bisect by POSITION, not by settings: same object, move it, compare. Cause: rest
  positions in world coordinates, and FEMFX inverts a matrix of them in float. Keep physics
  data local and pass the position as a separate translation.
- The FEMFX-Jolt bridge mirrors every Jolt body near FEMFX as a box. A terrain mesh's box is
  the whole world: tell the bridge to ignore huge static bodies (`PhysicsBridgeModule::ignore`).
- Physics runs slower than real time on a slow PC (3x on lavapipe). A fuse counted in frame
  time blew grenades up in mid-air. Count game timers that touch physics in `fixedUpdate`.
- Each game copies the shaders it uses. The particle effects need `effect*.spv`; without them
  the module failed init and turned itself off (one log line, easy to miss: grep "failed").
- Scripted demos on a slow machine: a frame can jump past a short time window. Fire every
  event whose time has passed this frame, don't test "is now inside the window".
- clang caught an unused `constexpr` GCC did not. Android CI is clang -Werror: always run
  the clang syntax check too.
- "Pushed 0 bodies" was not a bug: the barrels had already blown everything away. Check
  what is actually near before changing engine code.

For a 9B model: doable are new GunDef rows (magazine, interval, push), plate positions,
sound recipe numbers. Hard: the shot ray maths and anything in the FEMFX engine code; there,
reproduce with a scripted demo first and change one thing per run.
