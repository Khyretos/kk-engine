# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `7d49a686f6` (2026-09-26T21:58:35Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 12 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1867 | 0.1342 | 6 |
| `bench.audio_mix_32_voices_ms` | 0.1040 | 0.0709 | 12 |
| `bench.fracture_bake_cube_ms` | 7.92 | 6.07 | 12 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 1.03 | 12 |
| `bench.lua_think_50_hooks_ms` | 0.2596 | 0.2108 | 12 |
| `bench.net_snapshot_256_bodies_ms` | 0.0859 | 0.0839 | 12 |
| `bench.particle_fluid_2000_ms` | 4.36 | 3.59 | 12 |
| `bench.rigid_crates_400_ms` | 1.76 | 1.33 | 12 |
| `bench.rigid_raycast_1000_ms` | 0.4589 | 0.4341 | 12 |
| `stress.crates.fps_avg` | 13.42 | 21.93 | 12 |
| `stress.crates.physics_avg_ms` | 0.4518 | 0.4230 | 12 |
| `stress.fps_1pct_low` | 6.91 | 16.28 | 12 |
| `stress.fps_avg` | 13.28 | 21.82 | 12 |
| `stress.frame_p99_ms` | 97.16 | 59.73 | 12 |
| `stress.impacts.fps_avg` | 10.90 | 17.26 | 12 |
| `stress.peak_rss_mb` | 315 | 251 | 12 |
| `stress.walk.fps_avg` | 15.94 | 27.20 | 12 |

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
