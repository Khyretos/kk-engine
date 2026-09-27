# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `1eb5f221f7` (2026-09-27T11:32:26Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 61 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1853 | 0.1103 | 55 |
| `bench.audio_mix_32_voices_ms` | 0.0937 | 0.0614 | 61 |
| `bench.cloth_16x24_basic_ms` | 4.71 | 3.04 | 5 |
| `bench.cloth_16x24_full_ms` | 9.79 | 5.94 | 5 |
| `bench.cloth_1x32_basic_ms` | 0.5363 | 0.3387 | 5 |
| `bench.cloth_1x32_full_ms` | 1.23 | 0.7056 | 5 |
| `bench.cloth_1x32_off_ms` | 0.5359 | 0.3435 | 5 |
| `bench.cloth_1x64_basic_ms` | 2.26 | 1.44 | 5 |
| `bench.cloth_1x64_full_ms` | 8.19 | 5.27 | 5 |
| `bench.fracture_bake_cube_ms` | 7.22 | 4.67 | 61 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9114 | 61 |
| `bench.lua_think_50_hooks_ms` | 0.2807 | 0.1408 | 61 |
| `bench.net_snapshot_256_bodies_ms` | 0.0907 | 0.0503 | 61 |
| `bench.particle_fluid_2000_ms` | 3.91 | 2.58 | 61 |
| `bench.rigid_crates_400_ms` | 1.65 | 0.9864 | 61 |
| `bench.rigid_raycast_1000_ms` | 0.4950 | 0.3228 | 61 |
| `stress.crates.fps_avg` | 10.97 | 22.85 | 61 |
| `stress.crates.physics_avg_ms` | 0.4032 | 0.2847 | 61 |
| `stress.fps_1pct_low` | 5.57 | 16.28 | 61 |
| `stress.fps_avg` | 10.83 | 23.34 | 61 |
| `stress.frame_p99_ms` | 115 | 57.86 | 61 |
| `stress.impacts.fps_avg` | 9.22 | 19.00 | 61 |
| `stress.peak_rss_mb` | 414 | 251 | 61 |
| `stress.walk.fps_avg` | 12.58 | 29.14 | 61 |

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
