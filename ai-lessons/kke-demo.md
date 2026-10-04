# Lesson: kke_demo round 1, spawn menu, carrying, grid inventory (kk-engine)

Roles: worker (C++ game code, RmlUi), runner (headless checks), shared (input, game shell).
Files: `games/showcase/{Spawner,Carry,Geometry}.cpp`, `games/showcase/ui/showcase_spawn.rml`,
`engine/src/Inventory.cpp`, `engine/include/kke/Inventory.h`, `tests/test_inventory.cpp`,
`engine/src/modules/GameShellModule.cpp`.

## What was asked
Kees: kke_demo becomes a vast showcase world (he chose one open world around the yard).
Round 1: a spawn menu with clear/reset, picking up boxes, an inventory core.

## What went wrong and what fixed it

1. **Clicking the view never grabbed the mouse.** Found by logging in `onEvent`: `uiCapturesMouse()`
   was true everywhere. Logging `context->GetHoverElement()` showed `body` of doc `game_shell`.
   RmlUi hovers a body with `visibility: hidden`. Fix: also
   `doc->SetProperty("pointer-events", "none")` when hidden, `"auto"` when shown.
   Rule: **a hidden RmlUi document must also get pointer-events none.**
2. **Pick-up worked by hand but not in the scripted run.** The held body was dropped on the
   first physics step: the "snagged, let go" distance (1.0 m) was smaller than the distance from
   the floor to the hands. Fix: a grace time while lifting (`Held::age < 0.6 s` allows reach + 0.5 m).
   Found by logging the body's position at fixed times in a scripted run (`KKE_DEMO_CARRY=1`).
3. **`kNoBody` is not 0.** `RigidWorld::kNoBody == 0xffffffff`. Never test a BodyId with `if (id)`.
4. **Throw went backwards.** "Where the camera looks" is behind you when the camera is in front of
   the face. Fix: if the camera's flat direction points against the facing, throw along the facing.
5. **Name lookups for data files:** `equipSlotName()` returns "right hand" (display text), not a key.
   Data files need their own fixed keys ("RightHand"); a test that loads the JSON caught it.
6. **xdotool clicks are too short at 11 fps** (lavapipe): `click 1` can be missed by `pressed()`.
   Use `mousedown 1; sleep 0.3; mouseup 1`. Better: add a `KKE_DEMO_*` script that drives the feature
   and logs results; screenshots then show a known moment.

## How the pieces work (copy these patterns)
- **In-game menu over a running game:** an RmlUi doc with a data model (`RegisterStruct`,
  `RegisterArray`, `BindEventCallback("pick", ...)`), rows with
  `data-for="row, i : rows" data-class-sel="i == sel" data-event-click="pick(i)"`.
  Its own input context (`"spawnlist"`) is enabled only while open; the `"game"` context is
  disabled meanwhile so A does not also jump. The open/close action lives in a third context
  that is off while the pause menu is open.
- **Carrying a body:** keep it dynamic; at the physics rate set its velocity to
  `(target - pos) * 14` (max 9 m/s) plus `g * dt` upward to cancel gravity, and an angular
  velocity toward the wanted yaw. Walls stop it; if it gets 1 m off target it drops.
- **Hands on it:** `CharacterIk::hand(side, palm, elbowHint)` each frame before `apply()`.
- **Batch many props in one mesh** rebuilt each frame (`DynamicMeshRenderer::upload`), one for
  plain and one for metal: two draws for 300 props.
- **Ragdoll dummy:** `buildHumanoidRagdoll` + `bindSkeletonToRagdoll` + `RigidWorld::addRagdoll`;
  each frame `ragdollTransforms` -> `poseFromRagdoll` -> `setBoneWorldOverride`.

## Tested commands (cloud: Ubuntu, 4 cores; see kk-engine-sandbox-build for the deps)
```sh
cmake --build /home/user/build --target kke_demo kke_tests
/home/user/build/bin/kke_tests --gtest_filter='Inventory*'      # 8 passed
python3 tools/ci/check_std_includes.py                           # must print "Every std:: name ..."
# clang check of one file, using GCC's command from ninja:
cd /home/user/build && ninja -t commands | grep -m1 -- "-c /home/user/kk-engine/games/showcase/Carry.cpp" \
  | sed -e 's|^/usr/bin/c++|clang++|' -e 's| -o [^ ]*| -fsyntax-only|' -e 's| -MD -MT [^ ]* -MF [^ ]*||' \
        -e 's|-mfpmath=sse||' | sh
cd /home/user/build/bin && KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_DEMO_CARRY=1 ./kke_demo   # log: "carry demo: holding ..."
```
Headless driver with keys and a screenshot: `/mnt/project-files/kk-engine/patches/kke-demo-shots/drive.sh "<keys>" out.png <secs> [ENV=..]`
(starts Xvfb :77, focuses the window, keys held 0.35 s, `waitN` sleeps, `click` = held click).

## Patch workflow (cloud cannot push to Forgejo)
`git fetch forgejo main && git merge forgejo/main`, build, test, one commit, then
`git format-patch -1 --stdout > /mnt/project-files/kk-engine/patches/kke-demo.patch`, check it with
`git apply --check` in a worktree of `forgejo/main`, and also on top of the other queued patches
(`git am -3` each into a scratch worktree) to list conflicts in the checks file.

## Could a 9B local model do this alone?
- The inventory core + tests: yes, given `Inventory.h` as the spec and the test file as an example.
- Spawn menu: yes with this lesson's RmlUi pattern and `Spawner.cpp` to copy from.
- Carrying feel and the hidden-doc mouse bug: no, not alone. It needs the log-first habit
  (log positions at fixed times, log the hovered element) and a reviewer looking at screenshots.
  Give it smaller steps: "add a log line that prints X at time T, run, read it, then change one thing".

## Round 2: the bag (grid inventory screen) and equipment
- **Absolute tiles over flowing cells.** Cells `float: left` (42dp + 1dp border + 1dp margin = 46dp
  pitch); items are `position: absolute` divs with `left/top/width/height` in dp built in C++ and
  bound with `data-attr-style`. Tiles get `pointer-events: none` so hover and clicks reach the cell.
  dp is about 0.82 px at 720p and RmlUi snaps each cell to whole pixels: a 460dp row fitted 9 cells, not
  10. Leave ~2% slack (470dp) and check a screenshot.
- **One button, two meanings:** the bag uses two contexts, `invclosed` (open) and `invlist` (keys
  inside). A button that opens must not also be a key inside (d-pad right was both). Select/View opened
  the pause menu: GameShellModule now has `selectIsTheGames` (like `startIsTheGames`).
- **Items as Jolt `debris` bodies:** they land on the level but the character walks through them;
  before that, standing on a canteen read as "In the air".
- **Equipment:** build `kke::Equipment` from the full model (meshes + bones), not the bones-only rig.
  Draw items with `itemTransform(slot, poseToModel(rig, pose))` after the IK, then `closeHand`.
- **Scripted runs beat guessing:** `KKE_DEMO_ITEMS=2` keeps the bag open so `drive.sh` can send
  `m:X,Y` / `r:X,Y` clicks; each place/drop logs a "bag:" line. A scripted walk turns the character
  to the move direction, so set the facing before each pickup.
- **Slow asset scans:** `KKE_ASSETS_DIR` on the shared folder takes minutes to scan at start. Leave it
  unset for quick runs.
