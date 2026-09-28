# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `0fb409b31f` (2026-09-28T07:43:44Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 103 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1517 | 0.1096 | 97 |
| `bench.audio_mix_32_voices_ms` | 0.0807 | 0.0614 | 103 |
| `bench.cloth_16x24_basic_ms` | 3.72 | 2.90 | 47 |
| `bench.cloth_16x24_full_ms` | 7.83 | 5.31 | 47 |
| `bench.cloth_1x32_basic_ms` | 0.4246 | 0.3282 | 47 |
| `bench.cloth_1x32_full_ms` | 0.9563 | 0.6111 | 47 |
| `bench.cloth_1x32_off_ms` | 0.4228 | 0.3291 | 47 |
| `bench.cloth_1x64_basic_ms` | 1.79 | 1.38 | 47 |
| `bench.cloth_1x64_full_ms` | 33.14 | 5.27 | 47 |
| `bench.cloth_cape_basic_ms` | 0.3948 | 0.3091 | 36 |
| `bench.cloth_cape_full_ms` | 7.00 | 4.63 | 36 |
| `bench.fracture_bake_cube_ms` | 6.17 | 4.63 | 103 |
| `bench.hair_1x100_straight_ms` | 0.1726 | 0.1071 | 42 |
| `bench.hair_1x160_braids_ms` | 0.4128 | 0.2865 | 24 |
| `bench.hair_1x160_cornrows_ms` | 0.0451 | 0.0319 | 24 |
| `bench.hair_1x200_3b_ms` | 0.4129 | 0.2830 | 27 |
| `bench.hair_1x200_4c_ms` | 0.2341 | 0.1610 | 27 |
| `bench.hair_1x200_bantu_ms` | 0.5558 | 0.3813 | 27 |
| `bench.hair_1x400_curly_ms` | 1.85 | 1.17 | 42 |
| `bench.hair_1x400_long_ms` | 0.9116 | 0.5540 | 42 |
| `bench.hair_8x200_long_ms` | 3.56 | 2.16 | 42 |
| `bench.impact_synth_8_materials_ms` | 0.9500 | 0.9068 | 103 |
| `bench.lua_think_50_hooks_ms` | 0.2070 | 0.1408 | 103 |
| `bench.net_snapshot_256_bodies_ms` | 0.0668 | 0.0503 | 103 |
| `bench.particle_fluid_2000_ms` | 3.39 | 2.58 | 103 |
| `bench.rigid_crates_400_ms` | 1.36 | 0.9864 | 103 |
| `bench.rigid_raycast_1000_ms` | 0.3481 | 0.3228 | 103 |
| `stress.crates.fps_avg` | 14.16 | 22.85 | 103 |
| `stress.crates.physics_avg_ms` | 0.3603 | 0.2801 | 103 |
| `stress.fps_1pct_low` | 8.49 | 16.28 | 103 |
| `stress.fps_avg` | 14.06 | 23.34 | 103 |
| `stress.frame_p99_ms` | 85.45 | 57.86 | 103 |
| `stress.impacts.fps_avg` | 12.01 | 19.00 | 103 |
| `stress.peak_rss_mb` | 411 | 251 | 103 |
| `stress.walk.fps_avg` | 16.38 | 29.14 | 103 |

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
