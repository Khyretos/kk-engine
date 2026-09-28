# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `9410bd4700` (2026-09-28T04:55:34Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 94 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1513 | 0.1096 | 88 |
| `bench.audio_mix_32_voices_ms` | 0.0808 | 0.0614 | 94 |
| `bench.cloth_16x24_basic_ms` | 3.71 | 2.90 | 38 |
| `bench.cloth_16x24_full_ms` | 7.49 | 5.31 | 38 |
| `bench.cloth_1x32_basic_ms` | 0.4244 | 0.3282 | 38 |
| `bench.cloth_1x32_full_ms` | 0.9161 | 0.6111 | 38 |
| `bench.cloth_1x32_off_ms` | 0.4241 | 0.3291 | 38 |
| `bench.cloth_1x64_basic_ms` | 1.79 | 1.38 | 38 |
| `bench.cloth_1x64_full_ms` | 33.51 | 5.27 | 38 |
| `bench.cloth_cape_basic_ms` | 0.3948 | 0.3091 | 27 |
| `bench.cloth_cape_full_ms` | 6.71 | 4.63 | 27 |
| `bench.fracture_bake_cube_ms` | 6.17 | 4.63 | 94 |
| `bench.hair_1x100_straight_ms` | 0.1726 | 0.1071 | 33 |
| `bench.hair_1x160_braids_ms` | 0.4124 | 0.3104 | 15 |
| `bench.hair_1x160_cornrows_ms` | 0.0453 | 0.0327 | 15 |
| `bench.hair_1x200_3b_ms` | 0.4132 | 0.3052 | 18 |
| `bench.hair_1x200_4c_ms` | 0.2337 | 0.1725 | 18 |
| `bench.hair_1x200_bantu_ms` | 0.5616 | 0.4102 | 18 |
| `bench.hair_1x400_curly_ms` | 1.85 | 1.17 | 33 |
| `bench.hair_1x400_long_ms` | 0.9136 | 0.5540 | 33 |
| `bench.hair_8x200_long_ms` | 3.57 | 2.16 | 33 |
| `bench.impact_synth_8_materials_ms` | 0.9479 | 0.9068 | 94 |
| `bench.lua_think_50_hooks_ms` | 0.2004 | 0.1408 | 94 |
| `bench.net_snapshot_256_bodies_ms` | 0.0667 | 0.0503 | 94 |
| `bench.particle_fluid_2000_ms` | 3.40 | 2.58 | 94 |
| `bench.rigid_crates_400_ms` | 1.35 | 0.9864 | 94 |
| `bench.rigid_raycast_1000_ms` | 0.3501 | 0.3228 | 94 |
| `stress.crates.fps_avg` | 13.86 | 22.85 | 94 |
| `stress.crates.physics_avg_ms` | 0.3542 | 0.2847 | 94 |
| `stress.fps_1pct_low` | 7.83 | 16.28 | 94 |
| `stress.fps_avg` | 13.94 | 23.34 | 94 |
| `stress.frame_p99_ms` | 88.87 | 57.86 | 94 |
| `stress.impacts.fps_avg` | 11.84 | 19.00 | 94 |
| `stress.peak_rss_mb` | 391 | 251 | 94 |
| `stress.walk.fps_avg` | 16.58 | 29.14 | 94 |

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
