# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `417c735096` (2026-09-27T19:01:16Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 82 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1849 | 0.1096 | 76 |
| `bench.audio_mix_32_voices_ms` | 0.0936 | 0.0614 | 82 |
| `bench.cloth_16x24_basic_ms` | 4.59 | 2.91 | 26 |
| `bench.cloth_16x24_full_ms` | 8.96 | 5.31 | 26 |
| `bench.cloth_1x32_basic_ms` | 0.5226 | 0.3282 | 26 |
| `bench.cloth_1x32_full_ms` | 1.12 | 0.6111 | 26 |
| `bench.cloth_1x32_off_ms` | 0.5223 | 0.3291 | 26 |
| `bench.cloth_1x64_basic_ms` | 2.21 | 1.38 | 26 |
| `bench.cloth_1x64_full_ms` | 38.76 | 5.27 | 26 |
| `bench.cloth_cape_basic_ms` | 0.4914 | 0.3091 | 15 |
| `bench.cloth_cape_full_ms` | 7.72 | 4.63 | 15 |
| `bench.fracture_bake_cube_ms` | 7.22 | 4.63 | 82 |
| `bench.hair_1x100_straight_ms` | 0.1989 | 0.1071 | 21 |
| `bench.hair_1x160_braids_ms` | 0.5389 | 0.5389 | 3 |
| `bench.hair_1x160_cornrows_ms` | 0.0557 | 0.0557 | 3 |
| `bench.hair_1x200_3b_ms` | 0.5296 | 0.4540 | 6 |
| `bench.hair_1x200_4c_ms` | 0.3013 | 0.2404 | 6 |
| `bench.hair_1x200_bantu_ms` | 0.7223 | 0.5872 | 6 |
| `bench.hair_1x400_curly_ms` | 2.15 | 1.17 | 21 |
| `bench.hair_1x400_long_ms` | 1.04 | 0.5540 | 21 |
| `bench.hair_8x200_long_ms` | 4.07 | 2.16 | 21 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9068 | 82 |
| `bench.lua_think_50_hooks_ms` | 0.2856 | 0.1408 | 82 |
| `bench.net_snapshot_256_bodies_ms` | 0.0858 | 0.0503 | 82 |
| `bench.particle_fluid_2000_ms` | 3.94 | 2.58 | 82 |
| `bench.rigid_crates_400_ms` | 1.63 | 0.9864 | 82 |
| `bench.rigid_raycast_1000_ms` | 0.4894 | 0.3228 | 82 |
| `stress.crates.fps_avg` | 11.04 | 22.85 | 82 |
| `stress.crates.physics_avg_ms` | 0.3942 | 0.2847 | 82 |
| `stress.fps_1pct_low` | 5.82 | 16.28 | 82 |
| `stress.fps_avg` | 10.80 | 23.34 | 82 |
| `stress.frame_p99_ms` | 114 | 57.86 | 82 |
| `stress.impacts.fps_avg` | 9.11 | 19.00 | 82 |
| `stress.peak_rss_mb` | 392 | 251 | 82 |
| `stress.walk.fps_avg` | 12.54 | 29.14 | 82 |

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
