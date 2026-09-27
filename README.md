# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `dc1e2d459e` (2026-09-27T00:59:10Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 25 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1559 | 0.1342 | 19 |
| `bench.audio_mix_32_voices_ms` | 0.0808 | 0.0708 | 25 |
| `bench.fracture_bake_cube_ms` | 6.18 | 6.02 | 25 |
| `bench.impact_synth_8_materials_ms` | 0.9483 | 0.9483 | 25 |
| `bench.lua_think_50_hooks_ms` | 0.2222 | 0.2108 | 25 |
| `bench.net_snapshot_256_bodies_ms` | 0.0694 | 0.0667 | 25 |
| `bench.particle_fluid_2000_ms` | 3.43 | 3.39 | 25 |
| `bench.rigid_crates_400_ms` | 1.36 | 1.32 | 25 |
| `bench.rigid_raycast_1000_ms` | 0.3876 | 0.3511 | 25 |
| `stress.crates.fps_avg` | 17.51 | 21.93 | 25 |
| `stress.crates.physics_avg_ms` | 0.3689 | 0.3689 | 25 |
| `stress.fps_1pct_low` | 11.14 | 16.28 | 25 |
| `stress.fps_avg` | 17.70 | 21.82 | 25 |
| `stress.frame_p99_ms` | 70.05 | 59.73 | 25 |
| `stress.impacts.fps_avg` | 14.64 | 17.26 | 25 |
| `stress.peak_rss_mb` | 319 | 251 | 25 |
| `stress.walk.fps_avg` | 21.61 | 27.20 | 25 |

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
