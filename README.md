# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `01eec8510e` (2026-09-27T17:40:51Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 80 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1842 | 0.1096 | 74 |
| `bench.audio_mix_32_voices_ms` | 0.0936 | 0.0614 | 80 |
| `bench.cloth_16x24_basic_ms` | 4.71 | 2.91 | 24 |
| `bench.cloth_16x24_full_ms` | 9.13 | 5.31 | 24 |
| `bench.cloth_1x32_basic_ms` | 0.5387 | 0.3282 | 24 |
| `bench.cloth_1x32_full_ms` | 1.14 | 0.6111 | 24 |
| `bench.cloth_1x32_off_ms` | 0.5372 | 0.3291 | 24 |
| `bench.cloth_1x64_basic_ms` | 2.27 | 1.38 | 24 |
| `bench.cloth_1x64_full_ms` | 38.99 | 5.27 | 24 |
| `bench.cloth_cape_basic_ms` | 0.5151 | 0.3091 | 13 |
| `bench.cloth_cape_full_ms` | 8.14 | 4.63 | 13 |
| `bench.fracture_bake_cube_ms` | 7.26 | 4.63 | 80 |
| `bench.hair_1x100_straight_ms` | 0.2021 | 0.1071 | 19 |
| `bench.hair_1x160_braids_ms` | 0.5413 | 0.5413 | 1 |
| `bench.hair_1x160_cornrows_ms` | 0.0563 | 0.0563 | 1 |
| `bench.hair_1x200_3b_ms` | 0.5394 | 0.4540 | 4 |
| `bench.hair_1x200_4c_ms` | 0.3092 | 0.2404 | 4 |
| `bench.hair_1x200_bantu_ms` | 0.7344 | 0.5872 | 4 |
| `bench.hair_1x400_curly_ms` | 2.20 | 1.17 | 19 |
| `bench.hair_1x400_long_ms` | 1.06 | 0.5540 | 19 |
| `bench.hair_8x200_long_ms` | 4.13 | 2.16 | 19 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 80 |
| `bench.lua_think_50_hooks_ms` | 0.2844 | 0.1408 | 80 |
| `bench.net_snapshot_256_bodies_ms` | 0.0856 | 0.0503 | 80 |
| `bench.particle_fluid_2000_ms` | 3.95 | 2.58 | 80 |
| `bench.rigid_crates_400_ms` | 1.67 | 0.9864 | 80 |
| `bench.rigid_raycast_1000_ms` | 0.4948 | 0.3228 | 80 |
| `stress.crates.fps_avg` | 10.88 | 22.85 | 80 |
| `stress.crates.physics_avg_ms` | 0.3919 | 0.2847 | 80 |
| `stress.fps_1pct_low` | 5.69 | 16.28 | 80 |
| `stress.fps_avg` | 10.67 | 23.34 | 80 |
| `stress.frame_p99_ms` | 116 | 57.86 | 80 |
| `stress.impacts.fps_avg` | 9.02 | 19.00 | 80 |
| `stress.peak_rss_mb` | 417 | 251 | 80 |
| `stress.walk.fps_avg` | 12.39 | 29.14 | 80 |

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
