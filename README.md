# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `ca79750516` (2026-09-28T10:36:07Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 110 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1866 | 0.1096 | 104 |
| `bench.audio_mix_32_voices_ms` | 0.1045 | 0.0614 | 110 |
| `bench.cloth_16x24_basic_ms` | 4.79 | 2.90 | 54 |
| `bench.cloth_16x24_full_ms` | 10.42 | 5.31 | 54 |
| `bench.cloth_1x32_basic_ms` | 0.5468 | 0.3282 | 54 |
| `bench.cloth_1x32_full_ms` | 1.28 | 0.6111 | 54 |
| `bench.cloth_1x32_off_ms` | 0.5455 | 0.3291 | 54 |
| `bench.cloth_1x64_basic_ms` | 2.30 | 1.38 | 54 |
| `bench.cloth_1x64_full_ms` | 43.51 | 5.27 | 54 |
| `bench.cloth_cape_basic_ms` | 0.5142 | 0.3091 | 43 |
| `bench.cloth_cape_full_ms` | 9.07 | 4.63 | 43 |
| `bench.fracture_bake_cube_ms` | 7.93 | 4.62 | 110 |
| `bench.hair_1x100_straight_ms` | 0.2232 | 0.1071 | 49 |
| `bench.hair_1x160_braids_ms` | 0.5279 | 0.2855 | 31 |
| `bench.hair_1x160_cornrows_ms` | 0.0568 | 0.0317 | 31 |
| `bench.hair_1x200_3b_ms` | 0.5257 | 0.2824 | 34 |
| `bench.hair_1x200_4c_ms` | 0.2963 | 0.1606 | 34 |
| `bench.hair_1x200_bantu_ms` | 0.7062 | 0.3801 | 34 |
| `bench.hair_1x400_curly_ms` | 2.39 | 1.17 | 49 |
| `bench.hair_1x400_long_ms` | 1.17 | 0.5540 | 49 |
| `bench.hair_8x200_long_ms` | 4.59 | 2.16 | 49 |
| `bench.impact_synth_8_materials_ms` | 1.21 | 0.9068 | 110 |
| `bench.lua_think_50_hooks_ms` | 0.2704 | 0.1408 | 110 |
| `bench.net_snapshot_256_bodies_ms` | 0.0840 | 0.0503 | 110 |
| `bench.particle_fluid_2000_ms` | 4.36 | 2.58 | 110 |
| `bench.rigid_crates_400_ms` | 1.73 | 0.9864 | 110 |
| `bench.rigid_raycast_1000_ms` | 0.4515 | 0.3228 | 110 |
| `stress.crates.fps_avg` | 10.94 | 22.85 | 110 |
| `stress.crates.physics_avg_ms` | 0.4583 | 0.2801 | 110 |
| `stress.fps_1pct_low` | 5.48 | 16.28 | 110 |
| `stress.fps_avg` | 10.67 | 23.34 | 110 |
| `stress.frame_p99_ms` | 114 | 57.86 | 110 |
| `stress.impacts.fps_avg` | 9.13 | 19.00 | 110 |
| `stress.peak_rss_mb` | 413 | 251 | 110 |
| `stress.walk.fps_avg` | 12.20 | 29.14 | 110 |

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
