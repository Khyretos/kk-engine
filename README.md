# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `c34ca0aca8` (2026-09-27T10:51:56Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 57 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1846 | 0.1103 | 51 |
| `bench.audio_mix_32_voices_ms` | 0.0938 | 0.0614 | 57 |
| `bench.cloth_16x24_basic_ms` | 4.63 | 4.63 | 1 |
| `bench.cloth_16x24_full_ms` | 10.11 | 10.11 | 1 |
| `bench.cloth_1x32_basic_ms` | 0.5286 | 0.5286 | 1 |
| `bench.cloth_1x32_full_ms` | 1.23 | 1.23 | 1 |
| `bench.cloth_1x32_off_ms` | 0.5290 | 0.5290 | 1 |
| `bench.cloth_1x64_basic_ms` | 2.24 | 2.24 | 1 |
| `bench.cloth_1x64_full_ms` | 8.30 | 8.30 | 1 |
| `bench.fracture_bake_cube_ms` | 7.23 | 4.67 | 57 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9114 | 57 |
| `bench.lua_think_50_hooks_ms` | 0.2837 | 0.1412 | 57 |
| `bench.net_snapshot_256_bodies_ms` | 0.0908 | 0.0503 | 57 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 57 |
| `bench.rigid_crates_400_ms` | 1.59 | 0.9864 | 57 |
| `bench.rigid_raycast_1000_ms` | 0.5001 | 0.3228 | 57 |
| `stress.crates.fps_avg` | 10.90 | 22.85 | 57 |
| `stress.crates.physics_avg_ms` | 0.4010 | 0.2896 | 57 |
| `stress.fps_1pct_low` | 5.68 | 16.28 | 57 |
| `stress.fps_avg` | 10.74 | 23.34 | 57 |
| `stress.frame_p99_ms` | 114 | 57.86 | 57 |
| `stress.impacts.fps_avg` | 9.14 | 19.00 | 57 |
| `stress.peak_rss_mb` | 438 | 251 | 57 |
| `stress.walk.fps_avg` | 12.47 | 29.14 | 57 |

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
