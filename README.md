# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `ad5121ed15` (2026-09-28T08:04:47Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 104 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1846 | 0.1096 | 98 |
| `bench.audio_mix_32_voices_ms` | 0.0937 | 0.0614 | 104 |
| `bench.cloth_16x24_basic_ms` | 4.68 | 2.90 | 48 |
| `bench.cloth_16x24_full_ms` | 9.71 | 5.31 | 48 |
| `bench.cloth_1x32_basic_ms` | 0.5318 | 0.3282 | 48 |
| `bench.cloth_1x32_full_ms` | 1.20 | 0.6111 | 48 |
| `bench.cloth_1x32_off_ms` | 0.5323 | 0.3291 | 48 |
| `bench.cloth_1x64_basic_ms` | 2.25 | 1.38 | 48 |
| `bench.cloth_1x64_full_ms` | 38.70 | 5.27 | 48 |
| `bench.cloth_cape_basic_ms` | 0.5092 | 0.3091 | 37 |
| `bench.cloth_cape_full_ms` | 8.01 | 4.63 | 37 |
| `bench.fracture_bake_cube_ms` | 7.20 | 4.63 | 104 |
| `bench.hair_1x100_straight_ms` | 0.2210 | 0.1071 | 43 |
| `bench.hair_1x160_braids_ms` | 0.5444 | 0.2865 | 25 |
| `bench.hair_1x160_cornrows_ms` | 0.0560 | 0.0319 | 25 |
| `bench.hair_1x200_3b_ms` | 0.5339 | 0.2830 | 28 |
| `bench.hair_1x200_4c_ms` | 0.3011 | 0.1610 | 28 |
| `bench.hair_1x200_bantu_ms` | 0.7341 | 0.3813 | 28 |
| `bench.hair_1x400_curly_ms` | 2.37 | 1.17 | 43 |
| `bench.hair_1x400_long_ms` | 1.17 | 0.5540 | 43 |
| `bench.hair_8x200_long_ms` | 4.57 | 2.16 | 43 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 104 |
| `bench.lua_think_50_hooks_ms` | 0.2963 | 0.1408 | 104 |
| `bench.net_snapshot_256_bodies_ms` | 0.0882 | 0.0503 | 104 |
| `bench.particle_fluid_2000_ms` | 3.89 | 2.58 | 104 |
| `bench.rigid_crates_400_ms` | 1.59 | 0.9864 | 104 |
| `bench.rigid_raycast_1000_ms` | 0.4979 | 0.3228 | 104 |
| `stress.crates.fps_avg` | 10.94 | 22.85 | 104 |
| `stress.crates.physics_avg_ms` | 0.4075 | 0.2801 | 104 |
| `stress.fps_1pct_low` | 5.57 | 16.28 | 104 |
| `stress.fps_avg` | 10.76 | 23.34 | 104 |
| `stress.frame_p99_ms` | 112 | 57.86 | 104 |
| `stress.impacts.fps_avg` | 9.31 | 19.00 | 104 |
| `stress.peak_rss_mb` | 392 | 251 | 104 |
| `stress.walk.fps_avg` | 12.30 | 29.14 | 104 |

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
