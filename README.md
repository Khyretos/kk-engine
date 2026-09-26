# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `f794eb8e4f` (2026-09-26T21:56:01Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 11 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1879 | 0.1342 | 5 |
| `bench.audio_mix_32_voices_ms` | 0.1047 | 0.0709 | 11 |
| `bench.fracture_bake_cube_ms` | 7.93 | 6.07 | 11 |
| `bench.impact_synth_8_materials_ms` | 1.22 | 1.03 | 11 |
| `bench.lua_think_50_hooks_ms` | 0.2672 | 0.2108 | 11 |
| `bench.net_snapshot_256_bodies_ms` | 0.0840 | 0.0839 | 11 |
| `bench.particle_fluid_2000_ms` | 4.29 | 3.59 | 11 |
| `bench.rigid_crates_400_ms` | 1.74 | 1.33 | 11 |
| `bench.rigid_raycast_1000_ms` | 0.4603 | 0.4341 | 11 |
| `stress.crates.fps_avg` | 13.49 | 21.93 | 11 |
| `stress.crates.physics_avg_ms` | 0.4754 | 0.4230 | 11 |
| `stress.fps_1pct_low` | 7.37 | 16.28 | 11 |
| `stress.fps_avg` | 13.32 | 21.82 | 11 |
| `stress.frame_p99_ms` | 93.57 | 59.73 | 11 |
| `stress.impacts.fps_avg` | 11.05 | 17.26 | 11 |
| `stress.peak_rss_mb` | 290 | 251 | 11 |
| `stress.walk.fps_avg` | 15.83 | 27.20 | 11 |

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
