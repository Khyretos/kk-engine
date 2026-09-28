# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `46ad41f9f7` (2026-09-28T14:59:02Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 119 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1103 | 0.1096 | 113 |
| `bench.audio_mix_32_voices_ms` | 0.0670 | 0.0614 | 119 |
| `bench.cloth_16x24_basic_ms` | 3.04 | 2.90 | 63 |
| `bench.cloth_16x24_full_ms` | 6.23 | 5.31 | 63 |
| `bench.cloth_1x32_basic_ms` | 0.3310 | 0.3282 | 63 |
| `bench.cloth_1x32_full_ms` | 0.7391 | 0.6111 | 63 |
| `bench.cloth_1x32_off_ms` | 0.3307 | 0.3291 | 63 |
| `bench.cloth_1x64_basic_ms` | 1.42 | 1.38 | 63 |
| `bench.cloth_1x64_full_ms` | 25.65 | 5.27 | 63 |
| `bench.cloth_cape_basic_ms` | 0.3167 | 0.3091 | 52 |
| `bench.cloth_cape_full_ms` | 5.36 | 4.63 | 52 |
| `bench.fracture_bake_cube_ms` | 4.93 | 4.62 | 119 |
| `bench.hair_1x100_straight_ms` | 0.1205 | 0.1071 | 58 |
| `bench.hair_1x160_braids_ms` | 0.2968 | 0.2855 | 40 |
| `bench.hair_1x160_cornrows_ms` | 0.0327 | 0.0317 | 40 |
| `bench.hair_1x200_3b_ms` | 0.2821 | 0.2821 | 43 |
| `bench.hair_1x200_4c_ms` | 0.1600 | 0.1600 | 43 |
| `bench.hair_1x200_bantu_ms` | 0.3786 | 0.3786 | 43 |
| `bench.hair_1x400_curly_ms` | 1.33 | 1.17 | 58 |
| `bench.hair_1x400_long_ms` | 0.6362 | 0.5540 | 58 |
| `bench.hair_8x200_long_ms` | 2.46 | 2.16 | 58 |
| `bench.impact_synth_8_materials_ms` | 0.9131 | 0.9068 | 119 |
| `bench.lua_think_50_hooks_ms` | 0.1414 | 0.1408 | 119 |
| `bench.net_snapshot_256_bodies_ms` | 0.0503 | 0.0503 | 119 |
| `bench.particle_fluid_2000_ms` | 2.62 | 2.58 | 119 |
| `bench.rigid_crates_400_ms` | 1.01 | 0.9864 | 119 |
| `bench.rigid_raycast_1000_ms` | 0.3387 | 0.3228 | 119 |
| `stress.crates.fps_avg` | 18.27 | 22.85 | 119 |
| `stress.crates.physics_avg_ms` | 0.3121 | 0.2801 | 119 |
| `stress.fps_1pct_low` | 11.25 | 16.28 | 119 |
| `stress.fps_avg` | 18.29 | 23.34 | 119 |
| `stress.frame_p99_ms` | 67.09 | 57.86 | 119 |
| `stress.impacts.fps_avg` | 15.67 | 19.00 | 119 |
| `stress.peak_rss_mb` | 395 | 251 | 119 |
| `stress.walk.fps_avg` | 21.47 | 29.14 | 119 |

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
