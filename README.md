# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `48702ed335` (2026-09-27T16:32:55Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 77 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1564 | 0.1096 | 71 |
| `bench.audio_mix_32_voices_ms` | 0.0829 | 0.0614 | 77 |
| `bench.cloth_16x24_basic_ms` | 4.04 | 2.91 | 21 |
| `bench.cloth_16x24_full_ms` | 8.01 | 5.31 | 21 |
| `bench.cloth_1x32_basic_ms` | 0.4632 | 0.3282 | 21 |
| `bench.cloth_1x32_full_ms` | 0.9928 | 0.6111 | 21 |
| `bench.cloth_1x32_off_ms` | 0.4585 | 0.3291 | 21 |
| `bench.cloth_1x64_basic_ms` | 1.95 | 1.38 | 21 |
| `bench.cloth_1x64_full_ms` | 41.87 | 5.27 | 21 |
| `bench.cloth_cape_basic_ms` | 0.4317 | 0.3091 | 10 |
| `bench.cloth_cape_full_ms` | 9.19 | 4.63 | 10 |
| `bench.fracture_bake_cube_ms` | 7.08 | 4.63 | 77 |
| `bench.hair_1x100_straight_ms` | 0.1641 | 0.1071 | 16 |
| `bench.hair_1x200_3b_ms` | 0.4540 | 0.4540 | 1 |
| `bench.hair_1x200_4c_ms` | 0.2404 | 0.2404 | 1 |
| `bench.hair_1x200_bantu_ms` | 0.5872 | 0.5872 | 1 |
| `bench.hair_1x400_curly_ms` | 1.69 | 1.17 | 16 |
| `bench.hair_1x400_long_ms` | 0.8605 | 0.5540 | 16 |
| `bench.hair_8x200_long_ms` | 3.46 | 2.16 | 16 |
| `bench.impact_synth_8_materials_ms` | 1.43 | 0.9068 | 77 |
| `bench.lua_think_50_hooks_ms` | 0.2493 | 0.1408 | 77 |
| `bench.net_snapshot_256_bodies_ms` | 0.0981 | 0.0503 | 77 |
| `bench.particle_fluid_2000_ms` | 4.12 | 2.58 | 77 |
| `bench.rigid_crates_400_ms` | 1.58 | 0.9864 | 77 |
| `bench.rigid_raycast_1000_ms` | 0.5113 | 0.3228 | 77 |
| `stress.crates.fps_avg` | 11.94 | 22.85 | 77 |
| `stress.crates.physics_avg_ms` | 0.4656 | 0.2847 | 77 |
| `stress.fps_1pct_low` | 6.87 | 16.28 | 77 |
| `stress.fps_avg` | 12.08 | 23.34 | 77 |
| `stress.frame_p99_ms` | 103 | 57.86 | 77 |
| `stress.impacts.fps_avg` | 10.05 | 19.00 | 77 |
| `stress.peak_rss_mb` | 388 | 251 | 77 |
| `stress.walk.fps_avg` | 14.69 | 29.14 | 77 |

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
