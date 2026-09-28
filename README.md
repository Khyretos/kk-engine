# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `270c73472b` (2026-09-28T12:47:30Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 114 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1846 | 0.1096 | 108 |
| `bench.audio_mix_32_voices_ms` | 0.0929 | 0.0614 | 114 |
| `bench.cloth_16x24_basic_ms` | 4.67 | 2.90 | 58 |
| `bench.cloth_16x24_full_ms` | 10.16 | 5.31 | 58 |
| `bench.cloth_1x32_basic_ms` | 0.5309 | 0.3282 | 58 |
| `bench.cloth_1x32_full_ms` | 1.23 | 0.6111 | 58 |
| `bench.cloth_1x32_off_ms` | 0.5307 | 0.3291 | 58 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 58 |
| `bench.cloth_1x64_full_ms` | 40.16 | 5.27 | 58 |
| `bench.cloth_cape_basic_ms` | 0.5027 | 0.3091 | 47 |
| `bench.cloth_cape_full_ms` | 8.37 | 4.63 | 47 |
| `bench.fracture_bake_cube_ms` | 7.17 | 4.62 | 114 |
| `bench.hair_1x100_straight_ms` | 0.2202 | 0.1071 | 53 |
| `bench.hair_1x160_braids_ms` | 0.5393 | 0.2855 | 35 |
| `bench.hair_1x160_cornrows_ms` | 0.0557 | 0.0317 | 35 |
| `bench.hair_1x200_3b_ms` | 0.5327 | 0.2824 | 38 |
| `bench.hair_1x200_4c_ms` | 0.3019 | 0.1606 | 38 |
| `bench.hair_1x200_bantu_ms` | 0.7307 | 0.3801 | 38 |
| `bench.hair_1x400_curly_ms` | 2.34 | 1.17 | 53 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 53 |
| `bench.hair_8x200_long_ms` | 4.65 | 2.16 | 53 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 114 |
| `bench.lua_think_50_hooks_ms` | 0.2941 | 0.1408 | 114 |
| `bench.net_snapshot_256_bodies_ms` | 0.0853 | 0.0503 | 114 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 114 |
| `bench.rigid_crates_400_ms` | 1.61 | 0.9864 | 114 |
| `bench.rigid_raycast_1000_ms` | 0.4947 | 0.3228 | 114 |
| `stress.crates.fps_avg` | 10.97 | 22.85 | 114 |
| `stress.crates.physics_avg_ms` | 0.3936 | 0.2801 | 114 |
| `stress.fps_1pct_low` | 5.74 | 16.28 | 114 |
| `stress.fps_avg` | 10.71 | 23.34 | 114 |
| `stress.frame_p99_ms` | 112 | 57.86 | 114 |
| `stress.impacts.fps_avg` | 9.15 | 19.00 | 114 |
| `stress.peak_rss_mb` | 393 | 251 | 114 |
| `stress.walk.fps_avg` | 12.26 | 29.14 | 114 |

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
