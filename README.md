# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `13cc7c3655` (2026-09-27T04:54:24Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 33 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1156 | 0.1120 | 27 |
| `bench.audio_mix_32_voices_ms` | 0.0687 | 0.0614 | 33 |
| `bench.fracture_bake_cube_ms` | 4.84 | 4.67 | 33 |
| `bench.impact_synth_8_materials_ms` | 0.9373 | 0.9118 | 33 |
| `bench.lua_think_50_hooks_ms` | 0.1466 | 0.1466 | 33 |
| `bench.net_snapshot_256_bodies_ms` | 0.0524 | 0.0512 | 33 |
| `bench.particle_fluid_2000_ms` | 2.65 | 2.58 | 33 |
| `bench.rigid_crates_400_ms` | 1.07 | 0.9864 | 33 |
| `bench.rigid_raycast_1000_ms` | 0.3414 | 0.3228 | 33 |
| `stress.crates.fps_avg` | 22.85 | 22.85 | 33 |
| `stress.crates.physics_avg_ms` | 0.3066 | 0.3066 | 33 |
| `stress.fps_1pct_low` | 12.97 | 16.28 | 33 |
| `stress.fps_avg` | 23.34 | 23.34 | 33 |
| `stress.frame_p99_ms` | 58.23 | 57.86 | 33 |
| `stress.impacts.fps_avg` | 19.00 | 19.00 | 33 |
| `stress.peak_rss_mb` | 322 | 251 | 33 |
| `stress.walk.fps_avg` | 29.14 | 29.14 | 33 |

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
