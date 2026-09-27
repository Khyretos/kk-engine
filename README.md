# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `cc86f0a16b` (2026-09-27T09:35:03Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 48 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1852 | 0.1120 | 42 |
| `bench.audio_mix_32_voices_ms` | 0.0932 | 0.0614 | 48 |
| `bench.fracture_bake_cube_ms` | 7.11 | 4.67 | 48 |
| `bench.impact_synth_8_materials_ms` | 1.08 | 0.9118 | 48 |
| `bench.lua_think_50_hooks_ms` | 0.2850 | 0.1466 | 48 |
| `bench.net_snapshot_256_bodies_ms` | 0.0856 | 0.0512 | 48 |
| `bench.particle_fluid_2000_ms` | 3.92 | 2.58 | 48 |
| `bench.rigid_crates_400_ms` | 1.62 | 0.9864 | 48 |
| `bench.rigid_raycast_1000_ms` | 0.5055 | 0.3228 | 48 |
| `stress.crates.fps_avg` | 11.28 | 22.85 | 48 |
| `stress.crates.physics_avg_ms` | 0.4262 | 0.3066 | 48 |
| `stress.fps_1pct_low` | 5.87 | 16.28 | 48 |
| `stress.fps_avg` | 11.03 | 23.34 | 48 |
| `stress.frame_p99_ms` | 109 | 57.86 | 48 |
| `stress.impacts.fps_avg` | 9.40 | 19.00 | 48 |
| `stress.peak_rss_mb` | 437 | 251 | 48 |
| `stress.walk.fps_avg` | 12.69 | 29.14 | 48 |

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
