# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `d7e2dbacfa` (2026-09-27T11:59:44Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 63 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1863 | 0.1103 | 57 |
| `bench.audio_mix_32_voices_ms` | 0.0947 | 0.0614 | 63 |
| `bench.cloth_16x24_basic_ms` | 4.62 | 3.04 | 7 |
| `bench.cloth_16x24_full_ms` | 9.76 | 5.94 | 7 |
| `bench.cloth_1x32_basic_ms` | 0.5293 | 0.3387 | 7 |
| `bench.cloth_1x32_full_ms` | 1.22 | 0.7056 | 7 |
| `bench.cloth_1x32_off_ms` | 0.5288 | 0.3435 | 7 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 1.44 | 7 |
| `bench.cloth_1x64_full_ms` | 8.17 | 5.27 | 7 |
| `bench.fracture_bake_cube_ms` | 7.24 | 4.67 | 63 |
| `bench.hair_1x100_straight_ms` | 0.2021 | 0.1743 | 2 |
| `bench.hair_1x400_curly_ms` | 2.16 | 1.86 | 2 |
| `bench.hair_1x400_long_ms` | 1.05 | 0.9310 | 2 |
| `bench.hair_8x200_long_ms` | 4.12 | 3.71 | 2 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9114 | 63 |
| `bench.lua_think_50_hooks_ms` | 0.2784 | 0.1408 | 63 |
| `bench.net_snapshot_256_bodies_ms` | 0.0911 | 0.0503 | 63 |
| `bench.particle_fluid_2000_ms` | 3.89 | 2.58 | 63 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 63 |
| `bench.rigid_raycast_1000_ms` | 0.4964 | 0.3228 | 63 |
| `stress.crates.fps_avg` | 10.92 | 22.85 | 63 |
| `stress.crates.physics_avg_ms` | 0.4038 | 0.2847 | 63 |
| `stress.fps_1pct_low` | 5.72 | 16.28 | 63 |
| `stress.fps_avg` | 10.75 | 23.34 | 63 |
| `stress.frame_p99_ms` | 114 | 57.86 | 63 |
| `stress.impacts.fps_avg` | 9.10 | 19.00 | 63 |
| `stress.peak_rss_mb` | 438 | 251 | 63 |
| `stress.walk.fps_avg` | 12.53 | 29.14 | 63 |

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
