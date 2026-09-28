# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `96429afdde` (2026-09-28T12:54:03Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 115 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1848 | 0.1096 | 109 |
| `bench.audio_mix_32_voices_ms` | 0.0929 | 0.0614 | 115 |
| `bench.cloth_16x24_basic_ms` | 4.62 | 2.90 | 59 |
| `bench.cloth_16x24_full_ms` | 9.93 | 5.31 | 59 |
| `bench.cloth_1x32_basic_ms` | 0.5265 | 0.3282 | 59 |
| `bench.cloth_1x32_full_ms` | 1.23 | 0.6111 | 59 |
| `bench.cloth_1x32_off_ms` | 0.5256 | 0.3291 | 59 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 59 |
| `bench.cloth_1x64_full_ms` | 39.20 | 5.27 | 59 |
| `bench.cloth_cape_basic_ms` | 0.4979 | 0.3091 | 48 |
| `bench.cloth_cape_full_ms` | 8.22 | 4.63 | 48 |
| `bench.fracture_bake_cube_ms` | 7.17 | 4.62 | 115 |
| `bench.hair_1x100_straight_ms` | 0.2211 | 0.1071 | 54 |
| `bench.hair_1x160_braids_ms` | 0.5373 | 0.2855 | 36 |
| `bench.hair_1x160_cornrows_ms` | 0.0558 | 0.0317 | 36 |
| `bench.hair_1x200_3b_ms` | 0.5325 | 0.2824 | 39 |
| `bench.hair_1x200_4c_ms` | 0.3025 | 0.1606 | 39 |
| `bench.hair_1x200_bantu_ms` | 0.7225 | 0.3801 | 39 |
| `bench.hair_1x400_curly_ms` | 2.35 | 1.17 | 54 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 54 |
| `bench.hair_8x200_long_ms` | 4.56 | 2.16 | 54 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 115 |
| `bench.lua_think_50_hooks_ms` | 0.2918 | 0.1408 | 115 |
| `bench.net_snapshot_256_bodies_ms` | 0.0852 | 0.0503 | 115 |
| `bench.particle_fluid_2000_ms` | 3.89 | 2.58 | 115 |
| `bench.rigid_crates_400_ms` | 1.61 | 0.9864 | 115 |
| `bench.rigid_raycast_1000_ms` | 0.4986 | 0.3228 | 115 |
| `stress.crates.fps_avg` | 11.09 | 22.85 | 115 |
| `stress.crates.physics_avg_ms` | 0.4425 | 0.2801 | 115 |
| `stress.fps_1pct_low` | 5.79 | 16.28 | 115 |
| `stress.fps_avg` | 10.82 | 23.34 | 115 |
| `stress.frame_p99_ms` | 110 | 57.86 | 115 |
| `stress.impacts.fps_avg` | 9.25 | 19.00 | 115 |
| `stress.peak_rss_mb` | 413 | 251 | 115 |
| `stress.walk.fps_avg` | 12.38 | 29.14 | 115 |

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
