# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `17a3e385d5` (2026-09-26T21:40:15Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 9 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1846 | 0.1518 | 3 |
| `bench.audio_mix_32_voices_ms` | 0.0937 | 0.0803 | 9 |
| `bench.fracture_bake_cube_ms` | 7.19 | 6.89 | 9 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 1.03 | 9 |
| `bench.lua_think_50_hooks_ms` | 0.2844 | 0.2270 | 9 |
| `bench.net_snapshot_256_bodies_ms` | 0.0855 | 0.0855 | 9 |
| `bench.particle_fluid_2000_ms` | 3.96 | 3.89 | 9 |
| `bench.rigid_crates_400_ms` | 1.66 | 1.52 | 9 |
| `bench.rigid_raycast_1000_ms` | 0.4997 | 0.4869 | 9 |
| `stress.crates.fps_avg` | 13.93 | 21.93 | 9 |
| `stress.crates.physics_avg_ms` | 0.4312 | 0.4230 | 9 |
| `stress.fps_1pct_low` | 7.76 | 16.28 | 9 |
| `stress.fps_avg` | 13.79 | 21.82 | 9 |
| `stress.frame_p99_ms` | 89.87 | 59.73 | 9 |
| `stress.impacts.fps_avg` | 11.41 | 17.26 | 9 |
| `stress.peak_rss_mb` | 319 | 251 | 9 |
| `stress.walk.fps_avg` | 16.48 | 27.20 | 9 |

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
