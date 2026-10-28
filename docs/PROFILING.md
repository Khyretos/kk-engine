# Profiling: where the time and the memory go

`kke::Profiler` ([kke/Profiler.h](../engine/include/kke/Profiler.h)) answers
"why is this frame slow" in three levels, from always-on to a file you open
in a viewer.

## Always on: the Performance panel

Application times **every module call** (`update`, `fixedUpdate`, `render`,
`compute`, `renderUi`, ...: all of them go through one function, so every
module and every game is covered with no work). The Performance panel (F1)
lists the eight slowest, each with its average and worst case over the last
120 frames, next to the CPU and GPU frame-time graphs. The cost is two clock
reads per call, about 4 microseconds a frame for a 20-module game.

From Lua: `profile.modules(8)` gives the same table, `profile.frame()` the
average and worst frame time.

## A capture: a file for Perfetto

A capture records, until it ends:

- every **zone** from every thread (each module call, plus any
  `KKE_PROFILE_SCOPE` and `profile.push` you add), each thread on its own
  named track;
- **counters**, once a frame: GPU frame time, process memory, GPU memory;
- a mark for every frame, with its length.

It ends in one `.json` file in Chrome's trace format. Open it at
[ui.perfetto.dev](https://ui.perfetto.dev) (or `chrome://tracing`): zoom into
a slow frame, see which module and which thread, and read the counters above
the tracks. The same file holds a table of every zone (calls, total, average,
95th percentile, worst) under `kkeSummary`, and the log prints its top ten.

Start one from:

| Where | How |
|---|---|
| The Performance panel | **Capture 5 s**, then the file's path is shown. **Stop and save** ends one early. |
| The command line | `KKE_PROFILE=trace.json ./game` records 600 frames; `KKE_PROFILE=trace.json:120` records 120. |
| Lua | `profile.capture(seconds, path)` (path optional) returns the path; `profile.stop()` ends it now; `profile.active()`. |
| C++ | `Profiler::get().startCapture(options)`, `stopCapture()`. |

Files from the panel and from Lua go to `profiles/profile-YYYYMMDD-HHMMSS.json`
in the game's user data folder. A capture keeps at most two million events
(the file says how many it dropped), so a long one can't fill the memory.

## Your own zones

```lua
profile.push("pathfinding")
-- ... the part you want to see
profile.pop()
```

shows up as `lua: pathfinding` on the main thread's track. In C++:

```cpp
void Thing::update(const UpdateContext&) {
    KKE_PROFILE_SCOPE("Thing.update");
    // ...
}
```

Outside a capture a zone costs one relaxed atomic load; the name is looked up
once, the first time the line runs. `Profiler::get().counter(id, value)` adds a
value over time (the number of agents, say), and `setCounterSource` runs once
a frame while a capture is on.

## What it does not do (yet)

- **GPU time per pass.** The GPU track is the whole frame's time from the
  renderer's timestamp queries. Per-pass timestamps need a query slot in every
  pass; `KKE_ENABLE_GPU_PROFILER` with the VulkanProfiler layer gives
  per-draw data meanwhile (docs/HISTORY.md).
- **Allocation tracking.** Memory is the process total and the GPU heap in use,
  not who allocated it.

Tested in `tests/test_profiler.cpp` (nesting, valid JSON, threads, limits,
the rolling window).
