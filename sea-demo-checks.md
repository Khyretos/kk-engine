# Sea demo (ships, battle, fort, online): checks for soucouyant

Patch: `/mnt/project-files/kk-engine/patches/sea-demo.patch` (one commit, `git format-patch`)
Base: Forgejo `main` at `93499963c87767a15dc7a8086da8270175a2ef4d` (`git apply --check` clean on it)
PR branch name: `feature/sea-demo-ships`
PR title: `Sea demo: pirate ships, cannon battles, a FEMFX fort and sailing online`

## Already checked in the cloud

- `sea_demo` and `kke_tests` build with GCC, zero warnings, `KKE_ENABLE_FEMFX=ON`.
- clang++ 18 `-fsyntax-only -Wall -Wextra -Werror` clean on every changed .cpp.
- `tools/ci/check_std_includes.py` passes.
- `kke_tests --gtest_filter='*Floating*:*Ocean*'`: all pass, incl. 3 new tests
  (`LongHullsSailSideBySideWithoutBouncing`, `OverlappingHullsArePushedApart`, `RemovedSlotsAreReused`).
- Headless (lavapipe) runs with the Pirate pack: no errors; the only warning was the
  missing `clear_day.hdr` sky (the cloud has no skies; soucouyant does).
- Online on 127.0.0.1: host and joiner each logged `<name> sails in on a <ship>` for the other.
- FEMFX fort: idle 0.1 ms a step; six balls into the wall peaked at 8-12 ms (4-core cloud), back to 0.1 ms in 10 s.
- Not run in the cloud: real GPU, Vulkan validation, the full test suite, Android.

## 1. Apply and build

```sh
cd /media/development/Software/kk-engine
git fetch FORGEJO main && git checkout -B feature/sea-demo-ships FORGEJO/main
git am /path/to/sea-demo.patch
tools/runner/kkrun build        # expect: PASS build (zero warnings)
tools/runner/kkrun tests        # expect: PASS tests
```

The ships need `POLYGON_Pirate_Pack` in `assets/synty/` (or `KKE_ASSETS_DIR`).
Without it the ships are boxes; run step 2 once each way if you can.

## 2. Headless runs

```sh
KKE_SEA_START=1 KKE_SEA_ENEMIES=3 tools/check_game sea_demo --seconds 30 --headless --log build/sea-battle.log
KKE_SEA_START=1 KKE_SEA_MODE=1 KKE_SEA_ENEMIES=4 tools/check_game sea_demo --seconds 20 --headless
KKE_SEA_START=1 KKE_SEA_MODE=2 KKE_SEA_SHIP=0 tools/check_game sea_demo --seconds 15 --headless
```

Expect: last line `OK`, **no warnings**. In `build/sea-battle.log`:
`Ships from POLYGON Pirate Pack (5 of 5 classes)`, a `pack assets used:` line,
`fort: 5 FEMFX wall panels` (FEMFX builds only).

## 3. Screenshots (desktop, real GPU; look at each)

```sh
KKE_SEA_START=1 KKE_SEA_ENEMIES=3 tools/check_game sea_demo --seconds 25 --keys "w w" --shot build/sea-sailing.jpg
KKE_SEA_START=1 KKE_SEA_SHIP=4 KKE_SEA_ENEMIES=2 tools/check_game sea_demo --seconds 20 --shot build/sea-warship.jpg
KKE_SEA_START=1 KKE_SEA_SHIP=0 KKE_SEA_MODE=2 tools/check_game sea_demo --seconds 15 --shot build/sea-rowing.jpg
tools/check_game sea_demo --seconds 12 --shot build/sea-lobby.jpg   # the start menu
```

Look for:
- The ship sits in the water at its waterline (not floating above it, not sunk to the deck).
- Masts, sails, rigging and the flag sit on the hull (not offset or tiny/huge).
- Wake foam along the sides and spray at the bow while moving.
- The sea reaches the horizon with no flicker or seam; islands in the distance.
- Lobby: ship choice, Mode, Enemy ship, CPU count and Online rows readable.

## 4. Play it (needs hands; 5 minutes)

`./build/bin/sea_demo`, then Battle with 2 enemies. Check:
1. W/S step the sails (furled, half, full); speed builds slowly. A/D steer.
2. Hold right mouse (or LT): a dotted arc and a landing ring appear on the side the camera faces. Look up: the ring goes further.
3. Fire (left click / RT): a rolling broadside, white smoke, splashes. A hit dents the hull, throws splinters, can snap a mast (it falls and floats) and sinks the ship.
4. Tab (or Y): the next ship class; each feels different (rowing boat darts, man-o'-war turns slowly).
5. Sail to the fort island and fire at the palisade (FEMFX builds): it splinters. The panel's fort row shows the FEMFX ms; **note the peak** for Kees (expected well under 10 ms here).
6. Pause menu (Esc): "Change ship" and "Restart" work; Settings has a "Sea" page.
7. A second controller: press A in the start menu, split screen with two ships.

## 5. Online (two windows on one PC)

```sh
cd build/bin
KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_SEA_START=1 KKE_NET=host KKE_NET_NAME=Host KKE_SEA_ENEMIES=2 ./sea_demo > /tmp/sea-host.log 2>&1 &
sleep 5
KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_SEA_START=1 KKE_NET=join:127.0.0.1 KKE_NET_NAME=Client KKE_SEA_SHIP=4 ./sea_demo > /tmp/sea-client.log 2>&1 &
sleep 40; pkill -x sea_demo
grep -E "sails in|error|warning" /tmp/sea-host.log /tmp/sea-client.log
```

Expect: host log has `Client sails in on a Man-o'-war`; client log has
`Host sails in on a Brig` and the two enemy ships; no errors, no warnings.
If you play it: each window shows the other's ship moving,
shots from one appear on the other, and a hit from the client damages the
host's enemies.

## 6. Open the PR

Branch `feature/sea-demo-ships`, title above. Body: what changed (from the
commit message), the FEMFX peak you measured in step 4.5, and the screenshots
from step 3.
