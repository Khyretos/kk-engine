# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `5ee83ef7d5` (2026-09-27T07:57:09Z) on Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz, llvmpipe (LLVM 20.1.2, 256 bits). 37 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1781 | 0.1120 | 31 |
| `bench.audio_mix_32_voices_ms` | 0.0826 | 0.0614 | 37 |
| `bench.fracture_bake_cube_ms` | 6.59 | 4.67 | 37 |
| `bench.impact_synth_8_materials_ms` | 1.24 | 0.9118 | 37 |
| `bench.lua_think_50_hooks_ms` | 0.2693 | 0.1466 | 37 |
| `bench.net_snapshot_256_bodies_ms` | 0.0988 | 0.0512 | 37 |
| `bench.particle_fluid_2000_ms` | 3.97 | 2.58 | 37 |
| `bench.rigid_crates_400_ms` | 1.53 | 0.9864 | 37 |
| `bench.rigid_raycast_1000_ms` | 0.5124 | 0.3228 | 37 |
| `stress.crates.fps_avg` | 9.92 | 22.85 | 37 |
| `stress.crates.physics_avg_ms` | 0.4281 | 0.3066 | 37 |
| `stress.fps_1pct_low` | 5.71 | 16.28 | 37 |
| `stress.fps_avg` | 9.63 | 23.34 | 37 |
| `stress.frame_p99_ms` | 127 | 57.86 | 37 |
| `stress.impacts.fps_avg` | 8.18 | 19.00 | 37 |
| `stress.peak_rss_mb` | 415 | 251 | 37 |
| `stress.walk.fps_avg` | 11.02 | 29.14 | 37 |

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
