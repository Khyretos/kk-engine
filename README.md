# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `f15c3a1cf0` (2026-09-27T12:52:48Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 69 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1857 | 0.1103 | 63 |
| `bench.audio_mix_32_voices_ms` | 0.0937 | 0.0614 | 69 |
| `bench.cloth_16x24_basic_ms` | 4.64 | 3.04 | 13 |
| `bench.cloth_16x24_full_ms` | 8.66 | 5.94 | 13 |
| `bench.cloth_1x32_basic_ms` | 0.5303 | 0.3387 | 13 |
| `bench.cloth_1x32_full_ms` | 1.04 | 0.7056 | 13 |
| `bench.cloth_1x32_off_ms` | 0.5294 | 0.3435 | 13 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.44 | 13 |
| `bench.cloth_1x64_full_ms` | 9.90 | 5.27 | 13 |
| `bench.cloth_cape_basic_ms` | 0.5013 | 0.4990 | 2 |
| `bench.cloth_cape_full_ms` | 7.64 | 7.64 | 2 |
| `bench.fracture_bake_cube_ms` | 7.18 | 4.67 | 69 |
| `bench.hair_1x100_straight_ms` | 0.2009 | 0.1564 | 8 |
| `bench.hair_1x400_curly_ms` | 2.16 | 1.71 | 8 |
| `bench.hair_1x400_long_ms` | 1.05 | 0.8139 | 8 |
| `bench.hair_8x200_long_ms` | 4.09 | 3.19 | 8 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9114 | 69 |
| `bench.lua_think_50_hooks_ms` | 0.2978 | 0.1408 | 69 |
| `bench.net_snapshot_256_bodies_ms` | 0.0880 | 0.0503 | 69 |
| `bench.particle_fluid_2000_ms` | 3.88 | 2.58 | 69 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 69 |
| `bench.rigid_raycast_1000_ms` | 0.4942 | 0.3228 | 69 |
| `stress.crates.fps_avg` | 10.99 | 22.85 | 69 |
| `stress.crates.physics_avg_ms` | 0.3921 | 0.2847 | 69 |
| `stress.fps_1pct_low` | 5.77 | 16.28 | 69 |
| `stress.fps_avg` | 10.83 | 23.34 | 69 |
| `stress.frame_p99_ms` | 111 | 57.86 | 69 |
| `stress.impacts.fps_avg` | 9.32 | 19.00 | 69 |
| `stress.peak_rss_mb` | 416 | 251 | 69 |
| `stress.walk.fps_avg` | 12.46 | 29.14 | 69 |

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
