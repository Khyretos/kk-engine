# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `ef5c691b97` (2026-09-27T19:07:04Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 83 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1519 | 0.1096 | 77 |
| `bench.audio_mix_32_voices_ms` | 0.0815 | 0.0614 | 83 |
| `bench.cloth_16x24_basic_ms` | 3.71 | 2.91 | 27 |
| `bench.cloth_16x24_full_ms` | 7.50 | 5.31 | 27 |
| `bench.cloth_1x32_basic_ms` | 0.4225 | 0.3282 | 27 |
| `bench.cloth_1x32_full_ms` | 0.9085 | 0.6111 | 27 |
| `bench.cloth_1x32_off_ms` | 0.4235 | 0.3291 | 27 |
| `bench.cloth_1x64_basic_ms` | 1.79 | 1.38 | 27 |
| `bench.cloth_1x64_full_ms` | 33.60 | 5.27 | 27 |
| `bench.cloth_cape_basic_ms` | 0.3945 | 0.3091 | 16 |
| `bench.cloth_cape_full_ms` | 6.71 | 4.63 | 16 |
| `bench.fracture_bake_cube_ms` | 6.22 | 4.63 | 83 |
| `bench.hair_1x100_straight_ms` | 0.1724 | 0.1071 | 22 |
| `bench.hair_1x160_braids_ms` | 0.4129 | 0.4129 | 4 |
| `bench.hair_1x160_cornrows_ms` | 0.0447 | 0.0447 | 4 |
| `bench.hair_1x200_3b_ms` | 0.4124 | 0.4124 | 7 |
| `bench.hair_1x200_4c_ms` | 0.2338 | 0.2338 | 7 |
| `bench.hair_1x200_bantu_ms` | 0.5554 | 0.5554 | 7 |
| `bench.hair_1x400_curly_ms` | 1.85 | 1.17 | 22 |
| `bench.hair_1x400_long_ms` | 0.9096 | 0.5540 | 22 |
| `bench.hair_8x200_long_ms` | 3.61 | 2.16 | 22 |
| `bench.impact_synth_8_materials_ms` | 0.9467 | 0.9068 | 83 |
| `bench.lua_think_50_hooks_ms` | 0.2117 | 0.1408 | 83 |
| `bench.net_snapshot_256_bodies_ms` | 0.0652 | 0.0503 | 83 |
| `bench.particle_fluid_2000_ms` | 3.35 | 2.58 | 83 |
| `bench.rigid_crates_400_ms` | 1.38 | 0.9864 | 83 |
| `bench.rigid_raycast_1000_ms` | 0.3630 | 0.3228 | 83 |
| `stress.crates.fps_avg` | 13.73 | 22.85 | 83 |
| `stress.crates.physics_avg_ms` | 0.3797 | 0.2847 | 83 |
| `stress.fps_1pct_low` | 7.80 | 16.28 | 83 |
| `stress.fps_avg` | 13.66 | 23.34 | 83 |
| `stress.frame_p99_ms` | 90.58 | 57.86 | 83 |
| `stress.impacts.fps_avg` | 11.54 | 19.00 | 83 |
| `stress.peak_rss_mb` | 393 | 251 | 83 |
| `stress.walk.fps_avg` | 16.14 | 29.14 | 83 |

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
