# Goblin horde rework: checks to run on soucouyant

Patch: `goblin-horde.patch`, one commit on top of Forgejo `main`
9349996 ("Merge pull request 'Benchmarks: run on Forgejo ...' (#2)").
Apply with `git am goblin-horde.patch` (`git apply --check` passed on that base).

- Branch: `goblin-horde-rework`
- PR title: `Goblin horde: five goblin types, giants, weapons, split screen and online`

**If `platoon-duel.patch` lands first:** use `git am -3 goblin-horde.patch`.
`engine/src/KnownPacks.cpp` merges by itself; `docs/ASSETS.md` conflicts on two
neighbouring table rows. Keep both rows: platoon-duel's `duel` row and this
patch's `goblin_horde` row.

It changes 31 files: all of `games/goblin_horde/` (Hero.cpp and Goblins.cpp
replaced by Roster, Art, Puppets, Heroes, Foes, Shots, Hud, Net), plus
`engine/src/AnimRig.cpp` (older Synty bone names), `engine/src/Ragdoll.cpp`
(finds bones by those names too), `engine/src/KnownPacks.cpp` (War Camp,
Dungeon, Fantasy Rivals for goblin_horde), `tests/test_anim_rig.cpp`,
`tests/test_ragdoll.cpp`, `docs/ASSETS.md`, `docs/SCENES.md`,
`docs/PLAYTEST_CHECKLIST.md`.

Set `KKE_ASSETS_DIR` to the Synty folder first. Packs it uses:
POLYGON_Goblin_War_Camp, POLYGON_Dungeon_Pack, POLYGON_Fantasy_Rivals,
POLYGON_Fantasy_Characters, Universal Animation Library (full UAL1.fbx),
Universal Animation Library 2.

## Already checked in the cloud (lavapipe, 4 cores, GCC 13 Debug, -Werror)

- `goblin_horde` and `kke_tests` built with zero warnings.
- Full `kke_tests`: 1015 tests, 992 passed, 23 skipped (servers not set), 0 failed.
  New: `Ragdoll.OlderSyntyBoneNamesStillMakeARagdoll`, more asserts in
  `AnimRig.CanonicalNamesPairUalWithSynty`.
- `clang++-18 -fsyntax-only -Werror -Wall -Wextra` on every changed .cpp: clean.
- `tools/ci/check_std_includes.py`: clean. `tools/ci/check_dependencies.py`: clean.
- Bot runs (sword, bow, crossbow, great axe, `KKE_HORDE_BOSS=1`, `KKE_HORDE_LINEUP=1`):
  no warnings except the missing sky image (skies aren't fetched here), exit 0
  after `KKE_HORDE_QUIT`. Sword bot clears wave 1 and kills 17 of wave 2.
- Online on one machine: host (sword bot) + client (bow bot, then crossbow bot)
  with `KKE_NET=host` / `KKE_NET=join:127.0.0.1`: client connected, the client's
  shots killed goblins on the host (host counted the kills), both exit 0.
- Screenshots: `goblin-horde-shots/` next to this file.
- **Not run here:** split screen with two real pads, the GPU (RADV), an
  AddressSanitizer quit check, the flight sticks.

## 1. Build and tests

```
tools/runner/kkrun build        # last line PASS, 0 warnings
tools/runner/kkrun tests        # PASS
```

## 2. Headless runs

```
KKE_SKIP_INTRO=1 KKE_HORDE_BOT=1 KKE_HORDE_QUIT=120 tools/check_game goblin_horde --seconds 125 --log horde-bot.log
KKE_SKIP_INTRO=1 KKE_HORDE_BOT=1 KKE_HORDE_BOSS=1 tools/check_game goblin_horde --seconds 25 --shot horde-boss.jpg
KKE_SKIP_INTRO=1 KKE_HORDE_LINEUP=1 tools/check_game goblin_horde --seconds 22 --shot horde-lineup.jpg
KKE_SKIP_INTRO=1 KKE_HORDE_BOT=1 KKE_HORDE_WEAPON=bow tools/check_game goblin_horde --seconds 18 --shot horde-bow.jpg
KKE_SKIP_INTRO=1 KKE_HORDE_BOT=1 KKE_HORDE_WEAPON=crossbow tools/check_game goblin_horde --seconds 18 --shot horde-crossbow.jpg
```

| Check | Must show |
|---|---|
| horde-bot.log | "5 goblin types (19 looks), 4 bosses, 11 characters, 4 weapons"; the 10 s reports reach wave 2; no warning but the sky (and none at all where skies are fetched); exit 0 |
| horde-lineup.jpg | five goblins in a row, each different, none in a T-pose, holding weapons; four giants behind them |
| horde-boss.jpg | a giant with "The ..." boss bar at the top |
| horde-bow.jpg | the bow upright while drawing, crosshair in the middle |
| horde-crossbow.jpg | a short bow laid flat on a wooden stock |
| quit | `KKE_HORDE_QUIT=20 ./goblin_horde` exits 0 within a few seconds of 20 s |

## 3. Online on soucouyant

```
cd build/bin
KKE_SKIP_INTRO=1 KKE_HORDE_BOT=1 KKE_NET=host KKE_HORDE_QUIT=60 ./goblin_horde &
sleep 8; KKE_SKIP_INTRO=1 KKE_HORDE_BOT=1 KKE_NET=join:127.0.0.1 KKE_HORDE_WEAPON=bow KKE_HORDE_QUIT=45 ./goblin_horde
```

Host log: "Player 1 joined", "... joined the fight", kills rise. Client log:
"Connected as player 1", "shots" and "hits by players" rise. Both exit 0.

## 4. By hand (Kees, or the session with the desktop)

- Title, then the lobby: press a key or pad button to take a seat; pick name,
  character, weapon. A second pad takes a second seat: split screen, each with
  their own HUD and crosshair.
- Sword: J J J = three different swings; F = heavy; hold F = charge then spin.
- Tab (pad: d-pad up) opens the inventory in your part of the screen; choose
  the bow; hold left mouse to draw (charge bar fills), let go; headshots hit harder.
- Crossbow: one bolt, then a slow reload; damage the same however long you aim.
- Wave 3+: archers back off and shoot; shamans throw fire and mark circles;
  war shamans heal and buff (goblins tinted); brutes slam.
- Wave 5: a giant; each of its attacks is marked on the ground first.
- Esc pauses (resume, inventory, start over, settings, quit). Super+Q closes it.
- Online + split screen at once: host with two local seats, join from the other PC.
