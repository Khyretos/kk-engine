# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `766b2b9d09` (2026-09-26T22:32:26Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 14 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1876 | 0.1342 | 8 |
| `bench.audio_mix_32_voices_ms` | 0.0931 | 0.0709 | 14 |
| `bench.fracture_bake_cube_ms` | 7.17 | 6.07 | 14 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 1.03 | 14 |
| `bench.lua_think_50_hooks_ms` | 0.2914 | 0.2108 | 14 |
| `bench.net_snapshot_256_bodies_ms` | 0.0883 | 0.0839 | 14 |
| `bench.particle_fluid_2000_ms` | 3.90 | 3.59 | 14 |
| `bench.rigid_crates_400_ms` | 1.71 | 1.33 | 14 |
| `bench.rigid_raycast_1000_ms` | 0.4972 | 0.4341 | 14 |
| `stress.crates.fps_avg` | 13.73 | 21.93 | 14 |
| `stress.crates.physics_avg_ms` | 0.4357 | 0.4230 | 14 |
| `stress.fps_1pct_low` | 7.49 | 16.28 | 14 |
| `stress.fps_avg` | 13.56 | 21.82 | 14 |
| `stress.frame_p99_ms` | 92.57 | 59.73 | 14 |
| `stress.impacts.fps_avg` | 11.20 | 17.26 | 14 |
| `stress.peak_rss_mb` | 321 | 251 | 14 |
| `stress.walk.fps_avg` | 16.22 | 27.20 | 14 |

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
