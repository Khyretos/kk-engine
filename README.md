# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `c91d028e9a` (2026-09-27T09:50:28Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 50 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1574 | 0.1120 | 44 |
| `bench.audio_mix_32_voices_ms` | 0.0824 | 0.0614 | 50 |
| `bench.fracture_bake_cube_ms` | 7.01 | 4.67 | 50 |
| `bench.impact_synth_8_materials_ms` | 1.39 | 0.9118 | 50 |
| `bench.lua_think_50_hooks_ms` | 0.2416 | 0.1466 | 50 |
| `bench.net_snapshot_256_bodies_ms` | 0.0966 | 0.0512 | 50 |
| `bench.particle_fluid_2000_ms` | 4.14 | 2.58 | 50 |
| `bench.rigid_crates_400_ms` | 1.56 | 0.9864 | 50 |
| `bench.rigid_raycast_1000_ms` | 0.5442 | 0.3228 | 50 |
| `stress.crates.fps_avg` | 12.11 | 22.85 | 50 |
| `stress.crates.physics_avg_ms` | 0.5053 | 0.3066 | 50 |
| `stress.fps_1pct_low` | 7.26 | 16.28 | 50 |
| `stress.fps_avg` | 12.04 | 23.34 | 50 |
| `stress.frame_p99_ms` | 101 | 57.86 | 50 |
| `stress.impacts.fps_avg` | 10.23 | 19.00 | 50 |
| `stress.peak_rss_mb` | 436 | 251 | 50 |
| `stress.walk.fps_avg` | 14.13 | 29.14 | 50 |

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
