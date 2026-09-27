# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `c88665fbc4` (2026-09-27T02:46:54Z) on Intel(R) Xeon(R) 6973P-C, llvmpipe (LLVM 20.1.2, 256 bits). 30 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1161 | 0.1161 | 24 |
| `bench.audio_mix_32_voices_ms` | 0.0614 | 0.0614 | 30 |
| `bench.fracture_bake_cube_ms` | 5.16 | 5.16 | 30 |
| `bench.impact_synth_8_materials_ms` | 1.07 | 0.9483 | 30 |
| `bench.lua_think_50_hooks_ms` | 0.1835 | 0.1835 | 30 |
| `bench.net_snapshot_256_bodies_ms` | 0.0724 | 0.0666 | 30 |
| `bench.particle_fluid_2000_ms` | 3.02 | 3.02 | 30 |
| `bench.rigid_crates_400_ms` | 1.10 | 1.10 | 30 |
| `bench.rigid_raycast_1000_ms` | 0.3757 | 0.3511 | 30 |
| `stress.crates.fps_avg` | 18.86 | 21.93 | 30 |
| `stress.crates.physics_avg_ms` | 0.4174 | 0.3689 | 30 |
| `stress.fps_1pct_low` | 10.78 | 16.28 | 30 |
| `stress.fps_avg` | 19.54 | 21.82 | 30 |
| `stress.frame_p99_ms` | 73.60 | 59.73 | 30 |
| `stress.impacts.fps_avg` | 15.65 | 17.26 | 30 |
| `stress.peak_rss_mb` | 291 | 251 | 30 |
| `stress.walk.fps_avg` | 25.03 | 27.20 | 30 |

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
