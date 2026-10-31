# Test harness: repeatable runs, input tapes, screenshot compare

ENG-F69. A game test that plays the game the way a person would and
checks the pictures: record your input once, play it back on every
change, compare the screenshots with ones you approved.

## Quick start

```sh
# 1. Play 10 seconds (600 frames) and record what you press
tools/check_game my_game --frames 600 --record tests/tapes/walk.tape

# 2. Play it back and take screenshots at frames 300 and 590
tools/check_game my_game --frames 600 --replay tests/tapes/walk.tape \
    --shots 300:/tmp/shots/mid.png,590:/tmp/shots/end.png --headless

# 3. Happy with them? They become the references
mkdir -p tests/refs/my_game && cp /tmp/shots/*.png tests/refs/my_game/

# 4. From now on, every run compares
tools/check_game my_game --frames 600 --replay tests/tapes/walk.tape \
    --shots 300:/tmp/shots/mid.png,590:/tmp/shots/end.png \
    --expect tests/refs/my_game --headless
```

The last command prints one line per picture (how many pixels differ,
mean and largest difference, PSNR) and FAILED when one differs too much.
A failing picture also gets `diff/<name>.diff.png` next to it: the
reference dimmed, the pixels that changed in magenta.

## What makes a run repeatable

`--frames N` sets `KKE_FIXED_DT=60`: every frame advances exactly 1/60 s
of game time, however long it took to draw. Physics, animation, scripts
and timers see the same times on a fast GPU, on lavapipe in CI and under
a debugger. The game quits by itself after frame N (and after the last
screenshot has come back from the GPU).

Things that still change a picture between machines: different GPUs
round differently (the default tolerance, 8 of 255 per channel and 0.2%
of the pixels, absorbs that between lavapipe runs; compare references
made on the same kind of device), randomness seeded from the clock (seed
from a constant in tests), and anything threaded that finishes in a
different frame (asset streaming: wait a few frames before the first shot).

## The switches

Developer builds only (shipping builds compile them out, kke/DevTools.h).

| Switch | What it does |
|---|---|
| `KKE_FIXED_DT=60` or `=0.0166` | Fixed frame time: frames a second (above 1) or seconds |
| `KKE_INPUT_RECORD=run.tape` | Records keyboard, text, mouse and touch input; written when the game quits |
| `KKE_INPUT_PLAYBACK=run.tape` | Pushes a tape's events back in, each in the frame it was recorded in |
| `KKE_SHOTS=120:a.png,300:b.png` | Screenshots (PNG, what the player sees, UI included) at those frames |
| `KKE_QUIT_AT_FRAME=360` | Quits after that many frames |

`check_game --frames/--record/--replay/--shots/--expect` sets them for you.

## Tapes

A tape is a text file, one event per line, so a short one is easy to
write by hand or to edit:

```
kke-input-tape 1
# frame kind fields...
30 keydown 26 119 0 0      # W pressed (scancode 26, key 'w', no modifiers, not a repeat)
90 keyup 26 119 0 0
120 mousedown 1 1 640 360  # left button, single click, at (640, 360)
121 mouseup 1 1 640 360
150 text hello
```

Kinds: `keydown`/`keyup` (scancode key mod repeat), `text` (spaces as
`\s`), `motion` (x y xrel yrel buttons), `mousedown`/`mouseup` (button
clicks x y), `wheel` (x y mouse_x mouse_y), `fingerdown`/`fingerup`/
`fingermove` (id x y dx dy pressure). Lines may come in any order; they
play sorted by frame.

Gamepads are read by polling, not from events, so a tape doesn't hold
them: script a virtual pad with `KKE_VIRTUAL_PAD_SCRIPT` (docs/INPUT.md).

## Comparing pictures yourself

```sh
kke_imgdiff tests/refs/my_game /tmp/shots            # folder against folder
kke_imgdiff --tolerance 4 --max-diff 0.001 ref.png new.png --diff /tmp/diffs
```

Exit code 0 when every picture matches. In C++ the same check is
`kke::harness::compareImages` / `imagesMatch` (kke/TestHarness.h).
