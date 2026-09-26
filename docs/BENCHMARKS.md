# Benchmarks

KKE's promise is that a game made with it runs on a 1-core, 2 GB machine
with no GPU. This page is the evidence: what is measured, how to measure
it yourself, and the numbers so far.

There are two benchmarks, and both write the same kind of report
(`kke::BenchmarkReport`: a `.txt` for people and a `.json` for tools):

| | What it measures | Run it |
|---|---|---|
| **Stress test** | The whole engine as a player sees it: the showcase level for 36 s (walk with animation and IK, 300 crates raining, heavy impacts), uncapped. Frames per second, 1% lows, GPU and physics time per phase, and the list of modules that were running. | `KKE_STRESS_TEST=1 ./kke_demo`, or *Performance → Run stress test* |
| **kke_bench** | The engine's CPU building blocks one at a time, headless and single-threaded, with fixed seeds, so a change in a number means the code changed. | `./kke_bench` (`--quick`, `--filter NAME`, `--list`) |

Reports go to `benchmark/` (or `$KKE_BENCH_DIR`). Use a Release build
for numbers you compare; Debug is several times slower.

## kke_bench cases

Each case reports the median and p95 time of one sample, in ms (lower is
better), and a checksum of what it computed. The checksum is the same on
every run of the same commit; if it isn't, the workload stopped being
deterministic, and that's a bug.

| Case | One sample is |
|---|---|
| `rigid_crates_400` | One 60 Hz Jolt step while 400 crates fall and pile up |
| `rigid_raycast_1000` | 1000 raycasts into a settled pile (picking, audio occlusion, camera arm) |
| `fracture_bake_cube` | Voxelizing a 1 m cube at 10 cm and baking a Voronoi fracture (what spawning a breakable costs) |
| `particle_fluid_2000` | One step of 2000 Position Based Fluids particles (the melt and lava budget) |
| `audio_mix_32_voices` | 10 ms of output with 32 spatial voices and reverb; under 10 ms is real time |
| `impact_synth_8_materials` | Synthesizing one impact sound for each default material |
| `lua_think_50_hooks` | One `Think` hook fanned out to 50 Lua handlers |
| `net_snapshot_256_bodies` | Bit-packing and unpacking a 256-body network snapshot |

Cases for code that needs Jolt or Lua are left out of builds without them.

## Tracked over time

The **Benchmarks** workflow (`.github/workflows/benchmarks.yml`) builds
Release on every push to `main`, runs kke_bench and the stress test (under
Xvfb with Mesa's lavapipe software Vulkan, like the smoke tests), and
appends the results to the
[`benchmark-data` branch](https://github.com/Khyretos/kk-engine/tree/benchmark-data):
`history.jsonl` holds every run, and its README shows a table and a trend
chart per number. Pull requests run a quick pass and get a comparison in
the job summary without being recorded. Runs on `main` go one at a time;
when several pushes land while one runs, only the newest waits and the
ones in between are skipped, so the history samples main rather than
listing every commit.

Every number is compared with the median of the last 10 runs, and changes
beyond ±25% are marked in the job summary. Nothing fails on a slow
number: GitHub's shared runners vary by 10-20% between runs, so a single
flag is a reason to look at the chart, not proof. The job does fail when
a benchmark crashes, writes no report, or the build has a warning.

`benchmarks/track.py` does the bookkeeping and needs only Python 3. To
compare your own runs: `python3 benchmarks/track.py --history my-history
--commit $(git rev-parse HEAD) --date now benchmark/*.json`.

## Hardware profiles

`benchmarks/docker/compose.yml` runs both benchmarks inside limits, so
anyone can reproduce the low-end rows on whatever machine they have:

```bash
docker compose -f benchmarks/docker/compose.yml run --rm build         # once: Release build in build-bench/
docker compose -f benchmarks/docker/compose.yml run --rm floor-1c-2g   # reports in benchmark/floor-1c-2g/
```

| Profile | Cores | RAM | GPU |
|---|---|---|---|
| `floor-1c-2g` | 1 | 2 GB | none (lavapipe) |
| `laptop-1c-2g-vram512` | 1 | 2 GB | lavapipe, 512 MB VRAM cap |
| `dual-2c-2g-vram1g` | 2 | 2 GB | lavapipe, 1 GB VRAM cap |
| `mid-4c-8g` | 4 | 8 GB | lavapipe |
| `high-8c-16g` | 8 | 16 GB | lavapipe |
| `intel-arc`, `amd` | all | all | the host GPU through `/dev/dri` (and `/dev/kfd` for AMD) |
| `nvidia` | all | all | the host GPU (needs nvidia-container-toolkit) |
| `custom` | `KKE_CPUSET` | `KKE_MEM` | `KKE_VRAM_BUDGET_MB`, `KKE_GPU_DEVICE`, `KKE_VK_DRIVER_FILES` |

What this can and can't show, honestly:

- Cores are pinned with a cpuset, so the engine sees exactly that many
  (it sizes its worker threads from CPU affinity). RAM is a hard cgroup
  limit with no extra swap.
- Docker can't limit VRAM or turn one GPU into another. The engine's
  **`KKE_VRAM_BUDGET_MB`** setting caps the Vulkan allocator's
  device-local heaps, so allocations beyond the cap fail as they would on
  the smaller card, and the stress report records the cap. It works
  outside Docker too.
- The software-GPU profiles measure the CPU doing the GPU's work, which
  is the real worst case for "no GPU". They say nothing about how fast a
  real low-end GPU is; those rows have to come from real hardware.
- Phones and Macs can't be emulated honestly (different CPUs, GPUs and
  drivers). Their rows come from people's own devices.

## Results

Release builds, `kke_demo` without FEMFX (the default build), 1280x720.
Frame times are the stress test's; the kke_bench column is the
`rigid_crates_400` median as a CPU speed reference.

### Core modules (Settings, Input, RigidBodies, Network, Models, UI, Audio, Scripts, Showcase)

| Machine | Profile | GPU | FPS avg | 1% low | Verdict | Jolt step (ms) |
|---|---|---|---:|---:|---|---:|
| Cloud VM, Xeon @ 2.80 GHz (2026-09-26) | `floor-1c-2g` | lavapipe (software) | 7.2 | 4.1 | too slow | 1.66 |
| Cloud VM, Xeon @ 2.80 GHz (2026-09-26) | `laptop-1c-2g-vram512` | lavapipe, 512 MB cap | 7.3 | 4.6 | too slow | 1.70 |
| Cloud VM, Xeon @ 2.80 GHz (2026-09-26) | `dual-2c-2g-vram1g` | lavapipe, 1 GB cap | 11.8 | 7.6 | too slow | 1.69 |
| Cloud VM, Xeon @ 2.80 GHz (2026-09-26) | `mid-4c-8g` | lavapipe (software) | 19.4 | 11.3 | too slow | 1.66 |
| Cloud VM, Xeon @ 2.80 GHz, 4 vCPU, 16 GB (2026-09-26) | whole machine, no Docker | lavapipe (software) | 20.0 | 12.3 | too slow | 1.74 |

What these say: on a machine with no GPU at all, the CPU also draws every
pixel, and drawing is most of the frame (on the floor profile the GPU
passes take about 95% of it; physics is under 1 ms). The stress test is
deliberately harder than a normal scene, so "too slow" here is the worst
case, not what a typical game on that machine gets. The fps roughly
scale with cores because lavapipe renders on every core it's given. The
Jolt column stays flat because kke_bench is single-threaded by design.
The VRAM caps didn't change anything: the showcase fits in 512 MB.

### Core + FEMFX

No rows yet: the default build leaves FEMFX off. Build with
`-DKKE_ENABLE_FEMFX=ON` and send yours.

### Your hardware

Real machines are the rows that matter. Run the stress test (and
`./kke_bench` if you like) on a Release build, then open a
[Benchmark result](https://github.com/Khyretos/kk-engine/issues/new?template=benchmark_result.yml)
issue with the reports attached. Rows are added here with the CPU, GPU
and OS from the report, never the host name.
