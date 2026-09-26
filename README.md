# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `3c58296371` (2026-09-26T21:33:05Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 8 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1859 | 0.1518 | 2 |
| `bench.audio_mix_32_voices_ms` | 0.0941 | 0.0803 | 8 |
| `bench.fracture_bake_cube_ms` | 7.19 | 6.89 | 8 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 1.03 | 8 |
| `bench.lua_think_50_hooks_ms` | 0.2821 | 0.2270 | 8 |
| `bench.net_snapshot_256_bodies_ms` | 0.0855 | 0.0855 | 8 |
| `bench.particle_fluid_2000_ms` | 3.91 | 3.89 | 8 |
| `bench.rigid_crates_400_ms` | 1.61 | 1.52 | 8 |
| `bench.rigid_raycast_1000_ms` | 0.4965 | 0.4869 | 8 |
| `stress.crates.fps_avg` | 13.51 | 21.93 | 8 |
| `stress.crates.physics_avg_ms` | 0.4522 | 0.4230 | 8 |
| `stress.fps_1pct_low` | 6.78 | 16.28 | 8 |
| `stress.fps_avg` | 13.59 | 21.82 | 8 |
| `stress.frame_p99_ms` | 97.03 | 59.73 | 8 |
| `stress.impacts.fps_avg` | 11.07 | 17.26 | 8 |
| `stress.peak_rss_mb` | 318 | 251 | 8 |
| `stress.walk.fps_avg` | 16.70 | 27.20 | 8 |

![bench.audio_mix_32_voices_full_ms](charts/bench_audio_mix_32_voices_full_ms.svg)

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
