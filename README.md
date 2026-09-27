# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `d15ebcda74` (2026-09-27T18:15:42Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 81 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1852 | 0.1096 | 75 |
| `bench.audio_mix_32_voices_ms` | 0.0938 | 0.0614 | 81 |
| `bench.cloth_16x24_basic_ms` | 4.68 | 2.91 | 25 |
| `bench.cloth_16x24_full_ms` | 9.44 | 5.31 | 25 |
| `bench.cloth_1x32_basic_ms` | 0.5348 | 0.3282 | 25 |
| `bench.cloth_1x32_full_ms` | 1.14 | 0.6111 | 25 |
| `bench.cloth_1x32_off_ms` | 0.5321 | 0.3291 | 25 |
| `bench.cloth_1x64_basic_ms` | 2.25 | 1.38 | 25 |
| `bench.cloth_1x64_full_ms` | 40.34 | 5.27 | 25 |
| `bench.cloth_cape_basic_ms` | 0.5097 | 0.3091 | 14 |
| `bench.cloth_cape_full_ms` | 8.17 | 4.63 | 14 |
| `bench.fracture_bake_cube_ms` | 7.23 | 4.63 | 81 |
| `bench.hair_1x100_straight_ms` | 0.2008 | 0.1071 | 20 |
| `bench.hair_1x160_braids_ms` | 0.5391 | 0.5391 | 2 |
| `bench.hair_1x160_cornrows_ms` | 0.0559 | 0.0559 | 2 |
| `bench.hair_1x200_3b_ms` | 0.5343 | 0.4540 | 5 |
| `bench.hair_1x200_4c_ms` | 0.3032 | 0.2404 | 5 |
| `bench.hair_1x200_bantu_ms` | 0.7276 | 0.5872 | 5 |
| `bench.hair_1x400_curly_ms` | 2.18 | 1.17 | 20 |
| `bench.hair_1x400_long_ms` | 1.05 | 0.5540 | 20 |
| `bench.hair_8x200_long_ms` | 4.23 | 2.16 | 20 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9068 | 81 |
| `bench.lua_think_50_hooks_ms` | 0.2841 | 0.1408 | 81 |
| `bench.net_snapshot_256_bodies_ms` | 0.0866 | 0.0503 | 81 |
| `bench.particle_fluid_2000_ms` | 3.94 | 2.58 | 81 |
| `bench.rigid_crates_400_ms` | 1.61 | 0.9864 | 81 |
| `bench.rigid_raycast_1000_ms` | 0.4953 | 0.3228 | 81 |
| `stress.crates.fps_avg` | 10.62 | 22.85 | 81 |
| `stress.crates.physics_avg_ms` | 0.4231 | 0.2847 | 81 |
| `stress.fps_1pct_low` | 5.45 | 16.28 | 81 |
| `stress.fps_avg` | 10.50 | 23.34 | 81 |
| `stress.frame_p99_ms` | 116 | 57.86 | 81 |
| `stress.impacts.fps_avg` | 8.96 | 19.00 | 81 |
| `stress.peak_rss_mb` | 391 | 251 | 81 |
| `stress.walk.fps_avg` | 12.21 | 29.14 | 81 |

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
