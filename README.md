# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `4c9dedec00` (2026-09-27T08:27:49Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 41 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1613 | 0.1120 | 35 |
| `bench.audio_mix_32_voices_ms` | 0.0851 | 0.0614 | 41 |
| `bench.fracture_bake_cube_ms` | 7.04 | 4.67 | 41 |
| `bench.impact_synth_8_materials_ms` | 1.42 | 0.9118 | 41 |
| `bench.lua_think_50_hooks_ms` | 0.2717 | 0.1466 | 41 |
| `bench.net_snapshot_256_bodies_ms` | 0.0993 | 0.0512 | 41 |
| `bench.particle_fluid_2000_ms` | 4.18 | 2.58 | 41 |
| `bench.rigid_crates_400_ms` | 1.59 | 0.9864 | 41 |
| `bench.rigid_raycast_1000_ms` | 0.5056 | 0.3228 | 41 |
| `stress.crates.fps_avg` | 11.64 | 22.85 | 41 |
| `stress.crates.physics_avg_ms` | 0.4839 | 0.3066 | 41 |
| `stress.fps_1pct_low` | 6.82 | 16.28 | 41 |
| `stress.fps_avg` | 11.90 | 23.34 | 41 |
| `stress.frame_p99_ms` | 103 | 57.86 | 41 |
| `stress.impacts.fps_avg` | 9.96 | 19.00 | 41 |
| `stress.peak_rss_mb` | 392 | 251 | 41 |
| `stress.walk.fps_avg` | 14.58 | 29.14 | 41 |

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
