# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `25060fd3f1` (2026-09-27T00:55:41Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 24 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1566 | 0.1342 | 18 |
| `bench.audio_mix_32_voices_ms` | 0.0811 | 0.0708 | 24 |
| `bench.fracture_bake_cube_ms` | 6.16 | 6.02 | 24 |
| `bench.impact_synth_8_materials_ms` | 0.9491 | 0.9487 | 24 |
| `bench.lua_think_50_hooks_ms` | 0.2167 | 0.2108 | 24 |
| `bench.net_snapshot_256_bodies_ms` | 0.0667 | 0.0667 | 24 |
| `bench.particle_fluid_2000_ms` | 3.40 | 3.39 | 24 |
| `bench.rigid_crates_400_ms` | 1.37 | 1.32 | 24 |
| `bench.rigid_raycast_1000_ms` | 0.3574 | 0.3511 | 24 |
| `stress.crates.fps_avg` | 17.49 | 21.93 | 24 |
| `stress.crates.physics_avg_ms` | 0.3707 | 0.3707 | 24 |
| `stress.fps_1pct_low` | 10.75 | 16.28 | 24 |
| `stress.fps_avg` | 17.49 | 21.82 | 24 |
| `stress.frame_p99_ms` | 71.15 | 59.73 | 24 |
| `stress.impacts.fps_avg` | 14.39 | 17.26 | 24 |
| `stress.peak_rss_mb` | 291 | 251 | 24 |
| `stress.walk.fps_avg` | 21.21 | 27.20 | 24 |

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
