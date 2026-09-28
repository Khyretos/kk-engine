# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `3af7072d2b` (2026-09-28T01:26:12Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 88 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1894 | 0.1096 | 82 |
| `bench.audio_mix_32_voices_ms` | 0.0934 | 0.0614 | 88 |
| `bench.cloth_16x24_basic_ms` | 4.68 | 2.91 | 32 |
| `bench.cloth_16x24_full_ms` | 9.08 | 5.31 | 32 |
| `bench.cloth_1x32_basic_ms` | 0.5321 | 0.3282 | 32 |
| `bench.cloth_1x32_full_ms` | 1.13 | 0.6111 | 32 |
| `bench.cloth_1x32_off_ms` | 0.5310 | 0.3291 | 32 |
| `bench.cloth_1x64_basic_ms` | 2.25 | 1.38 | 32 |
| `bench.cloth_1x64_full_ms` | 39.07 | 5.27 | 32 |
| `bench.cloth_cape_basic_ms` | 0.5080 | 0.3091 | 21 |
| `bench.cloth_cape_full_ms` | 7.77 | 4.63 | 21 |
| `bench.fracture_bake_cube_ms` | 7.19 | 4.63 | 88 |
| `bench.hair_1x100_straight_ms` | 0.2214 | 0.1071 | 27 |
| `bench.hair_1x160_braids_ms` | 0.5371 | 0.4129 | 9 |
| `bench.hair_1x160_cornrows_ms` | 0.0555 | 0.0447 | 9 |
| `bench.hair_1x200_3b_ms` | 0.5410 | 0.4124 | 12 |
| `bench.hair_1x200_4c_ms` | 0.3051 | 0.2338 | 12 |
| `bench.hair_1x200_bantu_ms` | 0.7288 | 0.5554 | 12 |
| `bench.hair_1x400_curly_ms` | 2.36 | 1.17 | 27 |
| `bench.hair_1x400_long_ms` | 1.18 | 0.5540 | 27 |
| `bench.hair_8x200_long_ms` | 4.58 | 2.16 | 27 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 88 |
| `bench.lua_think_50_hooks_ms` | 0.2917 | 0.1408 | 88 |
| `bench.net_snapshot_256_bodies_ms` | 0.0881 | 0.0503 | 88 |
| `bench.particle_fluid_2000_ms` | 3.88 | 2.58 | 88 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 88 |
| `bench.rigid_raycast_1000_ms` | 0.4939 | 0.3228 | 88 |
| `stress.crates.fps_avg` | 11.13 | 22.85 | 88 |
| `stress.crates.physics_avg_ms` | 0.4114 | 0.2847 | 88 |
| `stress.fps_1pct_low` | 5.81 | 16.28 | 88 |
| `stress.fps_avg` | 10.83 | 23.34 | 88 |
| `stress.frame_p99_ms` | 111 | 57.86 | 88 |
| `stress.impacts.fps_avg` | 9.26 | 19.00 | 88 |
| `stress.peak_rss_mb` | 413 | 251 | 88 |
| `stress.walk.fps_avg` | 12.36 | 29.14 | 88 |

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
