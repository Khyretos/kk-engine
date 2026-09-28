# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `6c00d7a9b5` (2026-09-28T06:23:38Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 101 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1140 | 0.1096 | 95 |
| `bench.audio_mix_32_voices_ms` | 0.0699 | 0.0614 | 101 |
| `bench.cloth_16x24_basic_ms` | 2.93 | 2.90 | 45 |
| `bench.cloth_16x24_full_ms` | 5.88 | 5.31 | 45 |
| `bench.cloth_1x32_basic_ms` | 0.3309 | 0.3282 | 45 |
| `bench.cloth_1x32_full_ms` | 0.7217 | 0.6111 | 45 |
| `bench.cloth_1x32_off_ms` | 0.3307 | 0.3291 | 45 |
| `bench.cloth_1x64_basic_ms` | 1.39 | 1.38 | 45 |
| `bench.cloth_1x64_full_ms` | 25.03 | 5.27 | 45 |
| `bench.cloth_cape_basic_ms` | 0.3333 | 0.3091 | 34 |
| `bench.cloth_cape_full_ms` | 5.44 | 4.63 | 34 |
| `bench.fracture_bake_cube_ms` | 4.68 | 4.63 | 101 |
| `bench.hair_1x100_straight_ms` | 0.1191 | 0.1071 | 40 |
| `bench.hair_1x160_braids_ms` | 0.2865 | 0.2865 | 22 |
| `bench.hair_1x160_cornrows_ms` | 0.0319 | 0.0319 | 22 |
| `bench.hair_1x200_3b_ms` | 0.2841 | 0.2830 | 25 |
| `bench.hair_1x200_4c_ms` | 0.1611 | 0.1610 | 25 |
| `bench.hair_1x200_bantu_ms` | 0.3813 | 0.3813 | 25 |
| `bench.hair_1x400_curly_ms` | 1.28 | 1.17 | 40 |
| `bench.hair_1x400_long_ms` | 0.6309 | 0.5540 | 40 |
| `bench.hair_8x200_long_ms` | 2.46 | 2.16 | 40 |
| `bench.impact_synth_8_materials_ms` | 0.9111 | 0.9068 | 101 |
| `bench.lua_think_50_hooks_ms` | 0.1470 | 0.1408 | 101 |
| `bench.net_snapshot_256_bodies_ms` | 0.0512 | 0.0503 | 101 |
| `bench.particle_fluid_2000_ms` | 2.59 | 2.58 | 101 |
| `bench.rigid_crates_400_ms` | 1.02 | 0.9864 | 101 |
| `bench.rigid_raycast_1000_ms` | 0.3232 | 0.3228 | 101 |
| `stress.crates.fps_avg` | 18.85 | 22.85 | 101 |
| `stress.crates.physics_avg_ms` | 0.2852 | 0.2801 | 101 |
| `stress.fps_1pct_low` | 11.51 | 16.28 | 101 |
| `stress.fps_avg` | 18.82 | 23.34 | 101 |
| `stress.frame_p99_ms` | 66.77 | 57.86 | 101 |
| `stress.impacts.fps_avg` | 15.97 | 19.00 | 101 |
| `stress.peak_rss_mb` | 416 | 251 | 101 |
| `stress.walk.fps_avg` | 22.23 | 29.14 | 101 |

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
