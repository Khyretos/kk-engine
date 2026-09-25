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

### HW-001 · Physics benchmark, optimized build (dev box)
Build with the new preset, then run the scripted benchmark:
```bash
cmake --workflow --preset everything-release
cd build-release/bin
KKE_PHYSICS_BENCH=1200 ./physics_demo 2>&1 | tee bench_release.log
```
It spawns every scene on a fixed schedule, runs 1200 physics ticks
(20 simulated seconds), prints one `BENCH RESULT:` line, and quits by
itself. **Send back:** the `BENCH RESULT` line, the `Task system` line,
and a handful of the `perf:` lines (one per second).
*Expectation:* on 16 threads + a real GPU this should stay near 60 FPS
except for short dips when something big shatters. If it doesn't, the
`perf:` lines say whether the physics step or rendering is the cost.
**Result:** —

### HW-002 · Same benchmark, Debug build (dev box)
Same as HW-001 but from `build/` (`cmake --workflow --preset everything`).
FEMFX itself is optimized in both now; this measures how much the rest
of the engine costs at -O0. **Send back:** the `BENCH RESULT` line.
**Result:** —

### HW-003 · Physics thread scaling (dev box)
```bash
for n in 1 2 4 8 16; do KKE_PHYSICS_THREADS=$n KKE_PHYSICS_BENCH=1200 ./physics_demo 2>&1 | grep "BENCH RESULT"; done
```
Answers PERFORMANCE_NOTES.md item 5 (does the thread pool actually help,
and where does it stop helping?). The sandbox has 4 cores and a software
renderer competing for them, so it can't answer this. **Send back:** all
five lines. **Result:** —

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

## Done

Nothing run on real hardware yet under this checklist. (Earlier
real-hardware findings — the Arch build gaps and the
`VK_EXT_layer_settings` crash — are recorded in INSTRUCTIONS.md.)
