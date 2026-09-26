# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `1d042a79da` (2026-09-26T22:10:25Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 13 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1866 | 0.1342 | 7 |
| `bench.audio_mix_32_voices_ms` | 0.1040 | 0.0709 | 13 |
| `bench.fracture_bake_cube_ms` | 7.94 | 6.07 | 13 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 1.03 | 13 |
| `bench.lua_think_50_hooks_ms` | 0.2718 | 0.2108 | 13 |
| `bench.net_snapshot_256_bodies_ms` | 0.0919 | 0.0839 | 13 |
| `bench.particle_fluid_2000_ms` | 4.35 | 3.59 | 13 |
| `bench.rigid_crates_400_ms` | 1.87 | 1.33 | 13 |
| `bench.rigid_raycast_1000_ms` | 0.4528 | 0.4341 | 13 |
| `stress.crates.fps_avg` | 13.10 | 21.93 | 13 |
| `stress.crates.physics_avg_ms` | 0.4627 | 0.4230 | 13 |
| `stress.fps_1pct_low` | 6.93 | 16.28 | 13 |
| `stress.fps_avg` | 13.20 | 21.82 | 13 |
| `stress.frame_p99_ms` | 95.79 | 59.73 | 13 |
| `stress.impacts.fps_avg` | 10.92 | 17.26 | 13 |
| `stress.peak_rss_mb` | 291 | 251 | 13 |
| `stress.walk.fps_avg` | 16.04 | 27.20 | 13 |

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
