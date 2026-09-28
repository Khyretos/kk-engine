# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `f336057f0d` (2026-09-28T04:13:29+02:00) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 91 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1567 | 0.1096 | 85 |
| `bench.audio_mix_32_voices_ms` | 0.0820 | 0.0614 | 91 |
| `bench.cloth_16x24_basic_ms` | 4.04 | 2.90 | 35 |
| `bench.cloth_16x24_full_ms` | 8.33 | 5.31 | 35 |
| `bench.cloth_1x32_basic_ms` | 0.4593 | 0.3282 | 35 |
| `bench.cloth_1x32_full_ms` | 1.02 | 0.6111 | 35 |
| `bench.cloth_1x32_off_ms` | 0.4605 | 0.3291 | 35 |
| `bench.cloth_1x64_basic_ms` | 1.95 | 1.38 | 35 |
| `bench.cloth_1x64_full_ms` | 40.98 | 5.27 | 35 |
| `bench.cloth_cape_basic_ms` | 0.4327 | 0.3091 | 24 |
| `bench.cloth_cape_full_ms` | 8.18 | 4.63 | 24 |
| `bench.fracture_bake_cube_ms` | 6.96 | 4.63 | 91 |
| `bench.hair_1x100_straight_ms` | 0.1858 | 0.1071 | 30 |
| `bench.hair_1x160_braids_ms` | 0.4417 | 0.3205 | 12 |
| `bench.hair_1x160_cornrows_ms` | 0.0471 | 0.0327 | 12 |
| `bench.hair_1x200_3b_ms` | 0.4384 | 0.3160 | 15 |
| `bench.hair_1x200_4c_ms` | 0.2396 | 0.1725 | 15 |
| `bench.hair_1x200_bantu_ms` | 0.5865 | 0.4257 | 15 |
| `bench.hair_1x400_curly_ms` | 1.92 | 1.17 | 30 |
| `bench.hair_1x400_long_ms` | 0.9965 | 0.5540 | 30 |
| `bench.hair_8x200_long_ms` | 4.01 | 2.16 | 30 |
| `bench.impact_synth_8_materials_ms` | 1.39 | 0.9068 | 91 |
| `bench.lua_think_50_hooks_ms` | 0.2429 | 0.1408 | 91 |
| `bench.net_snapshot_256_bodies_ms` | 0.0980 | 0.0503 | 91 |
| `bench.particle_fluid_2000_ms` | 4.15 | 2.58 | 91 |
| `bench.rigid_crates_400_ms` | 1.59 | 0.9864 | 91 |
| `bench.rigid_raycast_1000_ms` | 0.5121 | 0.3228 | 91 |
| `stress.crates.fps_avg` | 11.92 | 22.85 | 91 |
| `stress.crates.physics_avg_ms` | 0.4595 | 0.2847 | 91 |
| `stress.fps_1pct_low` | 7.24 | 16.28 | 91 |
| `stress.fps_avg` | 11.90 | 23.34 | 91 |
| `stress.frame_p99_ms` | 103 | 57.86 | 91 |
| `stress.impacts.fps_avg` | 10.00 | 19.00 | 91 |
| `stress.peak_rss_mb` | 390 | 251 | 91 |
| `stress.walk.fps_avg` | 14.15 | 29.14 | 91 |

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
