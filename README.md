# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `1f712b640d` (2026-09-27T19:47:36Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 86 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1560 | 0.1096 | 80 |
| `bench.audio_mix_32_voices_ms` | 0.0824 | 0.0614 | 86 |
| `bench.cloth_16x24_basic_ms` | 4.03 | 2.91 | 30 |
| `bench.cloth_16x24_full_ms` | 8.32 | 5.31 | 30 |
| `bench.cloth_1x32_basic_ms` | 0.4612 | 0.3282 | 30 |
| `bench.cloth_1x32_full_ms` | 1.11 | 0.6111 | 30 |
| `bench.cloth_1x32_off_ms` | 0.4603 | 0.3291 | 30 |
| `bench.cloth_1x64_basic_ms` | 1.96 | 1.38 | 30 |
| `bench.cloth_1x64_full_ms` | 40.99 | 5.27 | 30 |
| `bench.cloth_cape_basic_ms` | 0.4322 | 0.3091 | 19 |
| `bench.cloth_cape_full_ms` | 8.33 | 4.63 | 19 |
| `bench.fracture_bake_cube_ms` | 6.95 | 4.63 | 86 |
| `bench.hair_1x100_straight_ms` | 0.1834 | 0.1071 | 25 |
| `bench.hair_1x160_braids_ms` | 0.4413 | 0.4129 | 7 |
| `bench.hair_1x160_cornrows_ms` | 0.0458 | 0.0447 | 7 |
| `bench.hair_1x200_3b_ms` | 0.4365 | 0.4124 | 10 |
| `bench.hair_1x200_4c_ms` | 0.2396 | 0.2338 | 10 |
| `bench.hair_1x200_bantu_ms` | 0.5874 | 0.5554 | 10 |
| `bench.hair_1x400_curly_ms` | 1.92 | 1.17 | 25 |
| `bench.hair_1x400_long_ms` | 0.9938 | 0.5540 | 25 |
| `bench.hair_8x200_long_ms` | 4.01 | 2.16 | 25 |
| `bench.impact_synth_8_materials_ms` | 1.39 | 0.9068 | 86 |
| `bench.lua_think_50_hooks_ms` | 0.2371 | 0.1408 | 86 |
| `bench.net_snapshot_256_bodies_ms` | 0.0960 | 0.0503 | 86 |
| `bench.particle_fluid_2000_ms` | 4.16 | 2.58 | 86 |
| `bench.rigid_crates_400_ms` | 1.62 | 0.9864 | 86 |
| `bench.rigid_raycast_1000_ms` | 0.5147 | 0.3228 | 86 |
| `stress.crates.fps_avg` | 12.19 | 22.85 | 86 |
| `stress.crates.physics_avg_ms` | 0.4924 | 0.2847 | 86 |
| `stress.fps_1pct_low` | 7.32 | 16.28 | 86 |
| `stress.fps_avg` | 12.01 | 23.34 | 86 |
| `stress.frame_p99_ms` | 103 | 57.86 | 86 |
| `stress.impacts.fps_avg` | 10.09 | 19.00 | 86 |
| `stress.peak_rss_mb` | 414 | 251 | 86 |
| `stress.walk.fps_avg` | 14.09 | 29.14 | 86 |

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
