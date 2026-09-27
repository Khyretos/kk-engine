# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `688fc03883` (2026-09-27T12:34:09Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 66 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1854 | 0.1103 | 60 |
| `bench.audio_mix_32_voices_ms` | 0.0939 | 0.0614 | 66 |
| `bench.cloth_16x24_basic_ms` | 4.67 | 3.04 | 10 |
| `bench.cloth_16x24_full_ms` | 10.37 | 5.94 | 10 |
| `bench.cloth_1x32_basic_ms` | 0.5300 | 0.3387 | 10 |
| `bench.cloth_1x32_full_ms` | 1.23 | 0.7056 | 10 |
| `bench.cloth_1x32_off_ms` | 0.5301 | 0.3435 | 10 |
| `bench.cloth_1x64_basic_ms` | 2.25 | 1.44 | 10 |
| `bench.cloth_1x64_full_ms` | 8.61 | 5.27 | 10 |
| `bench.fracture_bake_cube_ms` | 7.26 | 4.67 | 66 |
| `bench.hair_1x100_straight_ms` | 0.2019 | 0.1564 | 5 |
| `bench.hair_1x400_curly_ms` | 2.21 | 1.71 | 5 |
| `bench.hair_1x400_long_ms` | 1.07 | 0.8139 | 5 |
| `bench.hair_8x200_long_ms` | 4.32 | 3.19 | 5 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9114 | 66 |
| `bench.lua_think_50_hooks_ms` | 0.2786 | 0.1408 | 66 |
| `bench.net_snapshot_256_bodies_ms` | 0.0911 | 0.0503 | 66 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 66 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 66 |
| `bench.rigid_raycast_1000_ms` | 0.4967 | 0.3228 | 66 |
| `stress.crates.fps_avg` | 10.35 | 22.85 | 66 |
| `stress.crates.physics_avg_ms` | 0.4154 | 0.2847 | 66 |
| `stress.fps_1pct_low` | 5.13 | 16.28 | 66 |
| `stress.fps_avg` | 10.57 | 23.34 | 66 |
| `stress.frame_p99_ms` | 120 | 57.86 | 66 |
| `stress.impacts.fps_avg` | 8.83 | 19.00 | 66 |
| `stress.peak_rss_mb` | 437 | 251 | 66 |
| `stress.walk.fps_avg` | 12.96 | 29.14 | 66 |

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
