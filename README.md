# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `6f5c52cd0d` (2026-09-28T06:35:48Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 102 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1839 | 0.1096 | 96 |
| `bench.audio_mix_32_voices_ms` | 0.0930 | 0.0614 | 102 |
| `bench.cloth_16x24_basic_ms` | 4.66 | 2.90 | 46 |
| `bench.cloth_16x24_full_ms` | 9.85 | 5.31 | 46 |
| `bench.cloth_1x32_basic_ms` | 0.5303 | 0.3282 | 46 |
| `bench.cloth_1x32_full_ms` | 1.18 | 0.6111 | 46 |
| `bench.cloth_1x32_off_ms` | 0.5272 | 0.3291 | 46 |
| `bench.cloth_1x64_basic_ms` | 2.24 | 1.38 | 46 |
| `bench.cloth_1x64_full_ms` | 39.23 | 5.27 | 46 |
| `bench.cloth_cape_basic_ms` | 0.5082 | 0.3091 | 35 |
| `bench.cloth_cape_full_ms` | 8.17 | 4.63 | 35 |
| `bench.fracture_bake_cube_ms` | 7.21 | 4.63 | 102 |
| `bench.hair_1x100_straight_ms` | 0.2214 | 0.1071 | 41 |
| `bench.hair_1x160_braids_ms` | 0.5416 | 0.2865 | 23 |
| `bench.hair_1x160_cornrows_ms` | 0.0559 | 0.0319 | 23 |
| `bench.hair_1x200_3b_ms` | 0.5391 | 0.2830 | 26 |
| `bench.hair_1x200_4c_ms` | 0.3037 | 0.1610 | 26 |
| `bench.hair_1x200_bantu_ms` | 0.7348 | 0.3813 | 26 |
| `bench.hair_1x400_curly_ms` | 2.36 | 1.17 | 41 |
| `bench.hair_1x400_long_ms` | 1.18 | 0.5540 | 41 |
| `bench.hair_8x200_long_ms` | 4.63 | 2.16 | 41 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 102 |
| `bench.lua_think_50_hooks_ms` | 0.3004 | 0.1408 | 102 |
| `bench.net_snapshot_256_bodies_ms` | 0.0885 | 0.0503 | 102 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 102 |
| `bench.rigid_crates_400_ms` | 1.62 | 0.9864 | 102 |
| `bench.rigid_raycast_1000_ms` | 0.4894 | 0.3228 | 102 |
| `stress.crates.fps_avg` | 11.00 | 22.85 | 102 |
| `stress.crates.physics_avg_ms` | 0.3921 | 0.2801 | 102 |
| `stress.fps_1pct_low` | 5.53 | 16.28 | 102 |
| `stress.fps_avg` | 10.80 | 23.34 | 102 |
| `stress.frame_p99_ms` | 112 | 57.86 | 102 |
| `stress.impacts.fps_avg` | 9.24 | 19.00 | 102 |
| `stress.peak_rss_mb` | 417 | 251 | 102 |
| `stress.walk.fps_avg` | 12.42 | 29.14 | 102 |

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
