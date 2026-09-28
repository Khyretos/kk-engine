# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `b0d0666e1c` (2026-09-28T13:45:13+02:00) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 112 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1859 | 0.1096 | 106 |
| `bench.audio_mix_32_voices_ms` | 0.0930 | 0.0614 | 112 |
| `bench.cloth_16x24_basic_ms` | 4.66 | 2.90 | 56 |
| `bench.cloth_16x24_full_ms` | 9.83 | 5.31 | 56 |
| `bench.cloth_1x32_basic_ms` | 0.5306 | 0.3282 | 56 |
| `bench.cloth_1x32_full_ms` | 1.23 | 0.6111 | 56 |
| `bench.cloth_1x32_off_ms` | 0.5310 | 0.3291 | 56 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 56 |
| `bench.cloth_1x64_full_ms` | 39.12 | 5.27 | 56 |
| `bench.cloth_cape_basic_ms` | 0.5018 | 0.3091 | 45 |
| `bench.cloth_cape_full_ms` | 8.19 | 4.63 | 45 |
| `bench.fracture_bake_cube_ms` | 7.18 | 4.62 | 112 |
| `bench.hair_1x100_straight_ms` | 0.2204 | 0.1071 | 51 |
| `bench.hair_1x160_braids_ms` | 0.5390 | 0.2855 | 33 |
| `bench.hair_1x160_cornrows_ms` | 0.0558 | 0.0317 | 33 |
| `bench.hair_1x200_3b_ms` | 0.5324 | 0.2824 | 36 |
| `bench.hair_1x200_4c_ms` | 0.3023 | 0.1606 | 36 |
| `bench.hair_1x200_bantu_ms` | 0.7248 | 0.3801 | 36 |
| `bench.hair_1x400_curly_ms` | 2.36 | 1.17 | 51 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 51 |
| `bench.hair_8x200_long_ms` | 4.55 | 2.16 | 51 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 112 |
| `bench.lua_think_50_hooks_ms` | 0.2977 | 0.1408 | 112 |
| `bench.net_snapshot_256_bodies_ms` | 0.0856 | 0.0503 | 112 |
| `bench.particle_fluid_2000_ms` | 3.91 | 2.58 | 112 |
| `bench.rigid_crates_400_ms` | 1.66 | 0.9864 | 112 |
| `bench.rigid_raycast_1000_ms` | 0.4983 | 0.3228 | 112 |
| `stress.crates.fps_avg` | 11.13 | 22.85 | 112 |
| `stress.crates.physics_avg_ms` | 0.4040 | 0.2801 | 112 |
| `stress.fps_1pct_low` | 5.56 | 16.28 | 112 |
| `stress.fps_avg` | 10.88 | 23.34 | 112 |
| `stress.frame_p99_ms` | 111 | 57.86 | 112 |
| `stress.impacts.fps_avg` | 9.28 | 19.00 | 112 |
| `stress.peak_rss_mb` | 392 | 251 | 112 |
| `stress.walk.fps_avg` | 12.50 | 29.14 | 112 |

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
