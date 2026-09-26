# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `9e8a68a6da` (2026-09-26T21:22:55Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 7 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1518 | 0.1518 | 1 |
| `bench.audio_mix_32_voices_ms` | 0.0803 | 0.0803 | 7 |
| `bench.fracture_bake_cube_ms` | 6.89 | 6.89 | 7 |
| `bench.impact_synth_8_materials_ms` | 1.33 | 1.03 | 7 |
| `bench.lua_think_50_hooks_ms` | 0.2270 | 0.2270 | 7 |
| `bench.net_snapshot_256_bodies_ms` | 0.0940 | 0.0855 | 7 |
| `bench.particle_fluid_2000_ms` | 3.98 | 3.89 | 7 |
| `bench.rigid_crates_400_ms` | 1.52 | 1.52 | 7 |
| `bench.rigid_raycast_1000_ms` | 0.4869 | 0.4869 | 7 |
| `stress.crates.fps_avg` | 15.52 | 21.93 | 7 |
| `stress.crates.physics_avg_ms` | 0.4660 | 0.4230 | 7 |
| `stress.fps_1pct_low` | 9.15 | 16.28 | 7 |
| `stress.fps_avg` | 15.59 | 21.82 | 7 |
| `stress.frame_p99_ms` | 83.70 | 59.73 | 7 |
| `stress.impacts.fps_avg` | 12.92 | 17.26 | 7 |
| `stress.peak_rss_mb` | 315 | 251 | 7 |
| `stress.walk.fps_avg` | 18.87 | 27.20 | 7 |

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
