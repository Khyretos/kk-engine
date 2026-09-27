# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `3c0114a6be` (2026-09-27T09:04:34Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 45 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1851 | 0.1120 | 39 |
| `bench.audio_mix_32_voices_ms` | 0.0937 | 0.0614 | 45 |
| `bench.fracture_bake_cube_ms` | 7.20 | 4.67 | 45 |
| `bench.impact_synth_8_materials_ms` | 1.03 | 0.9118 | 45 |
| `bench.lua_think_50_hooks_ms` | 0.2935 | 0.1466 | 45 |
| `bench.net_snapshot_256_bodies_ms` | 0.0911 | 0.0512 | 45 |
| `bench.particle_fluid_2000_ms` | 3.90 | 2.58 | 45 |
| `bench.rigid_crates_400_ms` | 1.61 | 0.9864 | 45 |
| `bench.rigid_raycast_1000_ms` | 0.4979 | 0.3228 | 45 |
| `stress.crates.fps_avg` | 11.26 | 22.85 | 45 |
| `stress.crates.physics_avg_ms` | 0.4093 | 0.3066 | 45 |
| `stress.fps_1pct_low` | 5.79 | 16.28 | 45 |
| `stress.fps_avg` | 11.00 | 23.34 | 45 |
| `stress.frame_p99_ms` | 108 | 57.86 | 45 |
| `stress.impacts.fps_avg` | 9.42 | 19.00 | 45 |
| `stress.peak_rss_mb` | 412 | 251 | 45 |
| `stress.walk.fps_avg` | 12.57 | 29.14 | 45 |

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
