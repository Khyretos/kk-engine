# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `ac90faa0b3` (2026-09-27T03:19:08Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 32 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1120 | 0.1120 | 26 |
| `bench.audio_mix_32_voices_ms` | 0.0660 | 0.0614 | 32 |
| `bench.fracture_bake_cube_ms` | 4.67 | 4.67 | 32 |
| `bench.impact_synth_8_materials_ms` | 0.9118 | 0.9118 | 32 |
| `bench.lua_think_50_hooks_ms` | 0.1578 | 0.1578 | 32 |
| `bench.net_snapshot_256_bodies_ms` | 0.0512 | 0.0512 | 32 |
| `bench.particle_fluid_2000_ms` | 2.58 | 2.58 | 32 |
| `bench.rigid_crates_400_ms` | 0.9864 | 0.9864 | 32 |
| `bench.rigid_raycast_1000_ms` | 0.3228 | 0.3228 | 32 |
| `stress.crates.fps_avg` | 22.48 | 22.48 | 32 |
| `stress.crates.physics_avg_ms` | 0.3172 | 0.3172 | 32 |
| `stress.fps_1pct_low` | 13.42 | 16.28 | 32 |
| `stress.fps_avg` | 22.89 | 22.89 | 32 |
| `stress.frame_p99_ms` | 57.86 | 57.86 | 32 |
| `stress.impacts.fps_avg` | 18.56 | 18.56 | 32 |
| `stress.peak_rss_mb` | 320 | 251 | 32 |
| `stress.walk.fps_avg` | 28.61 | 28.61 | 32 |

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
