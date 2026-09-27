# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `436abbaf39` (2026-09-27T00:11:22Z) on INTEL(R) XEON(R) PLATINUM 8573C, llvmpipe (LLVM 20.1.2, 256 bits). 20 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1357 | 0.1342 | 14 |
| `bench.audio_mix_32_voices_ms` | 0.0730 | 0.0708 | 20 |
| `bench.fracture_bake_cube_ms` | 6.26 | 6.02 | 20 |
| `bench.impact_synth_8_materials_ms` | 1.23 | 0.9487 | 20 |
| `bench.lua_think_50_hooks_ms` | 0.2282 | 0.2108 | 20 |
| `bench.net_snapshot_256_bodies_ms` | 0.0837 | 0.0667 | 20 |
| `bench.particle_fluid_2000_ms` | 3.62 | 3.39 | 20 |
| `bench.rigid_crates_400_ms` | 1.43 | 1.32 | 20 |
| `bench.rigid_raycast_1000_ms` | 0.4487 | 0.3511 | 20 |
| `stress.crates.fps_avg` | 16.59 | 21.93 | 20 |
| `stress.crates.physics_avg_ms` | 0.4408 | 0.3814 | 20 |
| `stress.fps_1pct_low` | 9.92 | 16.28 | 20 |
| `stress.fps_avg` | 16.68 | 21.82 | 20 |
| `stress.frame_p99_ms` | 77.32 | 59.73 | 20 |
| `stress.impacts.fps_avg` | 13.56 | 17.26 | 20 |
| `stress.peak_rss_mb` | 320 | 251 | 20 |
| `stress.walk.fps_avg` | 20.50 | 27.20 | 20 |

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
