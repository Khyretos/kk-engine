# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `4b60f5a134` (2026-09-28T10:16:45Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 108 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1852 | 0.1096 | 102 |
| `bench.audio_mix_32_voices_ms` | 0.0932 | 0.0614 | 108 |
| `bench.cloth_16x24_basic_ms` | 4.63 | 2.90 | 52 |
| `bench.cloth_16x24_full_ms` | 9.68 | 5.31 | 52 |
| `bench.cloth_1x32_basic_ms` | 0.5267 | 0.3282 | 52 |
| `bench.cloth_1x32_full_ms` | 1.18 | 0.6111 | 52 |
| `bench.cloth_1x32_off_ms` | 0.5242 | 0.3291 | 52 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 52 |
| `bench.cloth_1x64_full_ms` | 38.45 | 5.27 | 52 |
| `bench.cloth_cape_basic_ms` | 0.4987 | 0.3091 | 41 |
| `bench.cloth_cape_full_ms` | 8.09 | 4.63 | 41 |
| `bench.fracture_bake_cube_ms` | 7.15 | 4.62 | 108 |
| `bench.hair_1x100_straight_ms` | 0.2233 | 0.1071 | 47 |
| `bench.hair_1x160_braids_ms` | 0.5402 | 0.2855 | 29 |
| `bench.hair_1x160_cornrows_ms` | 0.0557 | 0.0317 | 29 |
| `bench.hair_1x200_3b_ms` | 0.5362 | 0.2824 | 32 |
| `bench.hair_1x200_4c_ms` | 0.3016 | 0.1606 | 32 |
| `bench.hair_1x200_bantu_ms` | 0.7278 | 0.3801 | 32 |
| `bench.hair_1x400_curly_ms` | 2.37 | 1.17 | 47 |
| `bench.hair_1x400_long_ms` | 1.17 | 0.5540 | 47 |
| `bench.hair_8x200_long_ms` | 4.59 | 2.16 | 47 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9068 | 108 |
| `bench.lua_think_50_hooks_ms` | 0.2794 | 0.1408 | 108 |
| `bench.net_snapshot_256_bodies_ms` | 0.0851 | 0.0503 | 108 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 108 |
| `bench.rigid_crates_400_ms` | 1.62 | 0.9864 | 108 |
| `bench.rigid_raycast_1000_ms` | 0.4943 | 0.3228 | 108 |
| `stress.crates.fps_avg` | 11.16 | 22.85 | 108 |
| `stress.crates.physics_avg_ms` | 0.4402 | 0.2801 | 108 |
| `stress.fps_1pct_low` | 5.84 | 16.28 | 108 |
| `stress.fps_avg` | 10.91 | 23.34 | 108 |
| `stress.frame_p99_ms` | 109 | 57.86 | 108 |
| `stress.impacts.fps_avg` | 9.39 | 19.00 | 108 |
| `stress.peak_rss_mb` | 413 | 251 | 108 |
| `stress.walk.fps_avg` | 12.44 | 29.14 | 108 |

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
