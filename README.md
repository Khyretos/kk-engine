# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `105ee96829` (2026-09-28T06:03:46Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 100 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1850 | 0.1096 | 94 |
| `bench.audio_mix_32_voices_ms` | 0.0939 | 0.0614 | 100 |
| `bench.cloth_16x24_basic_ms` | 4.64 | 2.90 | 44 |
| `bench.cloth_16x24_full_ms` | 9.56 | 5.31 | 44 |
| `bench.cloth_1x32_basic_ms` | 0.5302 | 0.3282 | 44 |
| `bench.cloth_1x32_full_ms` | 1.19 | 0.6111 | 44 |
| `bench.cloth_1x32_off_ms` | 0.5291 | 0.3291 | 44 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.38 | 44 |
| `bench.cloth_1x64_full_ms` | 38.51 | 5.27 | 44 |
| `bench.cloth_cape_basic_ms` | 0.5030 | 0.3091 | 33 |
| `bench.cloth_cape_full_ms` | 8.10 | 4.63 | 33 |
| `bench.fracture_bake_cube_ms` | 7.21 | 4.63 | 100 |
| `bench.hair_1x100_straight_ms` | 0.2201 | 0.1071 | 39 |
| `bench.hair_1x160_braids_ms` | 0.5367 | 0.2989 | 21 |
| `bench.hair_1x160_cornrows_ms` | 0.0558 | 0.0327 | 21 |
| `bench.hair_1x200_3b_ms` | 0.5286 | 0.2830 | 24 |
| `bench.hair_1x200_4c_ms` | 0.3004 | 0.1610 | 24 |
| `bench.hair_1x200_bantu_ms` | 0.7259 | 0.3852 | 24 |
| `bench.hair_1x400_curly_ms` | 2.34 | 1.17 | 39 |
| `bench.hair_1x400_long_ms` | 1.16 | 0.5540 | 39 |
| `bench.hair_8x200_long_ms` | 4.52 | 2.16 | 39 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9068 | 100 |
| `bench.lua_think_50_hooks_ms` | 0.2763 | 0.1408 | 100 |
| `bench.net_snapshot_256_bodies_ms` | 0.0853 | 0.0503 | 100 |
| `bench.particle_fluid_2000_ms` | 3.95 | 2.58 | 100 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 100 |
| `bench.rigid_raycast_1000_ms` | 0.4977 | 0.3228 | 100 |
| `stress.crates.fps_avg` | 11.17 | 22.85 | 100 |
| `stress.crates.physics_avg_ms` | 0.4059 | 0.2801 | 100 |
| `stress.fps_1pct_low` | 5.88 | 16.28 | 100 |
| `stress.fps_avg` | 10.91 | 23.34 | 100 |
| `stress.frame_p99_ms` | 111 | 57.86 | 100 |
| `stress.impacts.fps_avg` | 9.31 | 19.00 | 100 |
| `stress.peak_rss_mb` | 417 | 251 | 100 |
| `stress.walk.fps_avg` | 12.52 | 29.14 | 100 |

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
