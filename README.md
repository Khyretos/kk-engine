# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `60ebebc9d6` (2026-09-26T18:40:53Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 5 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_ms` | 0.0927 | 0.0927 | 5 |
| `bench.fracture_bake_cube_ms` | 7.20 | 7.20 | 5 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 1.03 | 5 |
| `bench.lua_think_50_hooks_ms` | 0.2900 | 0.2768 | 5 |
| `bench.net_snapshot_256_bodies_ms` | 0.0857 | 0.0857 | 5 |
| `bench.particle_fluid_2000_ms` | 3.89 | 3.89 | 5 |
| `bench.rigid_crates_400_ms` | 1.70 | 1.59 | 5 |
| `bench.rigid_raycast_1000_ms` | 0.4934 | 0.4921 | 5 |
| `stress.crates.fps_avg` | 15.33 | 21.93 | 5 |
| `stress.crates.physics_avg_ms` | 0.4397 | 0.4269 | 5 |
| `stress.fps_1pct_low` | 9.14 | 16.28 | 5 |
| `stress.fps_avg` | 17.38 | 21.82 | 5 |
| `stress.frame_p99_ms` | 81.81 | 59.73 | 5 |
| `stress.impacts.fps_avg` | 12.56 | 17.26 | 5 |
| `stress.peak_rss_mb` | 308 | 251 | 5 |
| `stress.walk.fps_avg` | 25.58 | 27.20 | 5 |

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
