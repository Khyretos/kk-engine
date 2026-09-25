# HARDWARE_TESTS.md — things only real hardware can answer

**How the work is split:** the AI side does the design, code, and every
measurement the sandbox can make (a software-rendered Vulkan device,
optionally pinned to one core to emulate min-spec). Anything that needs
a real GPU, real cores, a real display, or real feel goes on this list
for a person to run. When something here gets run, fill in its
"Result" and move it to "Done," so the next session (human or AI) can
see what has actually been checked on hardware and what hasn't.

Send back the **log lines** each test asks for, not just "it works" —
the numbers are what make runs on different machines comparable.

## The two reference machines

| Name | What it is | Why |
|---|---|---|
| **Min-spec** | 1 CPU core, ~2 GB RAM, software or weakest GPU | The floor. Every claim is "works on min-spec, with these limits." The sandbox emulates this: `taskset -c 0` pins to one core, and peak memory is measured with `/usr/bin/time -v` (see below). |
| **Dev box** | Ryzen 7 9800X3D (8 cores / 16 threads), RX 9070 XT, Arch Linux | What the engine should feel great on. |

### Emulating min-spec on the dev box

```bash
cd build-release/bin   # or build/bin
# 1 core + 2 GB memory cap (systemd user scope; works on Arch)
systemd-run --user --scope -p MemoryMax=2G -p AllowedCPUs=0 \
    env KKE_PHYSICS_BENCH=1200 ./physics_demo
```

If `systemd-run` rejects `AllowedCPUs` (depends on cgroup delegation),
drop that property and put `taskset -c 0` in front of `env` instead.
The physics thread pool now counts the cores it's actually allowed to
use, so a 1-core run really does use 1 worker thread (check the
`Task system: N worker(s)` log line).

## Open — please run these

**Benchmarks now write files** to `benchmark/` (next to where you run
them): a `.txt` to read and a `.json` for analysis, including your CPU,
GPU, driver, RAM, OS and build type. Paste either file back instead of
log lines.

### HW-011 · Sandbox with your packs (dev box, interactive)
`cd build/bin && ./sandbox` with your packs in `assets/synty/` (or type
the folder into the Assets panel). Does it find **all** your packs, and
do the categories make sense? Build something: floor tiles, walls,
stacked props (placement, snapping, rotate, move, duplicate, delete),
then save, quit, restart and load it. Then play: `K` on a character,
`X` on props with each "Breaks as" material, `F` to throw balls.
Anything that loads wrong (textures missing, pieces in the floor) or
feels awkward is exactly what I need.
**Send back:** screenshots, the saved `sandbox_layout.json`, the asset
count line from the Assets panel, and log warnings.
**Result:** —

### HW-010 · Physics thread scaling, as one file (dev box)
Replaces HW-003 (which only produced one line — `KKE_PHYSICS_THREADS`
probably didn't take effect in that shell). From the repository root:
```bash
cmake --workflow --preset everything-release   # if not built yet
cmake -P tools/run_physics_benchmarks.cmake
```
Runs the benchmark at 1, 2, 4, 8, 16 threads (up to your core count).
**Send back:** `benchmark/sweep_<time>/summary.txt`.
**Result:** —

### HW-002 · Same benchmark, Debug build (dev box)
Same as HW-001 but from `build/` (`cmake --workflow --preset everything`).
FEMFX itself is optimized in both now; this measures how much the rest
of the engine costs at -O0. **Send back:** the `BENCH RESULT` line.
**Result:** —

### HW-004 · Min-spec emulation (dev box)
Run the `systemd-run` command above with the release build. **Send back:**
the `BENCH RESULT` line, and whether the Physics panel's "Simulation
can't keep up — running in slow motion" message shows during the big
breaks. *Expectation from the sandbox:* ~10 FPS average while debris is
flying, then physics drops to ~0.2 ms/step once it settles. **Result:** —

### HW-005 · Does it look and feel right? (dev box, interactive)
Run `./physics_demo` normally and click through every scene button,
several times each.
- Do shattered objects show **all** their pieces, including the inside
  faces along the cracks? (Before this change, most pieces were
  invisible — see BUGS.md BUG-026.)
- Does anything fall through the floor, jitter forever, or float?
- After things settle, does the Physics panel show `(0 awake)`?
- Do shadows follow the pieces?
- Is the floor a pale green, and the shards pastel pink/yellow/green?
  That's how it looks in the sandbox's software renderer both before
  and after this change. If it looks different on a real GPU, that's
  worth a screenshot — it may be related to BUGS.md BUG-021.
**Send back:** yes/no per bullet, plus a screenshot of a big pile.
**Result:** —

### HW-006 · Clean exit (dev box)
Close each demo (`physics_demo`, `kke_demo`, `rmlui_demo`, `imgui_demo`)
with the window's close button. None should crash (BUGS.md BUG-025 used
to crash all of them on exit). **Send back:** any crash output.
**Result:** —

### HW-007 · FEMFX capacity warnings (dev box)
While doing HW-001/HW-005, watch the log for
`FEMFX hit a scene capacity limit`. It shouldn't appear. If it does,
send the line — the hex flags say exactly which limit
(`FM_WARNING_FLAG_*` in `external/FEMFX/amd_femfx/inc/AMD_FEMFX.h`).
**Result:** —

### HW-008 · UI showcase on a real desktop (dev box)
`cd build/bin && ./rmlui_demo`. Click through every nav tab. Then in
Settings: toggle **Fullscreen** and **VSync**, set a frame-rate limit,
drag **UI scale** and **FOV**, toggle **Shadows**, rebind a key, then
Apply & save and restart — do your choices come back?
- Does clicking land exactly where you click? (Especially with desktop
  scaling at 125%/150% — this was broken before, BUG-032.)
- Does typing work in the chat box? Does Enter send?
- Drag items around the inventory and onto equipment slots.
- Resize the window small and large: does everything stay on screen
  and readable?
- Do colors look like the screenshots in the session (dark navy panels,
  not washed-out grey)?
- Is VSync on really capped to your refresh rate, and off uncapped
  (check with F1 → Performance panel)?
**Send back:** anything that looks or behaves wrong, with a screenshot.
**Result:** —

### HW-009 · Synty demo, and your other Synty packs (dev box)
`cd build/bin && ./synty_demo` with the Prototype pack in
`assets/synty/POLYGON_Prototype/`. Check the level and characters look
right, press **B** for bones, pose a bone from the Characters panel.
Ragdolls: **R**, **Shift+R**, **T**, and **G** (glass pane) — does the
fall look believable, does anything explode, jitter, or sink through the
floor? How does the frame rate hold when everything falls at once?
Then try another pack you own: unzip it the same way and point
`KKE_SYNTY_DIR` at it — the demo only builds the Prototype level, but
the log line `loaded '...': ... bone(s), ... animation(s), bounds ...`
and any `texture ... not found` warnings for the other pack's files are
what matter. (A quick way to load one: change a path in
`games/synty_demo/SyntySceneModule.cpp`.)
**Send back:** screenshot, plus any warnings/errors from the log.
**Result:** —

## Done

### HW-001 · Physics benchmark, optimized build — ✅ 2026-09-25
Dev box (Ryzen 7 9800X3D, RX 9070 XT). `BENCH RESULT: 1200 ticks in
19.98 s wall (1.00x realtime), 162884 frames (8151.0 fps avg), step avg
1.38 ms max 5.77 ms, render prep avg 0.03 ms, 19 objects / 484 pieces /
2178 tets`. The simulation kept up with real time the entire run; the
worst physics step (5.8 ms) is a third of a 60 Hz frame. For comparison
the 1-core sandbox emulation needs 21 ms average. Settled pile: 0.06 ms.

### HW-003 · Thread scaling — ⚠️ inconclusive 2026-09-25
Only one run came back (step avg 1.40 ms, same as HW-001), so the thread
count most likely didn't change. Superseded by HW-010.

`VK_EXT_layer_settings` crash — are recorded in INSTRUCTIONS.md.)
