# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `099f05bde8` (2026-09-26T23:52:46Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 18 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1891 | 0.1342 | 12 |
| `bench.audio_mix_32_voices_ms` | 0.0936 | 0.0709 | 18 |
| `bench.fracture_bake_cube_ms` | 7.14 | 6.07 | 18 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9487 | 18 |
| `bench.lua_think_50_hooks_ms` | 0.3107 | 0.2108 | 18 |
| `bench.net_snapshot_256_bodies_ms` | 0.0856 | 0.0667 | 18 |
| `bench.particle_fluid_2000_ms` | 3.92 | 3.39 | 18 |
| `bench.rigid_crates_400_ms` | 1.64 | 1.33 | 18 |
| `bench.rigid_raycast_1000_ms` | 0.4960 | 0.3511 | 18 |
| `stress.crates.fps_avg` | 13.78 | 21.93 | 18 |
| `stress.crates.physics_avg_ms` | 0.4184 | 0.3814 | 18 |
| `stress.fps_1pct_low` | 7.39 | 16.28 | 18 |
| `stress.fps_avg` | 13.62 | 21.82 | 18 |
| `stress.frame_p99_ms` | 91.74 | 59.73 | 18 |
| `stress.impacts.fps_avg` | 11.29 | 17.26 | 18 |
| `stress.peak_rss_mb` | 317 | 251 | 18 |
| `stress.walk.fps_avg` | 16.24 | 27.20 | 18 |

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
