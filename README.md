# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `0f9f717bab` (2026-09-26T18:38:01Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 4 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_ms` | 0.0929 | 0.0927 | 4 |
| `bench.fracture_bake_cube_ms` | 7.24 | 7.20 | 4 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 1.03 | 4 |
| `bench.lua_think_50_hooks_ms` | 0.2768 | 0.2768 | 4 |
| `bench.net_snapshot_256_bodies_ms` | 0.0885 | 0.0882 | 4 |
| `bench.particle_fluid_2000_ms` | 4.06 | 4.06 | 4 |
| `bench.rigid_crates_400_ms` | 1.60 | 1.59 | 4 |
| `bench.rigid_raycast_1000_ms` | 0.4982 | 0.4921 | 4 |
| `stress.crates.fps_avg` | 15.08 | 21.93 | 4 |
| `stress.crates.physics_avg_ms` | 0.4269 | 0.4269 | 4 |
| `stress.fps_1pct_low` | 8.90 | 16.28 | 4 |
| `stress.fps_avg` | 17.17 | 21.82 | 4 |
| `stress.frame_p99_ms` | 82.78 | 59.73 | 4 |
| `stress.impacts.fps_avg` | 12.38 | 17.26 | 4 |
| `stress.peak_rss_mb` | 308 | 251 | 4 |
| `stress.walk.fps_avg` | 25.42 | 27.20 | 4 |

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
