# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `37c0ad1ada` (2026-09-27T12:52:10Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 68 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1875 | 0.1103 | 62 |
| `bench.audio_mix_32_voices_ms` | 0.0942 | 0.0614 | 68 |
| `bench.cloth_16x24_basic_ms` | 4.66 | 3.04 | 12 |
| `bench.cloth_16x24_full_ms` | 8.90 | 5.94 | 12 |
| `bench.cloth_1x32_basic_ms` | 0.5281 | 0.3387 | 12 |
| `bench.cloth_1x32_full_ms` | 1.02 | 0.7056 | 12 |
| `bench.cloth_1x32_off_ms` | 0.5285 | 0.3435 | 12 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.44 | 12 |
| `bench.cloth_1x64_full_ms` | 11.19 | 5.27 | 12 |
| `bench.cloth_cape_basic_ms` | 0.4990 | 0.4990 | 1 |
| `bench.cloth_cape_full_ms` | 7.66 | 7.66 | 1 |
| `bench.fracture_bake_cube_ms` | 7.24 | 4.67 | 68 |
| `bench.hair_1x100_straight_ms` | 0.2013 | 0.1564 | 7 |
| `bench.hair_1x400_curly_ms` | 2.17 | 1.71 | 7 |
| `bench.hair_1x400_long_ms` | 1.05 | 0.8139 | 7 |
| `bench.hair_8x200_long_ms` | 4.26 | 3.19 | 7 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9114 | 68 |
| `bench.lua_think_50_hooks_ms` | 0.2937 | 0.1408 | 68 |
| `bench.net_snapshot_256_bodies_ms` | 0.0877 | 0.0503 | 68 |
| `bench.particle_fluid_2000_ms` | 3.88 | 2.58 | 68 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 68 |
| `bench.rigid_raycast_1000_ms` | 0.5045 | 0.3228 | 68 |
| `stress.crates.fps_avg` | 10.73 | 22.85 | 68 |
| `stress.crates.physics_avg_ms` | 0.4055 | 0.2847 | 68 |
| `stress.fps_1pct_low` | 5.36 | 16.28 | 68 |
| `stress.fps_avg` | 10.60 | 23.34 | 68 |
| `stress.frame_p99_ms` | 118 | 57.86 | 68 |
| `stress.impacts.fps_avg` | 9.00 | 19.00 | 68 |
| `stress.peak_rss_mb` | 437 | 251 | 68 |
| `stress.walk.fps_avg` | 12.38 | 29.14 | 68 |

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
