# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `77f05348fa` (2026-09-27T08:57:22Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 44 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1518 | 0.1120 | 38 |
| `bench.audio_mix_32_voices_ms` | 0.0809 | 0.0614 | 44 |
| `bench.fracture_bake_cube_ms` | 6.16 | 4.67 | 44 |
| `bench.impact_synth_8_materials_ms` | 0.9441 | 0.9118 | 44 |
| `bench.lua_think_50_hooks_ms` | 0.2200 | 0.1466 | 44 |
| `bench.net_snapshot_256_bodies_ms` | 0.0714 | 0.0512 | 44 |
| `bench.particle_fluid_2000_ms` | 3.39 | 2.58 | 44 |
| `bench.rigid_crates_400_ms` | 1.37 | 0.9864 | 44 |
| `bench.rigid_raycast_1000_ms` | 0.3525 | 0.3228 | 44 |
| `stress.crates.fps_avg` | 14.40 | 22.85 | 44 |
| `stress.crates.physics_avg_ms` | 0.3656 | 0.3066 | 44 |
| `stress.fps_1pct_low` | 8.65 | 16.28 | 44 |
| `stress.fps_avg` | 14.22 | 23.34 | 44 |
| `stress.frame_p99_ms` | 84.78 | 57.86 | 44 |
| `stress.impacts.fps_avg` | 12.00 | 19.00 | 44 |
| `stress.peak_rss_mb` | 414 | 251 | 44 |
| `stress.walk.fps_avg` | 16.66 | 29.14 | 44 |

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
