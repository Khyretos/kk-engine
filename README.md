# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `f313161c49` (2026-09-27T14:06:34Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 74 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1521 | 0.1096 | 68 |
| `bench.audio_mix_32_voices_ms` | 0.0816 | 0.0614 | 74 |
| `bench.cloth_16x24_basic_ms` | 3.71 | 2.91 | 18 |
| `bench.cloth_16x24_full_ms` | 7.07 | 5.31 | 18 |
| `bench.cloth_1x32_basic_ms` | 0.4249 | 0.3282 | 18 |
| `bench.cloth_1x32_full_ms` | 0.8260 | 0.6111 | 18 |
| `bench.cloth_1x32_off_ms` | 0.4236 | 0.3291 | 18 |
| `bench.cloth_1x64_basic_ms` | 1.79 | 1.38 | 18 |
| `bench.cloth_1x64_full_ms` | 8.23 | 5.27 | 18 |
| `bench.cloth_cape_basic_ms` | 0.3949 | 0.3091 | 7 |
| `bench.cloth_cape_full_ms` | 6.33 | 4.63 | 7 |
| `bench.fracture_bake_cube_ms` | 6.18 | 4.63 | 74 |
| `bench.hair_1x100_straight_ms` | 0.1565 | 0.1071 | 13 |
| `bench.hair_1x400_curly_ms` | 1.71 | 1.17 | 13 |
| `bench.hair_1x400_long_ms` | 0.8154 | 0.5540 | 13 |
| `bench.hair_8x200_long_ms` | 3.18 | 2.16 | 13 |
| `bench.impact_synth_8_materials_ms` | 0.9533 | 0.9068 | 74 |
| `bench.lua_think_50_hooks_ms` | 0.2074 | 0.1408 | 74 |
| `bench.net_snapshot_256_bodies_ms` | 0.0668 | 0.0503 | 74 |
| `bench.particle_fluid_2000_ms` | 3.40 | 2.58 | 74 |
| `bench.rigid_crates_400_ms` | 1.34 | 0.9864 | 74 |
| `bench.rigid_raycast_1000_ms` | 0.3477 | 0.3228 | 74 |
| `stress.crates.fps_avg` | 14.07 | 22.85 | 74 |
| `stress.crates.physics_avg_ms` | 0.3521 | 0.2847 | 74 |
| `stress.fps_1pct_low` | 8.52 | 16.28 | 74 |
| `stress.fps_avg` | 14.03 | 23.34 | 74 |
| `stress.frame_p99_ms` | 85.48 | 57.86 | 74 |
| `stress.impacts.fps_avg` | 12.04 | 19.00 | 74 |
| `stress.peak_rss_mb` | 394 | 251 | 74 |
| `stress.walk.fps_avg` | 16.38 | 29.14 | 74 |

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
