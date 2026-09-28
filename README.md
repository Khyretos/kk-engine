# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `ab70cfd96a` (2026-09-28T05:56:24Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 99 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1153 | 0.1096 | 93 |
| `bench.audio_mix_32_voices_ms` | 0.0692 | 0.0614 | 99 |
| `bench.cloth_16x24_basic_ms` | 3.04 | 2.90 | 43 |
| `bench.cloth_16x24_full_ms` | 5.72 | 5.31 | 43 |
| `bench.cloth_1x32_basic_ms` | 0.3432 | 0.3282 | 43 |
| `bench.cloth_1x32_full_ms` | 0.7458 | 0.6111 | 43 |
| `bench.cloth_1x32_off_ms` | 0.3447 | 0.3291 | 43 |
| `bench.cloth_1x64_basic_ms` | 1.45 | 1.38 | 43 |
| `bench.cloth_1x64_full_ms` | 26.24 | 5.27 | 43 |
| `bench.cloth_cape_basic_ms` | 0.3246 | 0.3091 | 32 |
| `bench.cloth_cape_full_ms` | 5.38 | 4.63 | 32 |
| `bench.fracture_bake_cube_ms` | 4.86 | 4.63 | 99 |
| `bench.hair_1x100_straight_ms` | 0.1242 | 0.1071 | 38 |
| `bench.hair_1x160_braids_ms` | 0.2989 | 0.2989 | 20 |
| `bench.hair_1x160_cornrows_ms` | 0.0330 | 0.0327 | 20 |
| `bench.hair_1x200_3b_ms` | 0.2966 | 0.2830 | 23 |
| `bench.hair_1x200_4c_ms` | 0.1680 | 0.1610 | 23 |
| `bench.hair_1x200_bantu_ms` | 0.3989 | 0.3852 | 23 |
| `bench.hair_1x400_curly_ms` | 1.33 | 1.17 | 38 |
| `bench.hair_1x400_long_ms` | 0.6562 | 0.5540 | 38 |
| `bench.hair_8x200_long_ms` | 2.56 | 2.16 | 38 |
| `bench.impact_synth_8_materials_ms` | 0.9484 | 0.9068 | 99 |
| `bench.lua_think_50_hooks_ms` | 0.1428 | 0.1408 | 99 |
| `bench.net_snapshot_256_bodies_ms` | 0.0536 | 0.0503 | 99 |
| `bench.particle_fluid_2000_ms` | 2.70 | 2.58 | 99 |
| `bench.rigid_crates_400_ms` | 1.04 | 0.9864 | 99 |
| `bench.rigid_raycast_1000_ms` | 0.3416 | 0.3228 | 99 |
| `stress.crates.fps_avg` | 18.44 | 22.85 | 99 |
| `stress.crates.physics_avg_ms` | 0.2801 | 0.2801 | 99 |
| `stress.fps_1pct_low` | 10.74 | 16.28 | 99 |
| `stress.fps_avg` | 18.34 | 23.34 | 99 |
| `stress.frame_p99_ms` | 74.41 | 57.86 | 99 |
| `stress.impacts.fps_avg` | 15.74 | 19.00 | 99 |
| `stress.peak_rss_mb` | 413 | 251 | 99 |
| `stress.walk.fps_avg` | 21.33 | 29.14 | 99 |

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
