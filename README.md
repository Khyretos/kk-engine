# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `9b9ace62a4` (2026-09-27T13:33:42Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 72 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1096 | 0.1096 | 66 |
| `bench.audio_mix_32_voices_ms` | 0.0657 | 0.0614 | 72 |
| `bench.cloth_16x24_basic_ms` | 2.91 | 2.91 | 16 |
| `bench.cloth_16x24_full_ms` | 5.31 | 5.31 | 16 |
| `bench.cloth_1x32_basic_ms` | 0.3282 | 0.3282 | 16 |
| `bench.cloth_1x32_full_ms` | 0.6111 | 0.6111 | 16 |
| `bench.cloth_1x32_off_ms` | 0.3291 | 0.3291 | 16 |
| `bench.cloth_1x64_basic_ms` | 1.38 | 1.38 | 16 |
| `bench.cloth_1x64_full_ms` | 6.21 | 5.27 | 16 |
| `bench.cloth_cape_basic_ms` | 0.3091 | 0.3091 | 5 |
| `bench.cloth_cape_full_ms` | 4.63 | 4.63 | 5 |
| `bench.fracture_bake_cube_ms` | 4.63 | 4.63 | 72 |
| `bench.hair_1x100_straight_ms` | 0.1071 | 0.1071 | 11 |
| `bench.hair_1x400_curly_ms` | 1.17 | 1.17 | 11 |
| `bench.hair_1x400_long_ms` | 0.5540 | 0.5540 | 11 |
| `bench.hair_8x200_long_ms` | 2.16 | 2.16 | 11 |
| `bench.impact_synth_8_materials_ms` | 0.9068 | 0.9068 | 72 |
| `bench.lua_think_50_hooks_ms` | 0.1417 | 0.1408 | 72 |
| `bench.net_snapshot_256_bodies_ms` | 0.0508 | 0.0503 | 72 |
| `bench.particle_fluid_2000_ms` | 2.59 | 2.58 | 72 |
| `bench.rigid_crates_400_ms` | 1.01 | 0.9864 | 72 |
| `bench.rigid_raycast_1000_ms` | 0.3230 | 0.3228 | 72 |
| `stress.crates.fps_avg` | 18.86 | 22.85 | 72 |
| `stress.crates.physics_avg_ms` | 0.3013 | 0.2847 | 72 |
| `stress.fps_1pct_low` | 11.93 | 16.28 | 72 |
| `stress.fps_avg` | 19.15 | 23.34 | 72 |
| `stress.frame_p99_ms` | 64.06 | 57.86 | 72 |
| `stress.impacts.fps_avg` | 16.16 | 19.00 | 72 |
| `stress.peak_rss_mb` | 395 | 251 | 72 |
| `stress.walk.fps_avg` | 23.11 | 29.14 | 72 |

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
