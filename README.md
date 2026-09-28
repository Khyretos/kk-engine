# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `cfc412b57c` (2026-09-28T16:38:12Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 120 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1896 | 0.1096 | 114 |
| `bench.audio_mix_32_voices_ms` | 0.1045 | 0.0614 | 120 |
| `bench.cloth_16x24_basic_ms` | 4.78 | 2.90 | 64 |
| `bench.cloth_16x24_full_ms` | 10.36 | 5.31 | 64 |
| `bench.cloth_1x32_basic_ms` | 0.5511 | 0.3282 | 64 |
| `bench.cloth_1x32_full_ms` | 1.28 | 0.6111 | 64 |
| `bench.cloth_1x32_off_ms` | 0.5457 | 0.3291 | 64 |
| `bench.cloth_1x64_basic_ms` | 2.30 | 1.38 | 64 |
| `bench.cloth_1x64_full_ms` | 43.51 | 5.27 | 64 |
| `bench.cloth_cape_basic_ms` | 0.5149 | 0.3091 | 53 |
| `bench.cloth_cape_full_ms` | 9.10 | 4.63 | 53 |
| `bench.fracture_bake_cube_ms` | 7.93 | 4.62 | 120 |
| `bench.hair_1x100_straight_ms` | 0.2224 | 0.1071 | 59 |
| `bench.hair_1x160_braids_ms` | 0.5266 | 0.2855 | 41 |
| `bench.hair_1x160_cornrows_ms` | 0.0564 | 0.0317 | 41 |
| `bench.hair_1x200_3b_ms` | 0.5253 | 0.2821 | 44 |
| `bench.hair_1x200_4c_ms` | 0.2956 | 0.1600 | 44 |
| `bench.hair_1x200_bantu_ms` | 0.7068 | 0.3786 | 44 |
| `bench.hair_1x400_curly_ms` | 2.39 | 1.17 | 59 |
| `bench.hair_1x400_long_ms` | 1.17 | 0.5540 | 59 |
| `bench.hair_8x200_long_ms` | 4.52 | 2.16 | 59 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 0.9068 | 120 |
| `bench.lua_think_50_hooks_ms` | 0.2686 | 0.1408 | 120 |
| `bench.net_snapshot_256_bodies_ms` | 0.0838 | 0.0503 | 120 |
| `bench.particle_fluid_2000_ms` | 4.36 | 2.58 | 120 |
| `bench.rigid_crates_400_ms` | 1.75 | 0.9864 | 120 |
| `bench.rigid_raycast_1000_ms` | 0.4492 | 0.3228 | 120 |
| `stress.crates.fps_avg` | 11.05 | 22.85 | 120 |
| `stress.crates.physics_avg_ms` | 0.4425 | 0.2801 | 120 |
| `stress.fps_1pct_low` | 5.89 | 16.28 | 120 |
| `stress.fps_avg` | 10.75 | 23.34 | 120 |
| `stress.frame_p99_ms` | 112 | 57.86 | 120 |
| `stress.impacts.fps_avg` | 9.17 | 19.00 | 120 |
| `stress.peak_rss_mb` | 415 | 251 | 120 |
| `stress.walk.fps_avg` | 12.28 | 29.14 | 120 |

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
