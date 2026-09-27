# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `7d33f4c919` (2026-09-27T17:23:16Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 79 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1845 | 0.1096 | 73 |
| `bench.audio_mix_32_voices_ms` | 0.0939 | 0.0614 | 79 |
| `bench.cloth_16x24_basic_ms` | 4.66 | 2.91 | 23 |
| `bench.cloth_16x24_full_ms` | 9.06 | 5.31 | 23 |
| `bench.cloth_1x32_basic_ms` | 0.5360 | 0.3282 | 23 |
| `bench.cloth_1x32_full_ms` | 1.15 | 0.6111 | 23 |
| `bench.cloth_1x32_off_ms` | 0.5341 | 0.3291 | 23 |
| `bench.cloth_1x64_basic_ms` | 2.25 | 1.38 | 23 |
| `bench.cloth_1x64_full_ms` | 38.97 | 5.27 | 23 |
| `bench.cloth_cape_basic_ms` | 0.5049 | 0.3091 | 12 |
| `bench.cloth_cape_full_ms` | 7.73 | 4.63 | 12 |
| `bench.fracture_bake_cube_ms` | 7.21 | 4.63 | 79 |
| `bench.hair_1x100_straight_ms` | 0.2010 | 0.1071 | 18 |
| `bench.hair_1x200_3b_ms` | 0.5327 | 0.4540 | 3 |
| `bench.hair_1x200_4c_ms` | 0.3026 | 0.2404 | 3 |
| `bench.hair_1x200_bantu_ms` | 0.7271 | 0.5872 | 3 |
| `bench.hair_1x400_curly_ms` | 2.16 | 1.17 | 18 |
| `bench.hair_1x400_long_ms` | 1.05 | 0.5540 | 18 |
| `bench.hair_8x200_long_ms` | 4.10 | 2.16 | 18 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9068 | 79 |
| `bench.lua_think_50_hooks_ms` | 0.2835 | 0.1408 | 79 |
| `bench.net_snapshot_256_bodies_ms` | 0.0859 | 0.0503 | 79 |
| `bench.particle_fluid_2000_ms` | 3.95 | 2.58 | 79 |
| `bench.rigid_crates_400_ms` | 1.62 | 0.9864 | 79 |
| `bench.rigid_raycast_1000_ms` | 0.4940 | 0.3228 | 79 |
| `stress.crates.fps_avg` | 10.95 | 22.85 | 79 |
| `stress.crates.physics_avg_ms` | 0.4405 | 0.2847 | 79 |
| `stress.fps_1pct_low` | 5.67 | 16.28 | 79 |
| `stress.fps_avg` | 10.74 | 23.34 | 79 |
| `stress.frame_p99_ms` | 114 | 57.86 | 79 |
| `stress.impacts.fps_avg` | 9.10 | 19.00 | 79 |
| `stress.peak_rss_mb` | 393 | 251 | 79 |
| `stress.walk.fps_avg` | 12.45 | 29.14 | 79 |

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
