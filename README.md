# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `cfcea7d936` (2026-09-27T03:02:25Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 31 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1382 | 0.1161 | 25 |
| `bench.audio_mix_32_voices_ms` | 0.0726 | 0.0614 | 31 |
| `bench.fracture_bake_cube_ms` | 6.17 | 5.16 | 31 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 0.9483 | 31 |
| `bench.lua_think_50_hooks_ms` | 0.2311 | 0.1835 | 31 |
| `bench.net_snapshot_256_bodies_ms` | 0.0866 | 0.0666 | 31 |
| `bench.particle_fluid_2000_ms` | 3.65 | 3.02 | 31 |
| `bench.rigid_crates_400_ms` | 1.43 | 1.10 | 31 |
| `bench.rigid_raycast_1000_ms` | 0.4505 | 0.3511 | 31 |
| `stress.crates.fps_avg` | 16.61 | 21.93 | 31 |
| `stress.crates.physics_avg_ms` | 0.4315 | 0.3689 | 31 |
| `stress.fps_1pct_low` | 9.49 | 16.28 | 31 |
| `stress.fps_avg` | 16.88 | 21.82 | 31 |
| `stress.frame_p99_ms` | 80.62 | 59.73 | 31 |
| `stress.impacts.fps_avg` | 13.80 | 17.26 | 31 |
| `stress.peak_rss_mb` | 295 | 251 | 31 |
| `stress.walk.fps_avg` | 20.91 | 27.20 | 31 |

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
