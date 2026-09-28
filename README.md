# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `e0936b968a` (2026-09-28T04:27:16Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 92 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1858 | 0.1096 | 86 |
| `bench.audio_mix_32_voices_ms` | 0.0936 | 0.0614 | 92 |
| `bench.cloth_16x24_basic_ms` | 4.65 | 2.90 | 36 |
| `bench.cloth_16x24_full_ms` | 9.04 | 5.31 | 36 |
| `bench.cloth_1x32_basic_ms` | 0.5284 | 0.3282 | 36 |
| `bench.cloth_1x32_full_ms` | 1.13 | 0.6111 | 36 |
| `bench.cloth_1x32_off_ms` | 0.5286 | 0.3291 | 36 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 36 |
| `bench.cloth_1x64_full_ms` | 38.76 | 5.27 | 36 |
| `bench.cloth_cape_basic_ms` | 0.5059 | 0.3091 | 25 |
| `bench.cloth_cape_full_ms` | 7.77 | 4.63 | 25 |
| `bench.fracture_bake_cube_ms` | 7.15 | 4.63 | 92 |
| `bench.hair_1x100_straight_ms` | 0.2205 | 0.1071 | 31 |
| `bench.hair_1x160_braids_ms` | 0.5395 | 0.3205 | 13 |
| `bench.hair_1x160_cornrows_ms` | 0.0559 | 0.0327 | 13 |
| `bench.hair_1x200_3b_ms` | 0.5311 | 0.3160 | 16 |
| `bench.hair_1x200_4c_ms` | 0.3015 | 0.1725 | 16 |
| `bench.hair_1x200_bantu_ms` | 0.7238 | 0.4257 | 16 |
| `bench.hair_1x400_curly_ms` | 2.34 | 1.17 | 31 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 31 |
| `bench.hair_8x200_long_ms` | 4.55 | 2.16 | 31 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 92 |
| `bench.lua_think_50_hooks_ms` | 0.2756 | 0.1408 | 92 |
| `bench.net_snapshot_256_bodies_ms` | 0.0885 | 0.0503 | 92 |
| `bench.particle_fluid_2000_ms` | 3.87 | 2.58 | 92 |
| `bench.rigid_crates_400_ms` | 1.70 | 0.9864 | 92 |
| `bench.rigid_raycast_1000_ms` | 0.5041 | 0.3228 | 92 |
| `stress.crates.fps_avg` | 11.18 | 22.85 | 92 |
| `stress.crates.physics_avg_ms` | 0.4156 | 0.2847 | 92 |
| `stress.fps_1pct_low` | 5.87 | 16.28 | 92 |
| `stress.fps_avg` | 10.90 | 23.34 | 92 |
| `stress.frame_p99_ms` | 110 | 57.86 | 92 |
| `stress.impacts.fps_avg` | 9.24 | 19.00 | 92 |
| `stress.peak_rss_mb` | 394 | 251 | 92 |
| `stress.walk.fps_avg` | 12.52 | 29.14 | 92 |

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
