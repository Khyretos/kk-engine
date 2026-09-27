# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `99d61619b4` (2026-09-27T08:46:14Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 43 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1883 | 0.1120 | 37 |
| `bench.audio_mix_32_voices_ms` | 0.0924 | 0.0614 | 43 |
| `bench.fracture_bake_cube_ms` | 7.23 | 4.67 | 43 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9118 | 43 |
| `bench.lua_think_50_hooks_ms` | 0.2888 | 0.1466 | 43 |
| `bench.net_snapshot_256_bodies_ms` | 0.0913 | 0.0512 | 43 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 43 |
| `bench.rigid_crates_400_ms` | 1.59 | 0.9864 | 43 |
| `bench.rigid_raycast_1000_ms` | 0.5001 | 0.3228 | 43 |
| `stress.crates.fps_avg` | 11.18 | 22.85 | 43 |
| `stress.crates.physics_avg_ms` | 0.4526 | 0.3066 | 43 |
| `stress.fps_1pct_low` | 5.76 | 16.28 | 43 |
| `stress.fps_avg` | 10.93 | 23.34 | 43 |
| `stress.frame_p99_ms` | 113 | 57.86 | 43 |
| `stress.impacts.fps_avg` | 9.31 | 19.00 | 43 |
| `stress.peak_rss_mb` | 420 | 251 | 43 |
| `stress.walk.fps_avg` | 12.58 | 29.14 | 43 |

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
