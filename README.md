# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `fcd079e7f0` (2026-09-28T09:04:37Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 105 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1841 | 0.1096 | 99 |
| `bench.audio_mix_32_voices_ms` | 0.0933 | 0.0614 | 105 |
| `bench.cloth_16x24_basic_ms` | 4.66 | 2.90 | 49 |
| `bench.cloth_16x24_full_ms` | 9.62 | 5.31 | 49 |
| `bench.cloth_1x32_basic_ms` | 0.5300 | 0.3282 | 49 |
| `bench.cloth_1x32_full_ms` | 1.19 | 0.6111 | 49 |
| `bench.cloth_1x32_off_ms` | 0.5281 | 0.3291 | 49 |
| `bench.cloth_1x64_basic_ms` | 2.24 | 1.38 | 49 |
| `bench.cloth_1x64_full_ms` | 39.12 | 5.27 | 49 |
| `bench.cloth_cape_basic_ms` | 0.5026 | 0.3091 | 38 |
| `bench.cloth_cape_full_ms` | 8.21 | 4.63 | 38 |
| `bench.fracture_bake_cube_ms` | 7.22 | 4.63 | 105 |
| `bench.hair_1x100_straight_ms` | 0.2214 | 0.1071 | 44 |
| `bench.hair_1x160_braids_ms` | 0.5438 | 0.2865 | 26 |
| `bench.hair_1x160_cornrows_ms` | 0.0559 | 0.0319 | 26 |
| `bench.hair_1x200_3b_ms` | 0.5376 | 0.2830 | 29 |
| `bench.hair_1x200_4c_ms` | 0.3031 | 0.1610 | 29 |
| `bench.hair_1x200_bantu_ms` | 0.7394 | 0.3813 | 29 |
| `bench.hair_1x400_curly_ms` | 2.37 | 1.17 | 44 |
| `bench.hair_1x400_long_ms` | 1.17 | 0.5540 | 44 |
| `bench.hair_8x200_long_ms` | 4.59 | 2.16 | 44 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 105 |
| `bench.lua_think_50_hooks_ms` | 0.2937 | 0.1408 | 105 |
| `bench.net_snapshot_256_bodies_ms` | 0.0886 | 0.0503 | 105 |
| `bench.particle_fluid_2000_ms` | 3.88 | 2.58 | 105 |
| `bench.rigid_crates_400_ms` | 1.63 | 0.9864 | 105 |
| `bench.rigid_raycast_1000_ms` | 0.4928 | 0.3228 | 105 |
| `stress.crates.fps_avg` | 11.08 | 22.85 | 105 |
| `stress.crates.physics_avg_ms` | 0.4354 | 0.2801 | 105 |
| `stress.fps_1pct_low` | 5.80 | 16.28 | 105 |
| `stress.fps_avg` | 10.83 | 23.34 | 105 |
| `stress.frame_p99_ms` | 109 | 57.86 | 105 |
| `stress.impacts.fps_avg` | 9.35 | 19.00 | 105 |
| `stress.peak_rss_mb` | 394 | 251 | 105 |
| `stress.walk.fps_avg` | 12.30 | 29.14 | 105 |

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

![bench.hair_1x160_braids_ms](charts/bench_hair_1x160_braids_ms.svg)

![bench.hair_1x160_cornrows_ms](charts/bench_hair_1x160_cornrows_ms.svg)

![bench.hair_1x200_3b_ms](charts/bench_hair_1x200_3b_ms.svg)

![bench.hair_1x200_4c_ms](charts/bench_hair_1x200_4c_ms.svg)

![bench.hair_1x200_bantu_ms](charts/bench_hair_1x200_bantu_ms.svg)

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
