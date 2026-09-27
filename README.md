# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `13547e2e38` (2026-09-27T10:24:22Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 52 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1523 | 0.1120 | 46 |
| `bench.audio_mix_32_voices_ms` | 0.0811 | 0.0614 | 52 |
| `bench.fracture_bake_cube_ms` | 6.16 | 4.67 | 52 |
| `bench.impact_synth_8_materials_ms` | 0.9461 | 0.9118 | 52 |
| `bench.lua_think_50_hooks_ms` | 0.2154 | 0.1466 | 52 |
| `bench.net_snapshot_256_bodies_ms` | 0.0655 | 0.0512 | 52 |
| `bench.particle_fluid_2000_ms` | 3.40 | 2.58 | 52 |
| `bench.rigid_crates_400_ms` | 1.36 | 0.9864 | 52 |
| `bench.rigid_raycast_1000_ms` | 0.3584 | 0.3228 | 52 |
| `stress.crates.fps_avg` | 14.09 | 22.85 | 52 |
| `stress.crates.physics_avg_ms` | 0.3733 | 0.3066 | 52 |
| `stress.fps_1pct_low` | 8.22 | 16.28 | 52 |
| `stress.fps_avg` | 14.06 | 23.34 | 52 |
| `stress.frame_p99_ms` | 88.12 | 57.86 | 52 |
| `stress.impacts.fps_avg` | 11.88 | 19.00 | 52 |
| `stress.peak_rss_mb` | 414 | 251 | 52 |
| `stress.walk.fps_avg` | 16.63 | 29.14 | 52 |

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
