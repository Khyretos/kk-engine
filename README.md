# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `6dd27fd572` (2026-09-27T15:54:16Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 76 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1853 | 0.1096 | 70 |
| `bench.audio_mix_32_voices_ms` | 0.0939 | 0.0614 | 76 |
| `bench.cloth_16x24_basic_ms` | 4.64 | 2.91 | 20 |
| `bench.cloth_16x24_full_ms` | 8.73 | 5.31 | 20 |
| `bench.cloth_1x32_basic_ms` | 0.5283 | 0.3282 | 20 |
| `bench.cloth_1x32_full_ms` | 1.08 | 0.6111 | 20 |
| `bench.cloth_1x32_off_ms` | 0.5269 | 0.3291 | 20 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 20 |
| `bench.cloth_1x64_full_ms` | 40.96 | 5.27 | 20 |
| `bench.cloth_cape_basic_ms` | 0.4999 | 0.3091 | 9 |
| `bench.cloth_cape_full_ms` | 9.16 | 4.63 | 9 |
| `bench.fracture_bake_cube_ms` | 7.23 | 4.63 | 76 |
| `bench.hair_1x100_straight_ms` | 0.2012 | 0.1071 | 15 |
| `bench.hair_1x400_curly_ms` | 2.17 | 1.17 | 15 |
| `bench.hair_1x400_long_ms` | 1.06 | 0.5540 | 15 |
| `bench.hair_8x200_long_ms` | 4.16 | 2.16 | 15 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9068 | 76 |
| `bench.lua_think_50_hooks_ms` | 0.2766 | 0.1408 | 76 |
| `bench.net_snapshot_256_bodies_ms` | 0.0855 | 0.0503 | 76 |
| `bench.particle_fluid_2000_ms` | 3.95 | 2.58 | 76 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 76 |
| `bench.rigid_raycast_1000_ms` | 0.4941 | 0.3228 | 76 |
| `stress.crates.fps_avg` | 10.77 | 22.85 | 76 |
| `stress.crates.physics_avg_ms` | 0.4394 | 0.2847 | 76 |
| `stress.fps_1pct_low` | 5.56 | 16.28 | 76 |
| `stress.fps_avg` | 10.61 | 23.34 | 76 |
| `stress.frame_p99_ms` | 119 | 57.86 | 76 |
| `stress.impacts.fps_avg` | 9.06 | 19.00 | 76 |
| `stress.peak_rss_mb` | 413 | 251 | 76 |
| `stress.walk.fps_avg` | 12.27 | 29.14 | 76 |

![bench.audio_mix_32_voices_full_ms](charts/bench_audio_mix_32_voices_full_ms.svg)

![bench.audio_mix_32_voices_ms](charts/bench_audio_mix_32_voices_ms.svg)

![bench.cloth_16x24_basic_ms](charts/bench_cloth_16x24_basic_ms.svg)

![bench.cloth_16x24_full_ms](charts/bench_cloth_16x24_full_ms.svg)

![bench.cloth_1x32_basic_ms](charts/bench_cloth_1x32_basic_ms.svg)

![bench.cloth_1x32_full_ms](charts/bench_cloth_1x32_full_ms.svg)

![bench.cloth_1x32_off_ms](charts/bench_cloth_1x32_off_ms.svg)

![bench.cloth_1x64_basic_ms](charts/bench_cloth_1x64_basic_ms.svg)

![bench.cloth_1x64_full_ms](charts/bench_cloth_1x64_full_ms.svg)

![bench.cloth_cape_basic_ms](charts/bench_cloth_cape_basic_ms.svg)

![bench.cloth_cape_full_ms](charts/bench_cloth_cape_full_ms.svg)

![bench.fracture_bake_cube_ms](charts/bench_fracture_bake_cube_ms.svg)

![bench.hair_1x100_straight_ms](charts/bench_hair_1x100_straight_ms.svg)

![bench.hair_1x400_curly_ms](charts/bench_hair_1x400_curly_ms.svg)

![bench.hair_1x400_long_ms](charts/bench_hair_1x400_long_ms.svg)

![bench.hair_8x200_long_ms](charts/bench_hair_8x200_long_ms.svg)

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
