# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `1ea8b6d205` (2026-09-27T11:47:13Z) on Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz, llvmpipe (LLVM 20.1.2, 256 bits). 62 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1772 | 0.1103 | 56 |
| `bench.audio_mix_32_voices_ms` | 0.0846 | 0.0614 | 62 |
| `bench.cloth_16x24_basic_ms` | 4.09 | 3.04 | 6 |
| `bench.cloth_16x24_full_ms` | 9.85 | 5.94 | 6 |
| `bench.cloth_1x32_basic_ms` | 0.4682 | 0.3387 | 6 |
| `bench.cloth_1x32_full_ms` | 1.14 | 0.7056 | 6 |
| `bench.cloth_1x32_off_ms` | 0.4676 | 0.3435 | 6 |
| `bench.cloth_1x64_basic_ms` | 2.00 | 1.44 | 6 |
| `bench.cloth_1x64_full_ms` | 8.75 | 5.27 | 6 |
| `bench.fracture_bake_cube_ms` | 6.55 | 4.67 | 62 |
| `bench.hair_1x100_straight_ms` | 0.1743 | 0.1743 | 1 |
| `bench.hair_1x400_curly_ms` | 1.86 | 1.86 | 1 |
| `bench.hair_1x400_long_ms` | 0.9310 | 0.9310 | 1 |
| `bench.hair_8x200_long_ms` | 3.71 | 3.71 | 1 |
| `bench.impact_synth_8_materials_ms` | 1.37 | 0.9114 | 62 |
| `bench.lua_think_50_hooks_ms` | 0.2536 | 0.1408 | 62 |
| `bench.net_snapshot_256_bodies_ms` | 0.0994 | 0.0503 | 62 |
| `bench.particle_fluid_2000_ms` | 3.97 | 2.58 | 62 |
| `bench.rigid_crates_400_ms` | 1.57 | 0.9864 | 62 |
| `bench.rigid_raycast_1000_ms` | 0.5265 | 0.3228 | 62 |
| `stress.crates.fps_avg` | 9.97 | 22.85 | 62 |
| `stress.crates.physics_avg_ms` | 0.4001 | 0.2847 | 62 |
| `stress.fps_1pct_low` | 5.75 | 16.28 | 62 |
| `stress.fps_avg` | 9.66 | 23.34 | 62 |
| `stress.frame_p99_ms` | 126 | 57.86 | 62 |
| `stress.impacts.fps_avg` | 8.12 | 19.00 | 62 |
| `stress.peak_rss_mb` | 437 | 251 | 62 |
| `stress.walk.fps_avg` | 11.15 | 29.14 | 62 |

![bench.audio_mix_32_voices_full_ms](charts/bench_audio_mix_32_voices_full_ms.svg)

![bench.audio_mix_32_voices_ms](charts/bench_audio_mix_32_voices_ms.svg)

![bench.cloth_16x24_basic_ms](charts/bench_cloth_16x24_basic_ms.svg)

![bench.cloth_16x24_full_ms](charts/bench_cloth_16x24_full_ms.svg)

![bench.cloth_1x32_basic_ms](charts/bench_cloth_1x32_basic_ms.svg)

![bench.cloth_1x32_full_ms](charts/bench_cloth_1x32_full_ms.svg)

![bench.cloth_1x32_off_ms](charts/bench_cloth_1x32_off_ms.svg)

![bench.cloth_1x64_basic_ms](charts/bench_cloth_1x64_basic_ms.svg)

![bench.cloth_1x64_full_ms](charts/bench_cloth_1x64_full_ms.svg)

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
