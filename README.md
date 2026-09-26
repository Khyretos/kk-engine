# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `b9974910c2` (2026-09-26T18:44:46Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 6 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_ms` | 0.0926 | 0.0926 | 6 |
| `bench.fracture_bake_cube_ms` | 7.27 | 7.20 | 6 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 1.03 | 6 |
| `bench.lua_think_50_hooks_ms` | 0.2889 | 0.2768 | 6 |
| `bench.net_snapshot_256_bodies_ms` | 0.0855 | 0.0855 | 6 |
| `bench.particle_fluid_2000_ms` | 3.89 | 3.89 | 6 |
| `bench.rigid_crates_400_ms` | 1.59 | 1.59 | 6 |
| `bench.rigid_raycast_1000_ms` | 0.4979 | 0.4921 | 6 |
| `stress.crates.fps_avg` | 15.01 | 21.93 | 6 |
| `stress.crates.physics_avg_ms` | 0.4230 | 0.4230 | 6 |
| `stress.fps_1pct_low` | 8.92 | 16.28 | 6 |
| `stress.fps_avg` | 16.87 | 21.82 | 6 |
| `stress.frame_p99_ms` | 83.26 | 59.73 | 6 |
| `stress.impacts.fps_avg` | 12.41 | 17.26 | 6 |
| `stress.peak_rss_mb` | 308 | 251 | 6 |
| `stress.walk.fps_avg` | 24.42 | 27.20 | 6 |

![bench.audio_mix_32_voices_ms](charts/bench_audio_mix_32_voices_ms.svg)

![bench.fracture_bake_cube_ms](charts/bench_fracture_bake_cube_ms.svg)

![bench.impact_synth_8_materials_ms](charts/bench_impact_synth_8_materials_ms.svg)

![bench.lua_think_50_hooks_ms](charts/bench_lua_think_50_hooks_ms.svg)

![bench.net_snapshot_256_bodies_ms](charts/bench_net_snapshot_256_bodies_ms.svg)

![bench.particle_fluid_2000_ms](charts/bench_particle_fluid_2000_ms.svg)

![bench.rigid_crates_400_ms](charts/bench_rigid_crates_400_ms.svg)

![bench.rigid_raycast_1000_ms](charts/bench_rigid_raycast_1000_ms.svg)

![stress.crates.fps_avg](charts/stress_crates_fps_avg.svg)

![stress.crates.physics_avg_ms](charts/stress_crates_physics_avg_ms.svg)

![stress.fps_1pct_low](charts/stress_fps_1pct_low.svg)

![stress.fps_avg](charts/stress_fps_avg.svg)

![stress.frame_p99_ms](charts/stress_frame_p99_ms.svg)

![stress.impacts.fps_avg](charts/stress_impacts_fps_avg.svg)

![stress.peak_rss_mb](charts/stress_peak_rss_mb.svg)

![stress.walk.fps_avg](charts/stress_walk_fps_avg.svg)
