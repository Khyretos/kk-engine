# kke_demo lessons, part 3 (round 6: nature park and snow field)

Continues kke-demo.md and kke-demo-2.md. Same workflow: code in the cloud,
patch to /mnt/project-files/kk-engine/patches/, Kees's PC runs the checks.

## 1. Read team memory before using an art pack's assets

What happened: the plan used SM_Plant_Grass_01 and SM_Plant_FlowerPatch_01.
Team memory (showcase-course-art) already said Grass_01 has zero bounds
and FlowerPatch_01 draws as flat atlas squares in our loader.
Fix: swapped them for SM_Plant_Bush_02 and SM_Plant_PurpleFlower_01.
Rule: before picking Synty asset names, check memory for "renders wrong"
notes, and list the real files:
```sh
find /mnt/project-files/kk-engine/assets-cache/POLYGON_Nature -name "SM_Plant_*.fbx" | sed 's|.*/||' | sort
```

## 2. Load a whole art set or none of it

Half Synty trees and half shape trees look like a bug. Load every model,
and use them only if all of them loaded (a bool `all`). Otherwise fall back
to shapes for everything. Add an env switch (`KKE_NATURE_ART=0`) so the
fallback can be tested even where the pack is installed.

## 3. Test a pack quickly: a folder with only that pack

The catalog scan of the whole asset cache is slow. For a test, point
KKE_ASSETS_DIR at a folder that links only the pack you need:
```sh
mkdir -p /tmp/natassets && ln -sfn /mnt/project-files/kk-engine/assets-cache/POLYGON_Nature /tmp/natassets/POLYGON_Nature
KKE_ASSETS_DIR=/tmp/natassets KKE_DEMO_NATURE=1 ./kke_demo
```

## 4. Grid resolution decides whether a trail shows

What happened: footprints on a 0.5 m snow grid pressed about one vertex
each. The log said "6 snow cells pressed" and nothing was visible.
Cause: a foot is ~0.15 x 0.3 m, smaller than one cell.
Fix: 0.25 m cells (464 x 464 over 116 m) and a smaller field. The trail
then showed clearly. Rule: a stamp must cover at least 2-3 cells across.
Keep the mesh cheap by splitting it into chunks (8 x 8 here) and rebuilding
only the dirty ones, a few per frame. A vertex on a chunk seam belongs to
two chunks: mark both dirty.

## 5. A demo script needs a log line per step

KKE_DEMO_NATURE logs each step ("chop 1 of 4", "the tree lies as 3 logs",
"picked a flower_red (1 in the bag)", "N snow cells pressed"). Then one
headless run proves the whole feature without looking at a picture:
```sh
cd build/bin && ./drive.sh "" shot.png 52 KKE_DEMO_NATURE=1 KKE_NATURE_ART=0
grep "nature" drive.log
```
Screenshots then only need to show it looks right. Time each shot by the
log's timestamps (the demo starts about 4 s after launch).

## 6. Small API gotchas found by the compiler

- `glm::rotation(a, b)` needs gtx (experimental). Use the gtc constructor
  `glm::quat(from, to)` (both unit vectors) instead.
- A new .cpp needs adding to the game's CMakeLists, then `cmake .`.
  The link error ("ld returned 1") is the sign you forgot.

## 7. A falling tree, cheaply

No physics body while it falls: a rod tipping over its foot,
`speed += 1.5 * g / height * sin(angle) * dt`, which starts slow and
ends fast and looks right. When it lands, swap it for physical log props
plus a static stump. Remove the standing trunk's static body when it starts
to fall, or the character stays blocked by an invisible trunk.

## 8. 9B capability note

A 9B model can do steps 1, 3, 5 and 6 from this file. Steps 4 and 7 need
someone to look at the result (the log count, a screenshot) and reason
about why. Give it the measurement to check ("pressed cells > 50") rather
than asking whether it looks right.

# Round 7: parkour park, wall climb, hang vault

## 9. List an animation pack's clips before planning around them

The plan said "hang and shimmy clips". There are none. Check first:
```sh
build/bin/kke_model_info "$KKE_ASSETS_DIR/Universal Animation Library 2/Unity/UAL2.fbx" | grep -iE "hang|shimmy|climb|vault"
```
No clip means a procedural pose: IK hands and feet stepping in a cycle
(plant for 60% of a stride while the body moves past, then lift and move on).

## 10. A unit test world has edges

Debugging the shimmy in a test at z = -150 failed: the character fell
forever. The test floor is only 100 m square round the origin. Move the
geometry to near (0, 0) when copying a game layout into a test.

## 11. Demo scripts: drive by state, not by the clock

Under lavapipe the frames and the physics run at different speeds, so a
script that says "jump at 0.3 s" misses. The parkour demo waits for
states instead ("jump when 1.3 m from the wall", "next step when Hang").
Give each step a generous timeout that logs FAILED.

## 12. A new move must not steal an old one

Wall climb first grabbed a beam above a low wall, skipping the trick
course's "hang, then leap up". Fix: require one wall the whole way up
(rays every 0.5 m). Hang vault came before "leap up" and stole it too:
order matters. After adding a move, rerun the old demos
(KKE_DEMO_TRICKS, KKE_DEMO_HANG, KKE_DEMO_AUTOPILOT) and compare logs.

## 13. Freeze a fast move for a screenshot

A 0.6 s move is 5 frames at 9 fps. Hitting it by timing is luck. The demo
stops calling Locomotion::update (dt 0) once the move is half done
(KKE_DEMO_PARKOUR_FREEZE=wallclimb), then takes the shot.

# Round 8: spawning and carrying online

## 14. Test online with two copies on two virtual screens
```sh
Xvfb :77 & Xvfb :78 &
DISPLAY=:77 KKE_MAIN_MENU=0 KKE_NET=host KKE_DEMO_ONLINE=1 ./kke_demo > host.log 2>&1 &
sleep 10; DISPLAY=:78 KKE_MAIN_MENU=0 KKE_NET=join:127.0.0.1 KKE_DEMO_ONLINE=1 ./kke_demo > guest.log 2>&1
grep online host.log guest.log
```
Each side logs what it sees (net ids and positions), so one run checks both.

## 15. The host owns the body; the guest's copy is late
What happened: the guest lifted a crate and dropped it a step later.
Cause: the guest's copy follows the host's snapshots, so it trails the
guest's hands by more than the 1 m "snagged" limit. Fix: the guest only lets
go past 3 m, a bit more than the host's own 2.5 m, so the host decides.
Rule: any distance check on a replicated body needs slack for lag.

## 16. Walk to the thing, don't walk "forward"
The guest's start spot differs run to run, so "walk forward 1 s" missed the
crate. The demo walks toward the crate until it is 1.3 m away.
