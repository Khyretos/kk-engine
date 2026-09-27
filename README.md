# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `92a4b38ceb` (2026-09-27T11:19:38Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 59 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1147 | 0.1103 | 53 |
| `bench.audio_mix_32_voices_ms` | 0.0689 | 0.0614 | 59 |
| `bench.cloth_16x24_basic_ms` | 3.04 | 3.04 | 3 |
| `bench.cloth_16x24_full_ms` | 5.94 | 5.94 | 3 |
| `bench.cloth_1x32_basic_ms` | 0.3387 | 0.3387 | 3 |
| `bench.cloth_1x32_full_ms` | 0.7056 | 0.7056 | 3 |
| `bench.cloth_1x32_off_ms` | 0.3435 | 0.3435 | 3 |
| `bench.cloth_1x64_basic_ms` | 1.44 | 1.44 | 3 |
| `bench.cloth_1x64_full_ms` | 5.27 | 5.27 | 3 |
| `bench.fracture_bake_cube_ms` | 4.78 | 4.67 | 59 |
| `bench.impact_synth_8_materials_ms` | 0.9460 | 0.9114 | 59 |
| `bench.lua_think_50_hooks_ms` | 0.1408 | 0.1408 | 59 |
| `bench.net_snapshot_256_bodies_ms` | 0.0545 | 0.0503 | 59 |
| `bench.particle_fluid_2000_ms` | 2.67 | 2.58 | 59 |
| `bench.rigid_crates_400_ms` | 1.00 | 0.9864 | 59 |
| `bench.rigid_raycast_1000_ms` | 0.3360 | 0.3228 | 59 |
| `stress.crates.fps_avg` | 18.57 | 22.85 | 59 |
| `stress.crates.physics_avg_ms` | 0.2847 | 0.2847 | 59 |
| `stress.fps_1pct_low` | 12.45 | 16.28 | 59 |
| `stress.fps_avg` | 18.82 | 23.34 | 59 |
| `stress.frame_p99_ms` | 63.01 | 57.86 | 59 |
| `stress.impacts.fps_avg` | 16.37 | 19.00 | 59 |
| `stress.peak_rss_mb` | 412 | 251 | 59 |
| `stress.walk.fps_avg` | 22.06 | 29.14 | 59 |

![bench.audio_mix_32_voices_full_ms](charts/bench_audio_mix_32_voices_full_ms.svg)

![bench.audio_mix_32_voices_ms](charts/bench_audio_mix_32_voices_ms.svg)

![bench.cloth_16x24_basic_ms](charts/bench_cloth_16x24_basic_ms.svg)

![bench.cloth_16x24_full_ms](charts/bench_cloth_16x24_full_ms.svg)

![bench.cloth_1x32_basic_ms](charts/bench_cloth_1x32_basic_ms.svg)

![bench.cloth_1x32_full_ms](charts/bench_cloth_1x32_full_ms.svg)

![bench.cloth_1x32_off_ms](charts/bench_cloth_1x32_off_ms.svg)

![bench.cloth_1x64_basic_ms](charts/bench_cloth_1x64_basic_ms.svg)

![bench.cloth_1x64_full_ms](charts/bench_cloth_1x64_full_ms.svg)

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
