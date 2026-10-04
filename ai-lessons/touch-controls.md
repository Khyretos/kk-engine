# Lesson: touch controls in every kk-engine demo (2026-10-04)

Commit 6bed915 (merged as 238cca3 on main). Docs: `docs/TOUCH.md`.

## What was built, and why this shape

- Goal: every demo playable with fingers alone; players move, resize and rebind the buttons.
- Pure logic in one testable class (`kke::TouchControls`, no SDL/RmlUi): layout guess, fingers,
  sticks, buttons, toggles, look drag, editing, JSON save. 10 gtests cover it. Drawing is a
  separate RmlUi document (`engine/src/TouchOverlay.cpp`). Keep logic and drawing apart: the
  logic is then testable without a window.
- No per-game code needed: the layout is GUESSED from the game's controller bindings (A, X, B,
  Y, triggers, shoulders...). Games only steer it with `setTouchLayout({buttons...})`.
- Sticks feed "any controller" stick axes into player 1's map, so each game's deadzones and
  inverts still apply. Buttons use `InputMap::setScreenButton`; a new `setScreenAxis` holds
  Axis1D actions (racing throttle).
- `Application::addEventClaim`: a finger on a control is claimed before every module, so it is
  never also a click in the game. Without this, the orbit camera and picking also reacted.
- Controls are drawn OVER the HUD and a control under the finger wins over a HUD prompt:
  what the finger sees is what it presses. Menus hide the controls (game context disabled).

## What went wrong, and the fix

| Problem | Cause | Fix |
|---|---|---|
| Look drag far too fast | guessed factor x3 | calibrate: a short-side swipe = ~1 s of full stick (factor 1) |
| Edit bar hidden by HUD | overlay pushed to back | pull overlay to front (now always while shown) |
| Two buttons both "Left hand" | label cut at ": " | cut at ": " only when the label stays unique |
| Bottom buttons under the HUD hint bar | overlay under HUD | draw over HUD + controls win the hit test |
| Start menu needed a key press first | lobby only listened to devices | tap on a card pins player 1 to touch |
| CI check `check_std_includes.py` failed | used `std::tolower`, `std::pair` | add `<cctype>`, `<utility>` |
| Scripted fingers never press RmlUi menu buttons | SDL only makes a mouse from REAL touch, not pushed events | use xdotool mouse clicks for menus, scripted fingers for controls |

## Exact commands (all run and verified in the cloud container)

Build (deps came from mirrors, see the android-ci lesson):
```
cmake -S kk-engine -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DKKE_FETCH_SKIES=OFF \
  -DFETCHCONTENT_SOURCE_DIR_LUA=/home/user/deps/lua \
  -DFETCHCONTENT_SOURCE_DIR_SQLITE=/home/user/deps/bsq/package/deps/sqlite3 \
  -DFETCHCONTENT_SOURCE_DIR_WAYLAND_SCANNER_SRC=/home/user/deps/wayland
cmake --build build -j8 > build.log 2>&1; grep -cE "warning:|error:" build.log   # must print 0
```
Tests: `build/bin/kke_tests --gtest_brief=1` (991 passed, 23 skipped: need servers).
Only my tests: `--gtest_filter='Touch*:InputMap*:Lobby*'` (50 passed).

Pretend to be a phone (no touch screen needed):
```
KKE_MAIN_MENU=0 KKE_PROMPT_STYLE=touch KKE_WINDOW=1280x720 KKE_TARGET=android \
KKE_VIRTUAL_TOUCH_SCRIPT="3:down 1 0.15 0.8,3.5:move 1 0.15 0.65,5:up 1,7:tap 0.9 0.85" \
tools/check_game racing --bin build/bin --seconds 9 --shot racing.jpg
```
Script format: `seconds:down ID X Y`, `move ID X Y`, `up ID`, `tap X Y`; X/Y are 0..1 of the
window. The log line `touch controls: stick left, look look.rate, button ...` names the guess.
Then LOOK at the screenshot: tests pass while a button sits under a HUD bar.

Clang check (Android CI uses clang; GCC misses some warnings):
```
# take the file's command from build/compile_commands.json, swap the compiler for clang++,
# drop "-o file", add: -fsyntax-only -Wno-unknown-warning-option -Werror -Wall -Wextra
```
CI-only checks, run before every push:
`python3 tools/ci/check_std_includes.py` and `python3 tools/ci/check_dependencies.py`.

Push (Kees: straight to main, no PRs): `git fetch origin main && git merge origin/main`,
rebuild, re-run the tests, then `git push origin HEAD:main`.

## Common errors

- `uses std::X without #include <Y>`: add the header in the file that uses it.
- `-Wunused-const-variable` only on Android: clang warns, GCC doesn't. Delete the dead code.
- A finger that also clicks the game: the event was not claimed; check `addEventClaim`.
- RmlUi `data-for` needs a named index (`data-for="row, i : rows"`) to put `i` in an attribute.

## Could a 9B local model do this alone?

No, not this whole task. It spans 30+ files: engine input, RmlUi, SDL events, the menu
system and 8 games, and the hard parts were judgement from screenshots (too fast, hidden,
same labels). A 9B model loses the thread across that many files.

Yes, with help, for pieces of it:
- Adding `setTouchLayout({...})` to a new demo: one call, copy from `games/racing`.
- Adding a gtest case to `tests/test_touch_controls.cpp` from a written rule.
- Running the smoke command above and reporting warnings (a runner job).
What it needs: the exact file and function names, one example to copy, the commands above,
and a reviewer (Claude or Kees) to look at the screenshots.
