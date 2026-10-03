# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `83234f9059` (2026-10-03T11:36:19Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 123 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1847 | 0.1096 | 117 |
| `bench.audio_mix_32_voices_ms` | 0.0929 | 0.0614 | 123 |
| `bench.cloth_16x24_basic_ms` | 4.69 | 2.90 | 67 |
| `bench.cloth_16x24_full_ms` | 10.07 | 5.31 | 67 |
| `bench.cloth_1x32_basic_ms` | 0.5329 | 0.3282 | 67 |
| `bench.cloth_1x32_full_ms` | 1.22 | 0.6111 | 67 |
| `bench.cloth_1x32_off_ms` | 0.5296 | 0.3291 | 67 |
| `bench.cloth_1x64_basic_ms` | 2.24 | 1.38 | 67 |
| `bench.cloth_1x64_full_ms` | 40.12 | 5.27 | 67 |
| `bench.cloth_cape_basic_ms` | 0.5051 | 0.3091 | 56 |
| `bench.cloth_cape_full_ms` | 8.28 | 4.63 | 56 |
| `bench.fracture_bake_cube_ms` | 7.16 | 4.62 | 123 |
| `bench.hair_1x100_straight_ms` | 0.2216 | 0.1071 | 62 |
| `bench.hair_1x160_braids_ms` | 0.5429 | 0.2855 | 44 |
| `bench.hair_1x160_cornrows_ms` | 0.0560 | 0.0317 | 44 |
| `bench.hair_1x200_3b_ms` | 0.5414 | 0.2821 | 47 |
| `bench.hair_1x200_4c_ms` | 0.3043 | 0.1600 | 47 |
| `bench.hair_1x200_bantu_ms` | 0.7327 | 0.3786 | 47 |
| `bench.hair_1x400_curly_ms` | 2.38 | 1.17 | 62 |
| `bench.hair_1x400_long_ms` | 1.18 | 0.5540 | 62 |
| `bench.hair_8x200_long_ms` | 4.65 | 2.16 | 62 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 123 |
| `bench.lua_think_50_hooks_ms` | 0.2755 | 0.1408 | 123 |
| `bench.net_snapshot_256_bodies_ms` | 0.0852 | 0.0503 | 123 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 123 |
| `bench.rigid_crates_400_ms` | 1.62 | 0.9864 | 123 |
| `bench.rigid_raycast_1000_ms` | 0.4961 | 0.3228 | 123 |
| `stress.crates.fps_avg` | 11.01 | 22.85 | 123 |
| `stress.crates.physics_avg_ms` | 0.4380 | 0.2801 | 123 |
| `stress.fps_1pct_low` | 5.77 | 16.28 | 123 |
| `stress.fps_avg` | 10.73 | 23.34 | 123 |
| `stress.frame_p99_ms` | 112 | 57.86 | 123 |
| `stress.impacts.fps_avg` | 9.13 | 19.00 | 123 |
| `stress.peak_rss_mb` | 416 | 251 | 123 |
| `stress.walk.fps_avg` | 12.30 | 29.14 | 123 |

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
