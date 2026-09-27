# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `3a66f47faa` (2026-09-27T11:11:38Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 58 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1842 | 0.1103 | 52 |
| `bench.audio_mix_32_voices_ms` | 0.0937 | 0.0614 | 58 |
| `bench.cloth_16x24_basic_ms` | 4.62 | 4.62 | 2 |
| `bench.cloth_16x24_full_ms` | 9.79 | 9.79 | 2 |
| `bench.cloth_1x32_basic_ms` | 0.5254 | 0.5254 | 2 |
| `bench.cloth_1x32_full_ms` | 1.22 | 1.22 | 2 |
| `bench.cloth_1x32_off_ms` | 0.5255 | 0.5255 | 2 |
| `bench.cloth_1x64_basic_ms` | 2.23 | 2.23 | 2 |
| `bench.cloth_1x64_full_ms` | 8.18 | 8.18 | 2 |
| `bench.fracture_bake_cube_ms` | 7.23 | 4.67 | 58 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9114 | 58 |
| `bench.lua_think_50_hooks_ms` | 0.2770 | 0.1412 | 58 |
| `bench.net_snapshot_256_bodies_ms` | 0.0914 | 0.0503 | 58 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 58 |
| `bench.rigid_crates_400_ms` | 1.72 | 0.9864 | 58 |
| `bench.rigid_raycast_1000_ms` | 0.4932 | 0.3228 | 58 |
| `stress.crates.fps_avg` | 10.81 | 22.85 | 58 |
| `stress.crates.physics_avg_ms` | 0.4539 | 0.2896 | 58 |
| `stress.fps_1pct_low` | 5.52 | 16.28 | 58 |
| `stress.fps_avg` | 10.84 | 23.34 | 58 |
| `stress.frame_p99_ms` | 114 | 57.86 | 58 |
| `stress.impacts.fps_avg` | 9.24 | 19.00 | 58 |
| `stress.peak_rss_mb` | 434 | 251 | 58 |
| `stress.walk.fps_avg` | 12.81 | 29.14 | 58 |

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
