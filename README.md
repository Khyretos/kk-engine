# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `7b855bb534` (2026-09-27T00:15:40Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 21 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1886 | 0.1342 | 15 |
| `bench.audio_mix_32_voices_ms` | 0.0934 | 0.0708 | 21 |
| `bench.fracture_bake_cube_ms` | 7.11 | 6.02 | 21 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9487 | 21 |
| `bench.lua_think_50_hooks_ms` | 0.3026 | 0.2108 | 21 |
| `bench.net_snapshot_256_bodies_ms` | 0.0860 | 0.0667 | 21 |
| `bench.particle_fluid_2000_ms` | 3.91 | 3.39 | 21 |
| `bench.rigid_crates_400_ms` | 1.66 | 1.32 | 21 |
| `bench.rigid_raycast_1000_ms` | 0.5021 | 0.3511 | 21 |
| `stress.crates.fps_avg` | 13.96 | 21.93 | 21 |
| `stress.crates.physics_avg_ms` | 0.4321 | 0.3814 | 21 |
| `stress.fps_1pct_low` | 7.83 | 16.28 | 21 |
| `stress.fps_avg` | 13.87 | 21.82 | 21 |
| `stress.frame_p99_ms` | 88.84 | 59.73 | 21 |
| `stress.impacts.fps_avg` | 11.58 | 17.26 | 21 |
| `stress.peak_rss_mb` | 291 | 251 | 21 |
| `stress.walk.fps_avg` | 16.51 | 27.20 | 21 |

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
