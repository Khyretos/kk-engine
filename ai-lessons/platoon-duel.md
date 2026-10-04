# Lesson: platoon camera and cover, duel combos and stance (2026-10-04)

Role: worker (C++ in `games/platoon`, `games/duel`, a little engine AI). Shared notes at the end.
Could a 9B local model do this alone? Partly: the README edits and the duel combo table, yes. The
cover bug needed reading three engine files and logging per-soldier state; give it lesson 1 and 2 verbatim.

## The task

Platoon: camera like the procedural demo (XCOM on a pad), slow decisions, take cover by clicking next to
an obstacle or from a ring menu, crash on quit. Duel: awkward poses, every attack a jab, no menu, crash on quit.

## Lessons

1. **"Slow" was two bugs, found by logging, not by guessing.** A temporary log line per soldier (time, position,
   spot, speed, `AiWorld::describe(id)`) showed it:
   - `AiOrderBridge` turned a Stay order into `Hold` with `run = false`, so soldiers walked (1.6 m/s) to cover.
     Fix: `stayRunBeyond` (bridge field), platoon sets 1.5 m.
   - One obstacle circle round a 3.2 m barrier had radius 1.7 m; the cover spot was 1.37 m from its centre,
     inside the circle. `avoidObstacles` pushed soldiers off their own spot; they circled it.
     Fix: a row of small circles along the barrier, and in `AiWorld::move` skip an obstacle that the goal
     hugs when the agent is on the goal's side of it.
   Delete the debug log line before committing (`grep -n DBG`).
2. **Measure with the demo mode.** `KKE_PLATOON_DEMO=1 KKE_PLATOON_QUIT=30 ./platoon | grep demo:` prints
   "6 of 6 behind cover (N s)". Make the demo step wait for the condition and log the time, not a fixed 9 s.
3. **One mouse button, three jobs.** Right click = order, right hold = wheel, right drag = turn the camera.
   Done in `CommandInput` from raw SDL events (`rightButtonGestures`, opt-in so pet_companion is unchanged).
   The bound `cmd.context` press would fire too, so it is ignored while the gesture owns the button.
   Remove a binding by index from the back: `bindingsFor()` returns indices, `removeBinding(i)` shifts later ones.
4. **Smooth cameras: move goals, not the camera.** Every input changes `m_*Goal`; each frame
   `value += (goal - value) * (1 - exp(-12 * dt))`.
5. **Assign shortest pair first.** Greedy "each soldier takes its nearest spot" in selection order sends soldiers
   across each other. Repeatedly settle the globally closest (soldier, spot) pair instead.
6. **"Every attack is a jab": read the clip list first.** `./build/bin/kke_model_info FILE.fbx | grep clip`.
   The old code mapped the left jab to `Melee_Hook` and fell back to `Punch_Cross` for uppercut and knee when UAL2
   was missing, so everything looked alike. Each strike now has its own `AttackDesc` (`attackNamed`) and clip.
   The full `UAL1.fbx` (pack "Universal Animation Library") has `Kick`, `Dodge_Left/Right`, `Hit_Stomach`;
   load extra clips with `kke::appendClipsByBoneName`, skipping names the rig already has.
7. **A stance without a stance clip.** On top of the idle/walk pose: lower `pelvis` (convert the model-space
   offset with the inverse of the parent's model matrix), turn spine bones about model axes with
   `r = r * (inverse(boneWorld) * delta * boneWorld)`, then `kke::CharacterIk` (hands to the face as human arms,
   feet on the ground so the knees bend, lean). Sign check: to lean FORWARD about the right axis
   `right = cross(fwd, up)`, the angle is negative (use `-right`). Screenshot before and after.
8. **Free resources in `shutdown()`, not the destructor.** Modules are destroyed in add order after all
   `shutdown()`s, so a game's destructor runs after physics and UI are gone. Destroy ragdolls, close RmlUi
   documents and reset GPU meshes in `shutdown()`.
9. **`sed`/Python bulk replace bit me.** Replacing `", true);\n"` to drop one argument also changed four unrelated
   calls. Always `git diff | grep '^[-+]'` after a bulk replace.
10. **ASan under lavapipe is too slow to be useful** (duel never reached 25 s game time in 400 s). Leave
    AddressSanitizer runs to soucouyant with a real GPU.

## Verified commands (cloud container, Ubuntu 24.04, 4 cores)

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DKKE_WARNINGS_AS_ERRORS=ON -DKKE_FETCH_SKIES=OFF \
  -DFETCHCONTENT_SOURCE_DIR_LUA=... -DFETCHCONTENT_SOURCE_DIR_SQLITE=... -DFETCHCONTENT_SOURCE_DIR_WAYLAND_SCANNER_SRC=...
ninja -C build duel platoon kke_tests          # first build about 25 min on 4 cores
Xvfb :99 -screen 0 1280x720x24 &
export DISPLAY=:99 KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 KKE_ASSETS_DIR=/mnt/project-files/kk-engine/assets-cache
KKE_DUEL_BOTS=1 python3 tools/check_game duel --seconds 9 --shot /tmp/duel.png
build/bin/kke_tests --gtest_filter='OrderBridge.*'
python3 tools/ci/check_std_includes.py
```
Clang check for Android-only warnings: take the file's line from `ninja -C build -t commands`, swap `c++` for
`clang++`, `-o X` for `-fsyntax-only`, drop `-MD -MT -MF` and `-mfpmath=sse`, add `-Werror -Wunused-const-variable`.

## Shared

- Kees's words were "decision making is slow": check movement speed and arrival before touching the AI's thinking
  rate (`thinkInterval` was already 0.25 s and orders re-think at once).
- A demo README is the spec for the next model: update its controls table and "how it works" with the code.
