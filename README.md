# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `944378f3c3` (2026-09-28T05:04:00Z) on Intel(R) Xeon(R) 6973P-C, llvmpipe (LLVM 20.1.2, 256 bits). 95 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1186 | 0.1096 | 89 |
| `bench.audio_mix_32_voices_ms` | 0.0628 | 0.0614 | 95 |
| `bench.cloth_16x24_basic_ms` | 3.31 | 2.90 | 39 |
| `bench.cloth_16x24_full_ms` | 6.23 | 5.31 | 39 |
| `bench.cloth_1x32_basic_ms` | 0.3374 | 0.3282 | 39 |
| `bench.cloth_1x32_full_ms` | 0.7761 | 0.6111 | 39 |
| `bench.cloth_1x32_off_ms` | 0.3394 | 0.3291 | 39 |
| `bench.cloth_1x64_basic_ms` | 1.40 | 1.38 | 39 |
| `bench.cloth_1x64_full_ms` | 31.67 | 5.27 | 39 |
| `bench.cloth_cape_basic_ms` | 0.3176 | 0.3091 | 28 |
| `bench.cloth_cape_full_ms` | 6.10 | 4.63 | 28 |
| `bench.fracture_bake_cube_ms` | 5.16 | 4.63 | 95 |
| `bench.hair_1x100_straight_ms` | 0.1352 | 0.1071 | 34 |
| `bench.hair_1x160_braids_ms` | 0.3271 | 0.3104 | 16 |
| `bench.hair_1x160_cornrows_ms` | 0.0335 | 0.0327 | 16 |
| `bench.hair_1x200_3b_ms` | 0.3224 | 0.3052 | 19 |
| `bench.hair_1x200_4c_ms` | 0.1766 | 0.1725 | 19 |
| `bench.hair_1x200_bantu_ms` | 0.4329 | 0.4102 | 19 |
| `bench.hair_1x400_curly_ms` | 1.60 | 1.17 | 34 |
| `bench.hair_1x400_long_ms` | 0.7570 | 0.5540 | 34 |
| `bench.hair_8x200_long_ms` | 3.07 | 2.16 | 34 |
| `bench.impact_synth_8_materials_ms` | 1.10 | 0.9068 | 95 |
| `bench.lua_think_50_hooks_ms` | 0.1737 | 0.1408 | 95 |
| `bench.net_snapshot_256_bodies_ms` | 0.0738 | 0.0503 | 95 |
| `bench.particle_fluid_2000_ms` | 3.11 | 2.58 | 95 |
| `bench.rigid_crates_400_ms` | 1.13 | 0.9864 | 95 |
| `bench.rigid_raycast_1000_ms` | 0.3782 | 0.3228 | 95 |
| `stress.crates.fps_avg` | 14.50 | 22.85 | 95 |
| `stress.crates.physics_avg_ms` | 0.4205 | 0.2847 | 95 |
| `stress.fps_1pct_low` | 8.04 | 16.28 | 95 |
| `stress.fps_avg` | 14.57 | 23.34 | 95 |
| `stress.frame_p99_ms` | 98.79 | 57.86 | 95 |
| `stress.impacts.fps_avg` | 12.14 | 19.00 | 95 |
| `stress.peak_rss_mb` | 411 | 251 | 95 |
| `stress.walk.fps_avg` | 17.56 | 29.14 | 95 |

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
