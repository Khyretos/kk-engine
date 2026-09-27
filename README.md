# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `920a93286f` (2026-09-27T17:01:31Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 78 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1905 | 0.1096 | 72 |
| `bench.audio_mix_32_voices_ms` | 0.0949 | 0.0614 | 78 |
| `bench.cloth_16x24_basic_ms` | 4.62 | 2.91 | 22 |
| `bench.cloth_16x24_full_ms` | 9.14 | 5.31 | 22 |
| `bench.cloth_1x32_basic_ms` | 0.5299 | 0.3282 | 22 |
| `bench.cloth_1x32_full_ms` | 1.13 | 0.6111 | 22 |
| `bench.cloth_1x32_off_ms` | 0.5273 | 0.3291 | 22 |
| `bench.cloth_1x64_basic_ms` | 2.22 | 1.38 | 22 |
| `bench.cloth_1x64_full_ms` | 47.80 | 5.27 | 22 |
| `bench.cloth_cape_basic_ms` | 0.4994 | 0.3091 | 11 |
| `bench.cloth_cape_full_ms` | 11.30 | 4.63 | 11 |
| `bench.fracture_bake_cube_ms` | 7.21 | 4.63 | 78 |
| `bench.hair_1x100_straight_ms` | 0.2005 | 0.1071 | 17 |
| `bench.hair_1x200_3b_ms` | 0.5283 | 0.4540 | 2 |
| `bench.hair_1x200_4c_ms` | 0.3001 | 0.2404 | 2 |
| `bench.hair_1x200_bantu_ms` | 0.7225 | 0.5872 | 2 |
| `bench.hair_1x400_curly_ms` | 2.15 | 1.17 | 17 |
| `bench.hair_1x400_long_ms` | 1.05 | 0.5540 | 17 |
| `bench.hair_8x200_long_ms` | 4.09 | 2.16 | 17 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9068 | 78 |
| `bench.lua_think_50_hooks_ms` | 0.3007 | 0.1408 | 78 |
| `bench.net_snapshot_256_bodies_ms` | 0.0859 | 0.0503 | 78 |
| `bench.particle_fluid_2000_ms` | 3.97 | 2.58 | 78 |
| `bench.rigid_crates_400_ms` | 1.61 | 0.9864 | 78 |
| `bench.rigid_raycast_1000_ms` | 0.4896 | 0.3228 | 78 |
| `stress.crates.fps_avg` | 10.75 | 22.85 | 78 |
| `stress.crates.physics_avg_ms` | 0.4301 | 0.2847 | 78 |
| `stress.fps_1pct_low` | 5.32 | 16.28 | 78 |
| `stress.fps_avg` | 10.67 | 23.34 | 78 |
| `stress.frame_p99_ms` | 115 | 57.86 | 78 |
| `stress.impacts.fps_avg` | 8.99 | 19.00 | 78 |
| `stress.peak_rss_mb` | 392 | 251 | 78 |
| `stress.walk.fps_avg` | 12.59 | 29.14 | 78 |

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
