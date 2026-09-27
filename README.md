# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `a08d9a0570` (2026-09-27T09:18:33Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 46 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1857 | 0.1120 | 40 |
| `bench.audio_mix_32_voices_ms` | 0.0932 | 0.0614 | 46 |
| `bench.fracture_bake_cube_ms` | 7.13 | 4.67 | 46 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9118 | 46 |
| `bench.lua_think_50_hooks_ms` | 0.2873 | 0.1466 | 46 |
| `bench.net_snapshot_256_bodies_ms` | 0.0854 | 0.0512 | 46 |
| `bench.particle_fluid_2000_ms` | 3.91 | 2.58 | 46 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 46 |
| `bench.rigid_raycast_1000_ms` | 0.4930 | 0.3228 | 46 |
| `stress.crates.fps_avg` | 11.03 | 22.85 | 46 |
| `stress.crates.physics_avg_ms` | 0.4661 | 0.3066 | 46 |
| `stress.fps_1pct_low` | 5.56 | 16.28 | 46 |
| `stress.fps_avg` | 10.80 | 23.34 | 46 |
| `stress.frame_p99_ms` | 112 | 57.86 | 46 |
| `stress.impacts.fps_avg` | 9.23 | 19.00 | 46 |
| `stress.peak_rss_mb` | 411 | 251 | 46 |
| `stress.walk.fps_avg` | 12.42 | 29.14 | 46 |

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
