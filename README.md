# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `ac01df02de` (2026-09-26T22:48:34Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 16 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1561 | 0.1342 | 10 |
| `bench.audio_mix_32_voices_ms` | 0.0807 | 0.0709 | 16 |
| `bench.fracture_bake_cube_ms` | 6.13 | 6.07 | 16 |
| `bench.impact_synth_8_materials_ms` | 0.9487 | 0.9487 | 16 |
| `bench.lua_think_50_hooks_ms` | 0.2182 | 0.2108 | 16 |
| `bench.net_snapshot_256_bodies_ms` | 0.0667 | 0.0667 | 16 |
| `bench.particle_fluid_2000_ms` | 3.39 | 3.39 | 16 |
| `bench.rigid_crates_400_ms` | 1.38 | 1.33 | 16 |
| `bench.rigid_raycast_1000_ms` | 0.3511 | 0.3511 | 16 |
| `stress.crates.fps_avg` | 17.56 | 21.93 | 16 |
| `stress.crates.physics_avg_ms` | 0.3814 | 0.3814 | 16 |
| `stress.fps_1pct_low` | 10.62 | 16.28 | 16 |
| `stress.fps_avg` | 17.62 | 21.82 | 16 |
| `stress.frame_p99_ms` | 71.76 | 59.73 | 16 |
| `stress.impacts.fps_avg` | 14.31 | 17.26 | 16 |
| `stress.peak_rss_mb` | 321 | 251 | 16 |
| `stress.walk.fps_avg` | 21.66 | 27.20 | 16 |

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
