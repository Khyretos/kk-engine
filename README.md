# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `5a80badb39` (2026-09-28T05:31:04Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 96 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1166 | 0.1096 | 90 |
| `bench.audio_mix_32_voices_ms` | 0.0702 | 0.0614 | 96 |
| `bench.cloth_16x24_basic_ms` | 2.91 | 2.90 | 40 |
| `bench.cloth_16x24_full_ms` | 5.56 | 5.31 | 40 |
| `bench.cloth_1x32_basic_ms` | 0.3439 | 0.3282 | 40 |
| `bench.cloth_1x32_full_ms` | 0.7002 | 0.6111 | 40 |
| `bench.cloth_1x32_off_ms` | 0.3421 | 0.3291 | 40 |
| `bench.cloth_1x64_basic_ms` | 1.39 | 1.38 | 40 |
| `bench.cloth_1x64_full_ms` | 25.57 | 5.27 | 40 |
| `bench.cloth_cape_basic_ms` | 0.3219 | 0.3091 | 29 |
| `bench.cloth_cape_full_ms` | 5.21 | 4.63 | 29 |
| `bench.fracture_bake_cube_ms` | 4.85 | 4.63 | 96 |
| `bench.hair_1x100_straight_ms` | 0.1234 | 0.1071 | 35 |
| `bench.hair_1x160_braids_ms` | 0.3009 | 0.3009 | 17 |
| `bench.hair_1x160_cornrows_ms` | 0.0331 | 0.0327 | 17 |
| `bench.hair_1x200_3b_ms` | 0.2830 | 0.2830 | 20 |
| `bench.hair_1x200_4c_ms` | 0.1610 | 0.1610 | 20 |
| `bench.hair_1x200_bantu_ms` | 0.3852 | 0.3852 | 20 |
| `bench.hair_1x400_curly_ms` | 1.32 | 1.17 | 35 |
| `bench.hair_1x400_long_ms` | 0.6495 | 0.5540 | 35 |
| `bench.hair_8x200_long_ms` | 2.51 | 2.16 | 35 |
| `bench.impact_synth_8_materials_ms` | 0.9707 | 0.9068 | 96 |
| `bench.lua_think_50_hooks_ms` | 0.1451 | 0.1408 | 96 |
| `bench.net_snapshot_256_bodies_ms` | 0.0565 | 0.0503 | 96 |
| `bench.particle_fluid_2000_ms` | 2.74 | 2.58 | 96 |
| `bench.rigid_crates_400_ms` | 1.04 | 0.9864 | 96 |
| `bench.rigid_raycast_1000_ms` | 0.3377 | 0.3228 | 96 |
| `stress.crates.fps_avg` | 18.13 | 22.85 | 96 |
| `stress.crates.physics_avg_ms` | 0.2960 | 0.2847 | 96 |
| `stress.fps_1pct_low` | 11.79 | 16.28 | 96 |
| `stress.fps_avg` | 18.33 | 23.34 | 96 |
| `stress.frame_p99_ms` | 66.11 | 57.86 | 96 |
| `stress.impacts.fps_avg` | 15.74 | 19.00 | 96 |
| `stress.peak_rss_mb` | 393 | 251 | 96 |
| `stress.walk.fps_avg` | 21.67 | 29.14 | 96 |

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
