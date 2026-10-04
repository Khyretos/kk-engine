# Sandbox feedback round: checks for soucouyant

| | |
|---|---|
| Patch | `sandbox.patch` (one commit, made on Forgejo main `fa1b7f1`) |
| Branch | `sandbox/feedback-toys-walk` |
| PR title | Sandbox: Walk mode, gun, fire, melt, hit anything anywhere, menus |
| Engine files changed | **`engine/src/modules/GameShellModule.cpp` only**: a new `showDocument()` helper that also sets `pointer-events: none` on hidden shell documents, so a hidden menu no longer catches clicks meant for game UI. All other changes are under `games/sandbox/`, `docs/PLAY_TO_MAKE.md` and `tests/sandbox_replays/`. |
| Extra files | `sandbox-shots/` (cloud screenshots), `sandbox-toys.scene.json` (2 people, a sheep, crate, barrel, cone), `sandbox-wmclose.c` (closes a window the way Super+Q does) |

## What was already checked in the cloud

| Check | Result |
|---|---|
| `cmake --build build --target sandbox` (GCC, `-Wall -Wextra -Werror`) | builds, 0 warnings |
| clang++ `-fsyntax-only -Werror -Wall -Wextra` on the 7 changed .cpp files | 0 warnings |
| `python3 tools/ci/check_std_includes.py` | exit 0 |
| `kke_tests` | 991 passed, 23 skipped (postgres and valkey servers not running), 0 failed |
| `tools/check_game sandbox --headless` (with the toys scene) | OK, 8 s with no errors; 1 warning, the sky image below |
| Bat, Gun, Fire, Melt, sheep, Walk mode, pick-up, pause and title menus, played with xdotool on Xvfb (software Vulkan, about 8 fps) | all work, see the screenshots |
| Closing the window (WM_DELETE_WINDOW) after playing | exit 0, no VMA assert |
| SIGINT at 3 s and after play | exit 0 |
| The 3 replays with `KKE_MAIN_MENU=0` | no script errors; `gamepad_bat` didn't land its hit at 8 fps (its timing depends on frame rate), please check it at full speed |

Known and not from this patch: the log warns `can't read sky image assets/skies/clear_day.hdr`, and City Characters stand in a T-pose.

## Steps on soucouyant

1. On a branch off Forgejo main: `git am sandbox.patch`.
2. Build, capping the jobs: `cmake --build build --target sandbox kke_tests -j8`. It must print no warnings.
3. `python3 tools/ci/check_std_includes.py` should exit 0, and `build/bin/kke_tests` should report 0 failed.
4. Run `tools/check_game sandbox`; it should print OK.
5. Run the replays from `build/bin`, one at a time:
   `KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_SANDBOX_REPLAY=../../tests/sandbox_replays/<name>.replay ./sandbox`
   - `gamepad_bat`: the person falls over.
   - `gamepad_build`: Start goes from Build to Play, then Start opens the pause menu and closes it again.
   - `touch_gestures`: the view turns and zooms.
6. Close test: start `./sandbox`, play a little, then press Super+Q. It must exit with code 0, and the log must have no `VMA` or `assert`.
7. Take screenshots, replacing the cloud ones in `sandbox-shots/`. Load the scene with
   `KKE_SANDBOX_LAYOUT=<path>/sandbox-toys.scene.json KKE_MAIN_MENU=0 KKE_SKIP_INTRO=1 ./sandbox`, then take:
   - the title screen (run without `KKE_MAIN_MENU=0`);
   - the crate after Fire;
   - the barrel after Melt;
   - Walk mode holding the bat;
   - Walk mode firing the shotgun;
   - the pause menu.
8. Open the Forgejo PR with the title above.

## Play list (by hand, with a mouse and a pad)

Start from the title screen.

1. **Play, then Fly around.**
   - Drag out a person and a sheep. Take the Bat and click the person's head: they fall from the head.
   - Click the sheep: it tumbles, then gets up after about 5 s.
   - Click the cone: it rolls.
   - Click the barrel: it shatters (FEMFX build).
2. **Gun ("Bang!").** Click the crate: it gets pushed, with wood chips. Click a person: they fall.
3. **Fire ("Burn").** Hold on the crate: it catches fire, chars, spreads to wood nearby, and slumps into a heap after about 14 s. Set a person alight: they burn briefly and drop.
4. **Melt ("Hot!").** Hold on the barrel: it glows red and sags into a puddle. Animals nearby run.
5. **Esc, Start or Select** opens the pause menu, and the world freezes.
   - Walk around, Fly around, Build mode, Back to Play, Everyone up and Clear everything each do what they say and close the menu.
   - Settings has a Sandbox section with Props break, Props roll and Who you are. Controls lists the Sandbox actions.
6. **Walk around.**
   - Click to take the mouse. WASD moves, Space jumps, C crouches, V switches to first person.
   - Key 3 drops the bat at your feet; E (pad Y) picks it up. Left click (RT) swings at the dot.
   - Pick up the gun, the torch and the melt tool the same way and use them.
   - Tab frees the mouse for the palette. R (D-pad up) gets everyone up.
   - On a pad: LB/RB choose a picture and X uses it.
7. **Quit** from the menu, and also with Super+Q. Both should close cleanly.
8. Not checked yet: a real GPU, a real pad, touch on a phone, and the touch buttons in Walk mode (fire, jump, interact, crouch, camera, get up).
