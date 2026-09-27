# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `666405ef7b` (2026-09-27T19:59:22Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 87 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1861 | 0.1096 | 81 |
| `bench.audio_mix_32_voices_ms` | 0.1038 | 0.0614 | 87 |
| `bench.cloth_16x24_basic_ms` | 4.77 | 2.91 | 31 |
| `bench.cloth_16x24_full_ms` | 9.72 | 5.31 | 31 |
| `bench.cloth_1x32_basic_ms` | 0.5432 | 0.3282 | 31 |
| `bench.cloth_1x32_full_ms` | 1.16 | 0.6111 | 31 |
| `bench.cloth_1x32_off_ms` | 0.5438 | 0.3291 | 31 |
| `bench.cloth_1x64_basic_ms` | 2.30 | 1.38 | 31 |
| `bench.cloth_1x64_full_ms` | 42.78 | 5.27 | 31 |
| `bench.cloth_cape_basic_ms` | 0.5128 | 0.3091 | 20 |
| `bench.cloth_cape_full_ms` | 8.54 | 4.63 | 20 |
| `bench.fracture_bake_cube_ms` | 7.92 | 4.63 | 87 |
| `bench.hair_1x100_straight_ms` | 0.2184 | 0.1071 | 26 |
| `bench.hair_1x160_braids_ms` | 0.5280 | 0.4129 | 8 |
| `bench.hair_1x160_cornrows_ms` | 0.0559 | 0.0447 | 8 |
| `bench.hair_1x200_3b_ms` | 0.5259 | 0.4124 | 11 |
| `bench.hair_1x200_4c_ms` | 0.2948 | 0.2338 | 11 |
| `bench.hair_1x200_bantu_ms` | 0.7099 | 0.5554 | 11 |
| `bench.hair_1x400_curly_ms` | 2.36 | 1.17 | 26 |
| `bench.hair_1x400_long_ms` | 1.15 | 0.5540 | 26 |
| `bench.hair_8x200_long_ms` | 4.52 | 2.16 | 26 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 0.9068 | 87 |
| `bench.lua_think_50_hooks_ms` | 0.2865 | 0.1408 | 87 |
| `bench.net_snapshot_256_bodies_ms` | 0.0859 | 0.0503 | 87 |
| `bench.particle_fluid_2000_ms` | 4.36 | 2.58 | 87 |
| `bench.rigid_crates_400_ms` | 1.76 | 0.9864 | 87 |
| `bench.rigid_raycast_1000_ms` | 0.4529 | 0.3228 | 87 |
| `stress.crates.fps_avg` | 10.94 | 22.85 | 87 |
| `stress.crates.physics_avg_ms` | 0.4570 | 0.2847 | 87 |
| `stress.fps_1pct_low` | 5.69 | 16.28 | 87 |
| `stress.fps_avg` | 10.76 | 23.34 | 87 |
| `stress.frame_p99_ms` | 111 | 57.86 | 87 |
| `stress.impacts.fps_avg` | 9.34 | 19.00 | 87 |
| `stress.peak_rss_mb` | 390 | 251 | 87 |
| `stress.walk.fps_avg` | 12.24 | 29.14 | 87 |

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
