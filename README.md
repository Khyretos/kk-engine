# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `b42544e817` (2026-09-28T14:30:30Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 117 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1855 | 0.1096 | 111 |
| `bench.audio_mix_32_voices_ms` | 0.0930 | 0.0614 | 117 |
| `bench.cloth_16x24_basic_ms` | 4.64 | 2.90 | 61 |
| `bench.cloth_16x24_full_ms` | 9.93 | 5.31 | 61 |
| `bench.cloth_1x32_basic_ms` | 0.5296 | 0.3282 | 61 |
| `bench.cloth_1x32_full_ms` | 1.22 | 0.6111 | 61 |
| `bench.cloth_1x32_off_ms` | 0.5279 | 0.3291 | 61 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 61 |
| `bench.cloth_1x64_full_ms` | 39.65 | 5.27 | 61 |
| `bench.cloth_cape_basic_ms` | 0.4981 | 0.3091 | 50 |
| `bench.cloth_cape_full_ms` | 8.22 | 4.63 | 50 |
| `bench.fracture_bake_cube_ms` | 7.15 | 4.62 | 117 |
| `bench.hair_1x100_straight_ms` | 0.2208 | 0.1071 | 56 |
| `bench.hair_1x160_braids_ms` | 0.5377 | 0.2855 | 38 |
| `bench.hair_1x160_cornrows_ms` | 0.0557 | 0.0317 | 38 |
| `bench.hair_1x200_3b_ms` | 0.5322 | 0.2824 | 41 |
| `bench.hair_1x200_4c_ms` | 0.3018 | 0.1606 | 41 |
| `bench.hair_1x200_bantu_ms` | 0.7234 | 0.3801 | 41 |
| `bench.hair_1x400_curly_ms` | 2.36 | 1.17 | 56 |
| `bench.hair_1x400_long_ms` | 1.17 | 0.5540 | 56 |
| `bench.hair_8x200_long_ms` | 4.59 | 2.16 | 56 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 117 |
| `bench.lua_think_50_hooks_ms` | 0.2888 | 0.1408 | 117 |
| `bench.net_snapshot_256_bodies_ms` | 0.0852 | 0.0503 | 117 |
| `bench.particle_fluid_2000_ms` | 3.92 | 2.58 | 117 |
| `bench.rigid_crates_400_ms` | 1.73 | 0.9864 | 117 |
| `bench.rigid_raycast_1000_ms` | 0.5003 | 0.3228 | 117 |
| `stress.crates.fps_avg` | 11.15 | 22.85 | 117 |
| `stress.crates.physics_avg_ms` | 0.3970 | 0.2801 | 117 |
| `stress.fps_1pct_low` | 5.85 | 16.28 | 117 |
| `stress.fps_avg` | 10.81 | 23.34 | 117 |
| `stress.frame_p99_ms` | 112 | 57.86 | 117 |
| `stress.impacts.fps_avg` | 9.17 | 19.00 | 117 |
| `stress.peak_rss_mb` | 415 | 251 | 117 |
| `stress.walk.fps_avg` | 12.36 | 29.14 | 117 |

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
