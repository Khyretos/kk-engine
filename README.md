# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `43584ebb28` (2026-09-27T05:58:15Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 36 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1890 | 0.1120 | 30 |
| `bench.audio_mix_32_voices_ms` | 0.0935 | 0.0614 | 36 |
| `bench.fracture_bake_cube_ms` | 7.15 | 4.67 | 36 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9118 | 36 |
| `bench.lua_think_50_hooks_ms` | 0.3068 | 0.1466 | 36 |
| `bench.net_snapshot_256_bodies_ms` | 0.0887 | 0.0512 | 36 |
| `bench.particle_fluid_2000_ms` | 3.88 | 2.58 | 36 |
| `bench.rigid_crates_400_ms` | 1.61 | 0.9864 | 36 |
| `bench.rigid_raycast_1000_ms` | 0.4971 | 0.3228 | 36 |
| `stress.crates.fps_avg` | 10.92 | 22.85 | 36 |
| `stress.crates.physics_avg_ms` | 0.4170 | 0.3066 | 36 |
| `stress.fps_1pct_low` | 5.49 | 16.28 | 36 |
| `stress.fps_avg` | 10.98 | 23.34 | 36 |
| `stress.frame_p99_ms` | 109 | 57.86 | 36 |
| `stress.impacts.fps_avg` | 9.40 | 19.00 | 36 |
| `stress.peak_rss_mb` | 413 | 251 | 36 |
| `stress.walk.fps_avg` | 12.95 | 29.14 | 36 |

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
