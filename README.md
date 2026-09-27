# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `0ef01e4746` (2026-09-27T05:05:48Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 34 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1876 | 0.1120 | 28 |
| `bench.audio_mix_32_voices_ms` | 0.0929 | 0.0614 | 34 |
| `bench.fracture_bake_cube_ms` | 7.18 | 4.67 | 34 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9118 | 34 |
| `bench.lua_think_50_hooks_ms` | 0.3089 | 0.1466 | 34 |
| `bench.net_snapshot_256_bodies_ms` | 0.0885 | 0.0512 | 34 |
| `bench.particle_fluid_2000_ms` | 3.87 | 2.58 | 34 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 34 |
| `bench.rigid_raycast_1000_ms` | 0.4966 | 0.3228 | 34 |
| `stress.crates.fps_avg` | 11.25 | 22.85 | 34 |
| `stress.crates.physics_avg_ms` | 0.4095 | 0.3066 | 34 |
| `stress.fps_1pct_low` | 5.87 | 16.28 | 34 |
| `stress.fps_avg` | 10.97 | 23.34 | 34 |
| `stress.frame_p99_ms` | 109 | 57.86 | 34 |
| `stress.impacts.fps_avg` | 9.35 | 19.00 | 34 |
| `stress.peak_rss_mb` | 391 | 251 | 34 |
| `stress.walk.fps_avg` | 12.59 | 29.14 | 34 |

![bench.audio_mix_32_voices_full_ms](charts/bench_audio_mix_32_voices_full_ms.svg)

![bench.audio_mix_32_voices_ms](charts/bench_audio_mix_32_voices_ms.svg)

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
