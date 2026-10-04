# Lesson: sea demo, pirate ships, cannon battles, FEMFX fort, online (kk-engine)

Roles: worker (C++ game code), runner (checks list), shared (FEMFX, NetModule, Synty).
Repo: kk-engine. Files: `games/sea_demo/*` (new: Ships, Battle, Effects, World, SeaNet),
`engine/src/FloatingBodies.cpp`, `engine/src/OceanRenderer.cpp`, `shaders/ocean.vert`,
`tests/test_floating_bodies.cpp`.

## What was asked
Kees: better looks, more particles, Synty pirate ships with different handling, change ship,
a small sea battle (enemies that fight or lie still), Black Flag style arcing cannon fire,
FEMFX wood breaking (he picked: ships break the cheap way, plus a FEMFX fort), sailing online.

## What went wrong and what fixed it

1. **Synty Pirate parts loaded at different sizes.** The loader's `fixUnitMismatch` scales a
   file that looks tiny (rigging) but not its hull. Fix: load every part of one ship with
   `opts.fixUnitMismatch = false`, measure the hull, and scale the whole set by 100 when the raw
   hull length is under 2 (those files are in centimetres). Check sizes first with
   `build/bin/kke_model_info <file.fbx>`, never guess.
2. **Long ships bounced off each other.** `FloatingBodies` used one sphere per body, radius =
   half the length, so two 30 m hulls side by side "touched" 12 m apart. Fix: a capsule along the
   longest box axis. Write a test for it first (`LongHullsSailSideBySideWithoutBouncing`).
3. **Body list grew forever.** `remove()` never reused slots. Fix: `add()` reuses dead slots;
   test `RemovedSlotsAreReused`.
4. **Far horizon flickered.** Fix: a graded ocean grid (fine near, coarse far) and fade wave
   height where a cell is bigger than half a wavelength (`shaders/ocean.vert`).
5. **The FEMFX fort fell apart by itself** (5 pieces -> 137 in 3 s, 19 ms a step, no ball
   fired). Cause: five panels placed on an arc overlapped at spawn; FEMFX pushed them apart hard
   enough to break them. Fix: a straight wall, panels 0.2 m apart. Rule: **FEMFX objects must
   never overlap when spawned.** Found by logging `panelStats()` once a second (pieces, awake
   pieces, ms), not by guessing.
6. **Breaking cost too much** (135 moving splinters, 17 ms). Fix: bigger chunks (0.6 m) and
   `setDebrisBudget(60)`. Result: idle 0.1 ms, six balls 8-12 ms peak on 4 cloud cores.
7. **Online: the other screen never saw the host's own ship.** Cause: `NetModule::playerCharacter`
   is sent once, when hosting/joining starts (KKE_NET does it in the first frame); the game set it
   later. Fix: set `m_net->playerCharacter = "ship:p"` in the module's init. Never overwrite
   `playerName` (it carries `KKE_NET_NAME`).
8. **A cannonball tunnels through a 0.3 m FEMFX plank** at 80 m/s (1.3 m per 60 Hz step). Fix:
   hand FEMFX a slower ball (30 m/s) with density raised to keep the momentum.
9. **clang caught an unused `constexpr` that GCC did not.** Always run the clang check below.

## Exact commands (all run in the cloud container)

```sh
cd /home/user/kk-engine
ninja -C build sea_demo kke_tests 2>&1 | grep -E "warning:|error:"     # must print nothing
build/bin/kke_tests --gtest_filter='*Floating*:*Ocean*'
python3 tools/ci/check_std_includes.py                                  # "Every std:: name used..."
# clang: from build/compile_commands.json replace the compiler with clang++, drop -o X and -c,
# add -fsyntax-only -Wall -Wextra -Werror, run for each changed .cpp (a 20-line python loop).
KKE_ASSETS_DIR=/mnt/project-files/kk-engine/assets-cache KKE_SEA_START=1 KKE_SEA_ENEMIES=3 \
  tools/check_game sea_demo --seconds 30 --shot /tmp/x/sea.jpg           # look at the picture
```

Online test on one machine (two games, one Xvfb):
```sh
cd build/bin; Xvfb :97 -screen 0 1280x720x24 & export DISPLAY=:97 SDL_VIDEODRIVER=x11 \
  KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_SEA_START=1
KKE_NET=host KKE_SEA_ENEMIES=2 timeout 50 ./sea_demo > host.log 2>&1 &
sleep 10; KKE_NET=join:127.0.0.1 KKE_SEA_SHIP=4 timeout 35 ./sea_demo > client.log 2>&1
grep "sails in" host.log client.log    # each side must name the other's ship
```
`check_game` hides info lines; to see your own debug values log them as `warn` for one run,
then delete them (`grep -n DBG games/sea_demo/*.cpp` must print nothing before committing).

## Patch for soucouyant (no push token in the cloud)

```sh
git fetch forgejo main
git stash push -u && git checkout -B feature/sea-demo-ships forgejo/main && git stash pop
# fix conflicts (here: one table row in docs/demos/index.md), then: git reset; git stash drop
git add -A games/sea_demo engine tests shaders docs && git commit -F msg.txt
git format-patch -1 --stdout > /mnt/project-files/kk-engine/patches/sea-demo.patch
git checkout -q forgejo/main && git apply --check /mnt/project-files/kk-engine/patches/sea-demo.patch
```
Write a checks list next to it: what was verified, then exact commands with what to expect.

## Rules worth keeping
- One `FloatingBody` per ship gives buoyancy and rolling for free; add sail thrust, keel force
  and a yaw-rate rudder on top. Physics in `fixedUpdate`, drawing interpolated with `ctx.alpha`.
- Cannonballs without drag: the predicted arc and the real path are the same integration.
- Cheap ship damage (dents via `setDeformedVertices`, splinters as floating planks) costs
  nothing; FEMFX costs ~0.15-0.2 ms per moving piece, so give it a debris budget.
- A dev switch to skip menus (`KKE_SEA_START=1`) makes every headless check possible.

## Could a 9B model do this alone?
Not the whole job: ~3400 new lines across 7 files, plus physics, networking and FEMFX debugging.
It could do, one at a time: add a ship row to `kClasses` (copy a row, change names and
numbers), tune a number and rerun check_game, write the README sections, run the commands
above and the checks list, and fix a reported compile error. It needs: the file and function
name, the exact command to run after, and the rule list above. Debugging cases 5 and 7 needs a
bigger model or a human: the cause was not in the file where the symptom showed.
