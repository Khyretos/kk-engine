# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `abf532c6c8` (2026-09-28T01:32:17Z) on Intel(R) Xeon(R) 6973P-C, llvmpipe (LLVM 20.1.2, 256 bits). 89 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1159 | 0.1096 | 83 |
| `bench.audio_mix_32_voices_ms` | 0.0615 | 0.0614 | 89 |
| `bench.cloth_16x24_basic_ms` | 2.90 | 2.90 | 33 |
| `bench.cloth_16x24_full_ms` | 6.15 | 5.31 | 33 |
| `bench.cloth_1x32_basic_ms` | 0.3337 | 0.3282 | 33 |
| `bench.cloth_1x32_full_ms` | 0.7486 | 0.6111 | 33 |
| `bench.cloth_1x32_off_ms` | 0.3325 | 0.3291 | 33 |
| `bench.cloth_1x64_basic_ms` | 1.40 | 1.38 | 33 |
| `bench.cloth_1x64_full_ms` | 29.33 | 5.27 | 33 |
| `bench.cloth_cape_basic_ms` | 0.3098 | 0.3091 | 22 |
| `bench.cloth_cape_full_ms` | 5.87 | 4.63 | 22 |
| `bench.fracture_bake_cube_ms` | 5.16 | 4.63 | 89 |
| `bench.hair_1x100_straight_ms` | 0.1318 | 0.1071 | 28 |
| `bench.hair_1x160_braids_ms` | 0.3205 | 0.3205 | 10 |
| `bench.hair_1x160_cornrows_ms` | 0.0327 | 0.0327 | 10 |
| `bench.hair_1x200_3b_ms` | 0.3160 | 0.3160 | 13 |
| `bench.hair_1x200_4c_ms` | 0.1725 | 0.1725 | 13 |
| `bench.hair_1x200_bantu_ms` | 0.4257 | 0.4257 | 13 |
| `bench.hair_1x400_curly_ms` | 1.43 | 1.17 | 28 |
| `bench.hair_1x400_long_ms` | 0.7366 | 0.5540 | 28 |
| `bench.hair_8x200_long_ms` | 3.01 | 2.16 | 28 |
| `bench.impact_synth_8_materials_ms` | 1.07 | 0.9068 | 89 |
| `bench.lua_think_50_hooks_ms` | 0.1694 | 0.1408 | 89 |
| `bench.net_snapshot_256_bodies_ms` | 0.0716 | 0.0503 | 89 |
| `bench.particle_fluid_2000_ms` | 3.02 | 2.58 | 89 |
| `bench.rigid_crates_400_ms` | 1.14 | 0.9864 | 89 |
| `bench.rigid_raycast_1000_ms` | 0.4748 | 0.3228 | 89 |
| `stress.crates.fps_avg` | 15.62 | 22.85 | 89 |
| `stress.crates.physics_avg_ms` | 0.3792 | 0.2847 | 89 |
| `stress.fps_1pct_low` | 8.81 | 16.28 | 89 |
| `stress.fps_avg` | 15.48 | 23.34 | 89 |
| `stress.frame_p99_ms` | 86.90 | 57.86 | 89 |
| `stress.impacts.fps_avg` | 12.88 | 19.00 | 89 |
| `stress.peak_rss_mb` | 393 | 251 | 89 |
| `stress.walk.fps_avg` | 18.42 | 29.14 | 89 |

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
