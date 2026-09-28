# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `551402e0d3` (2026-09-28T10:27:02Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 109 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1850 | 0.1096 | 103 |
| `bench.audio_mix_32_voices_ms` | 0.0935 | 0.0614 | 109 |
| `bench.cloth_16x24_basic_ms` | 4.64 | 2.90 | 53 |
| `bench.cloth_16x24_full_ms` | 9.60 | 5.31 | 53 |
| `bench.cloth_1x32_basic_ms` | 0.5298 | 0.3282 | 53 |
| `bench.cloth_1x32_full_ms` | 1.24 | 0.6111 | 53 |
| `bench.cloth_1x32_off_ms` | 0.5270 | 0.3291 | 53 |
| `bench.cloth_1x64_basic_ms` | 2.24 | 1.38 | 53 |
| `bench.cloth_1x64_full_ms` | 38.05 | 5.27 | 53 |
| `bench.cloth_cape_basic_ms` | 0.4986 | 0.3091 | 42 |
| `bench.cloth_cape_full_ms` | 8.19 | 4.63 | 42 |
| `bench.fracture_bake_cube_ms` | 7.14 | 4.62 | 109 |
| `bench.hair_1x100_straight_ms` | 0.2201 | 0.1071 | 48 |
| `bench.hair_1x160_braids_ms` | 0.5388 | 0.2855 | 30 |
| `bench.hair_1x160_cornrows_ms` | 0.0557 | 0.0317 | 30 |
| `bench.hair_1x200_3b_ms` | 0.5285 | 0.2824 | 33 |
| `bench.hair_1x200_4c_ms` | 0.3001 | 0.1606 | 33 |
| `bench.hair_1x200_bantu_ms` | 0.7226 | 0.3801 | 33 |
| `bench.hair_1x400_curly_ms` | 2.34 | 1.17 | 48 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 48 |
| `bench.hair_8x200_long_ms` | 4.55 | 2.16 | 48 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 109 |
| `bench.lua_think_50_hooks_ms` | 0.2803 | 0.1408 | 109 |
| `bench.net_snapshot_256_bodies_ms` | 0.0853 | 0.0503 | 109 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 109 |
| `bench.rigid_crates_400_ms` | 1.59 | 0.9864 | 109 |
| `bench.rigid_raycast_1000_ms` | 0.5064 | 0.3228 | 109 |
| `stress.crates.fps_avg` | 11.23 | 22.85 | 109 |
| `stress.crates.physics_avg_ms` | 0.4268 | 0.2801 | 109 |
| `stress.fps_1pct_low` | 5.82 | 16.28 | 109 |
| `stress.fps_avg` | 10.91 | 23.34 | 109 |
| `stress.frame_p99_ms` | 110 | 57.86 | 109 |
| `stress.impacts.fps_avg` | 9.30 | 19.00 | 109 |
| `stress.peak_rss_mb` | 413 | 251 | 109 |
| `stress.walk.fps_avg` | 12.45 | 29.14 | 109 |

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
