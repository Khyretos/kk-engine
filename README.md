# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `41044b5c96` (2026-10-03T11:45:51Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 125 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1865 | 0.1096 | 119 |
| `bench.audio_mix_32_voices_ms` | 0.1040 | 0.0614 | 125 |
| `bench.cloth_16x24_basic_ms` | 4.79 | 2.90 | 69 |
| `bench.cloth_16x24_full_ms` | 10.44 | 5.31 | 69 |
| `bench.cloth_1x32_basic_ms` | 0.5454 | 0.3282 | 69 |
| `bench.cloth_1x32_full_ms` | 1.28 | 0.6111 | 69 |
| `bench.cloth_1x32_off_ms` | 0.5439 | 0.3291 | 69 |
| `bench.cloth_1x64_basic_ms` | 2.31 | 1.38 | 69 |
| `bench.cloth_1x64_full_ms` | 19.17 | 5.27 | 69 |
| `bench.cloth_cape_basic_ms` | 0.5129 | 0.3091 | 58 |
| `bench.cloth_cape_full_ms` | 9.08 | 4.63 | 58 |
| `bench.fracture_bake_cube_ms` | 7.92 | 4.62 | 125 |
| `bench.hair_1x100_straight_ms` | 0.2177 | 0.1071 | 64 |
| `bench.hair_1x160_braids_ms` | 0.5277 | 0.2855 | 46 |
| `bench.hair_1x160_cornrows_ms` | 0.0564 | 0.0317 | 46 |
| `bench.hair_1x200_3b_ms` | 0.5266 | 0.2821 | 49 |
| `bench.hair_1x200_4c_ms` | 0.2969 | 0.1600 | 49 |
| `bench.hair_1x200_bantu_ms` | 0.7143 | 0.3786 | 49 |
| `bench.hair_1x400_curly_ms` | 2.35 | 1.17 | 64 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 64 |
| `bench.hair_8x200_long_ms` | 4.53 | 2.16 | 64 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 0.9068 | 125 |
| `bench.lua_think_50_hooks_ms` | 0.2579 | 0.1408 | 125 |
| `bench.net_snapshot_256_bodies_ms` | 0.0859 | 0.0503 | 125 |
| `bench.particle_fluid_2000_ms` | 4.33 | 2.58 | 125 |
| `bench.rigid_crates_400_ms` | 1.79 | 0.9864 | 125 |
| `bench.rigid_raycast_1000_ms` | 0.4516 | 0.3228 | 125 |
| `stress.crates.fps_avg` | 11.04 | 22.85 | 125 |
| `stress.crates.physics_avg_ms` | 0.4199 | 0.2801 | 125 |
| `stress.fps_1pct_low` | 5.89 | 16.28 | 125 |
| `stress.fps_avg` | 10.76 | 23.34 | 125 |
| `stress.frame_p99_ms` | 111 | 57.86 | 125 |
| `stress.impacts.fps_avg` | 9.23 | 19.00 | 125 |
| `stress.peak_rss_mb` | 392 | 251 | 125 |
| `stress.walk.fps_avg` | 12.29 | 29.14 | 125 |

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
