# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `f661b50fdf` (2026-09-26T23:58:02Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 19 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1347 | 0.1342 | 13 |
| `bench.audio_mix_32_voices_ms` | 0.0708 | 0.0708 | 19 |
| `bench.fracture_bake_cube_ms` | 6.02 | 6.02 | 19 |
| `bench.impact_synth_8_materials_ms` | 1.18 | 0.9487 | 19 |
| `bench.lua_think_50_hooks_ms` | 0.2254 | 0.2108 | 19 |
| `bench.net_snapshot_256_bodies_ms` | 0.0819 | 0.0667 | 19 |
| `bench.particle_fluid_2000_ms` | 3.56 | 3.39 | 19 |
| `bench.rigid_crates_400_ms` | 1.32 | 1.32 | 19 |
| `bench.rigid_raycast_1000_ms` | 0.4334 | 0.3511 | 19 |
| `stress.crates.fps_avg` | 18.50 | 21.93 | 19 |
| `stress.crates.physics_avg_ms` | 0.4635 | 0.3814 | 19 |
| `stress.fps_1pct_low` | 11.46 | 16.28 | 19 |
| `stress.fps_avg` | 18.69 | 21.82 | 19 |
| `stress.frame_p99_ms` | 68.78 | 59.73 | 19 |
| `stress.impacts.fps_avg` | 15.07 | 17.26 | 19 |
| `stress.peak_rss_mb` | 321 | 251 | 19 |
| `stress.walk.fps_avg` | 23.27 | 27.20 | 19 |

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
