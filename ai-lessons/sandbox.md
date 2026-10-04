# Lesson: sandbox feedback round (Walk mode, gun, fire, melt, menus)

Task: Kees asked for these in the sandbox:
- hit anything at the exact spot you aim at;
- a gun, fire and melt;
- props that roll and animals that ragdoll;
- a third-person mode with a bat to pick up;
- a pause menu and a clean close.

Result: one patch (`patches/sandbox.patch`, branch `sandbox/feedback-toys-walk`). Every command below was run and worked.

## What was done
- `games/sandbox/Toys.cpp` (new):
  - `aimAt()` turns the mouse ray into a hit on a body part, a prop's box, a FEMFX breakable or the ground.
  - `strike()` pushes what was hit:
    - Jolt bodies for small props;
    - `buildQuadrupedRagdoll` for animals;
    - FEMFX for breakables.
  - Fire uses `ParticleLibrary` "fire" plus `ModelModule::setTint` to char.
  - Melt uses `setDeformedVertices`.
- Walk mode reuses `Walker` (Locomotion, CameraRig, the UAL1 mannequin, hand IK).
- Menus come from `GameShellModule`:
  - `addMode` for Walk and Fly;
  - `addPauseItem`; pause items do NOT close the menu, so call `closeMenu()`;
  - `settings("Sandbox")`.
  - While paused, only `frameStart` runs.

## Build (cloud, Linux)
```sh
cmake -S . -B build -DKKE_ENABLE_FEMFX=ON -DKKE_ENABLE_GPU_PROFILER=OFF
cmake --build build --target sandbox kke_tests -j6
python3 tools/ci/check_std_includes.py     # exit 0
build/bin/kke_tests                        # 0 failed (23 skipped: no postgres/valkey)
```
New shaders are only built when listed in the game's CMakeLists. The fire, smoke and chips effects need these shaders added to the `foreach(shader ...)` list:
- `effect.vert`
- `effect_smoke.frag`, `effect_spark.frag`, `effect_glow.frag`, `effect_flake.frag`, `effect_ring.frag`

A game that shows RmlUi documents needs `kke_use_ui(<target>)` in its CMakeLists. It copies the `rml_*` shaders and the Noto fonts.

## Check with clang too (CI uses clang -Werror on Android)
GCC was happy, but clang failed on `-Wmismatched-tags`: `class AssetCatalog;` was declared, but the real type is a `struct`. A forward declaration must use the same word as the definition.

Syntax-only clang check per file: take the command from `build/compile_commands.json`, drop `-o X` and `-c`, and add `-fsyntax-only -Werror -Wall -Wextra`.

## Run it headless and click things
```sh
Xvfb :99 -screen 0 1280x720x24 &
cd build/bin
DISPLAY=:99 XDG_RUNTIME_DIR=/tmp KKE_SKIP_INTRO=1 KKE_MAIN_MENU=0 \
  KKE_ASSETS_DIR=<folder of pack symlinks> KKE_SANDBOX_LAYOUT=<scene.json> ./sandbox &
xdotool mousemove 808 685; sleep 0.6; xdotool click 1     # a palette picture
import -window root shot.png                              # screenshot (ImageMagick)
```
- `KKE_MAIN_MENU=0` skips the title screen. Without it, clicks land on the menu.
- Software Vulkan runs at about 8 fps, so `sleep 0.6` after each `mousemove` before clicking. Otherwise the click lands before the hover updates.
- `tools/check_game sandbox --headless` printed `OK` (8 s, no errors).

## Test "Super+Q quits cleanly" without Hyprland
Super+Q sends WM_DELETE_WINDOW. A 20-line Xlib program does the same (`patches/sandbox-wmclose.c`):
```sh
gcc wmclose.c -lX11 -o wmclose
./wmclose $(xdotool search --name "Kreative Kompas Engine - Sandbox" | head -1)
wait $PID; echo $?        # must be 0, and the log has no VMA assert
```
Search for the window by name. `xdotool search --pid $!` found nothing, because `$!` was the pid of `timeout`, not of the game.

## The bug that cost the most time
After clicking the world, palette clicks did nothing. The cause: the shell's pause menu was hidden with `visibility: hidden`, but in RmlUi a hidden document still catches the mouse and is pulled to the front on click.

To find it, temporarily log the hover element in UiModule, then revert the log. The fix (in the engine, GameShellModule): hide documents with `pointer-events: none` too. Lesson: when UI clicks vanish, find which element is under the mouse before changing input code.

## Other pitfalls
- Hidden palette cells still got presses in Walk mode, where the mouse is captured. Ignore `onPress` while captured.
- Replays that push Start now open the pause menu. Update the replay files and run them with `KKE_MAIN_MENU=0`.
- Pick-up prompt: look for the NEAREST pickup, not the first in the list.

## Could a 9B local model do this alone?
No, not the whole thing. The patch is about 2,200 lines across 14 files and needs many engine APIs at once:
- Jolt, FEMFX, ragdolls, particles, RmlUi, GameShell and InputModule;
- plus debugging by screenshot.

A 9B could do single pieces with a clear brief, an example to copy and a test command:
- add a pause item;
- add a setting toggle;
- add a shader to CMakeLists;
- fix a `-Wmismatched-tags` warning;
- write the wmclose test.

It would need: the API lines for each module pasted into the prompt, one file per task, and a reviewer model or a human for the cross-module design and the RmlUi hover bug.
