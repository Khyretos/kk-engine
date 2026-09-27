# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `34fadf27a0` (2026-09-27T02:00:15Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 29 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1882 | 0.1342 | 23 |
| `bench.audio_mix_32_voices_ms` | 0.0932 | 0.0708 | 29 |
| `bench.fracture_bake_cube_ms` | 7.15 | 6.02 | 29 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9483 | 29 |
| `bench.lua_think_50_hooks_ms` | 0.3148 | 0.2108 | 29 |
| `bench.net_snapshot_256_bodies_ms` | 0.0881 | 0.0666 | 29 |
| `bench.particle_fluid_2000_ms` | 3.89 | 3.39 | 29 |
| `bench.rigid_crates_400_ms` | 1.60 | 1.32 | 29 |
| `bench.rigid_raycast_1000_ms` | 0.4899 | 0.3511 | 29 |
| `stress.crates.fps_avg` | 14.00 | 21.93 | 29 |
| `stress.crates.physics_avg_ms` | 0.4420 | 0.3689 | 29 |
| `stress.fps_1pct_low` | 7.76 | 16.28 | 29 |
| `stress.fps_avg` | 13.79 | 21.82 | 29 |
| `stress.frame_p99_ms` | 90.42 | 59.73 | 29 |
| `stress.impacts.fps_avg` | 11.39 | 17.26 | 29 |
| `stress.peak_rss_mb` | 316 | 251 | 29 |
| `stress.walk.fps_avg` | 16.42 | 27.20 | 29 |

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
