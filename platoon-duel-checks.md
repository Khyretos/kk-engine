# Platoon and duel: checks to run on soucouyant

Patch: `platoon-duel.patch`, one commit on top of Forgejo `main`
93499963c87767a15dc7a8086da8270175a2ef4d. Apply with `git am platoon-duel.patch`
(`git apply --check` passed on that base).

It changes 15 files: `games/platoon/*`, `games/duel/*`, `games/command_kit/CommandInput.*`,
`engine/src/ai/AiWorld.cpp`, `engine/src/OrderBridge.cpp` + `engine/include/kke/OrderBridge.h`,
`engine/src/KnownPacks.cpp`, `docs/ASSETS.md`, `tests/test_orders.cpp`. Both demo READMEs describe the new controls.

Already checked in the cloud (lavapipe, 4 cores, GCC 13 Debug, `-Werror`):

- `duel`, `platoon`, `kke_tests` built with zero warnings after merging main.
- `kke_tests --gtest_filter='OrderBridge.*:Orders.*:Ai*.*:CharacterIk*.*:Combat*.*:KnownPacks*.*'`: 74 passed,
  including the new `OrderBridge.StayAtASpotInFrontOfAnObstacleRunsThereAndSettles`.
- `clang++ -fsyntax-only -Werror -Wunused-const-variable` on the 10 changed .cpp files: clean.
- `tools/ci/check_std_includes.py`: clean.
- `KKE_PLATOON_DEMO=1`: "6 of 6 behind cover (5.4 s)", area cleared, exit 0.
- `KKE_DUEL_BOTS=1 KKE_DUEL_QUIT=40`: exit 0; log shows jab, cross, hook and uppercut thrown.
- **Not run:** the full `kke_tests`, and an AddressSanitizer quit check (too slow under lavapipe here).

Set `KKE_ASSETS_DIR` to the Synty folder first (UAL2 and, for the kick, the full "Universal Animation Library" pack).

## 1. Build and tests

```
tools/runner/kkrun build        # last line PASS, 0 warnings
tools/runner/kkrun tests        # PASS
```

## 2. Duel

```
KKE_DUEL_BOTS=1 tools/check_game duel --seconds 9  --shot duel-stance.jpg
KKE_DUEL_BOTS=1 tools/check_game duel --seconds 14 --shot duel-fight.jpg
KKE_DUEL_BOTS=1 KKE_DUEL_QUIT=60 tools/check_game duel --seconds 65
```

| Check | Must show |
|---|---|
| duel-stance.jpg | both fighters with knees bent, leaning in, left shoulder forward, both fists at the face (not above the head, not at the chest) |
| duel-fight.jpg | a strike or a hit reaction; arms never through the body |
| the 60 s log | "threw:" lines with more than one strike name; no warnings except the missing sky image |
| quit | exit code 0 after KKE_DUEL_QUIT; also close the window with SUPER+Q mid-knockdown: no crash, no hang |

By hand (keyboard): J J J quickly = jab, cross, hook (three different arms/clips). L far away = front kick (with the full UAL1), close = knee.

## 3. Platoon

```
KKE_PLATOON_DEMO=1 tools/check_game platoon --seconds 150
KKE_PLATOON_DEMO=1 tools/check_game platoon --seconds 22 --shot platoon-cover.jpg
```

| Check | Must show |
|---|---|
| log | "demo: 6 of 6 behind cover (N s)" with N under 7; "area clear"; exit 0 |
| platoon-cover.jpg | soldiers crouched right behind crates and barriers, on the side away from the enemy, not circling |

By hand (mouse): right-drag turns and tilts the view; middle-drag moves it; a right click (no drag) still gives the order;
right button held still opens the order wheel; right click on the ground next to a crate = everyone selected takes cover
behind it; F centres on the selection; Settings > Camera has "Move the view at the window's edge".
Controller: left stick moves the view, right stick turns/zooms, L3 centres, RB next to a crate = take cover.
Quit from the pause menu: no crash.
