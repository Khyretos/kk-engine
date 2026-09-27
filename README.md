# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `e2e0d2d67d` (2026-09-27T09:24:59Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 47 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1848 | 0.1120 | 41 |
| `bench.audio_mix_32_voices_ms` | 0.0930 | 0.0614 | 47 |
| `bench.fracture_bake_cube_ms` | 7.17 | 4.67 | 47 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9118 | 47 |
| `bench.lua_think_50_hooks_ms` | 0.2866 | 0.1466 | 47 |
| `bench.net_snapshot_256_bodies_ms` | 0.0856 | 0.0512 | 47 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 47 |
| `bench.rigid_crates_400_ms` | 1.63 | 0.9864 | 47 |
| `bench.rigid_raycast_1000_ms` | 0.4956 | 0.3228 | 47 |
| `stress.crates.fps_avg` | 11.17 | 22.85 | 47 |
| `stress.crates.physics_avg_ms` | 0.4563 | 0.3066 | 47 |
| `stress.fps_1pct_low` | 5.81 | 16.28 | 47 |
| `stress.fps_avg` | 10.93 | 23.34 | 47 |
| `stress.frame_p99_ms` | 110 | 57.86 | 47 |
| `stress.impacts.fps_avg` | 9.33 | 19.00 | 47 |
| `stress.peak_rss_mb` | 434 | 251 | 47 |
| `stress.walk.fps_avg` | 12.59 | 29.14 | 47 |

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
