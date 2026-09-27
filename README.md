# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `aa5835fb06` (2026-09-27T11:24:16Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 60 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.2010 | 0.1103 | 54 |
| `bench.audio_mix_32_voices_ms` | 0.0947 | 0.0614 | 60 |
| `bench.cloth_16x24_basic_ms` | 4.69 | 3.04 | 4 |
| `bench.cloth_16x24_full_ms` | 9.78 | 5.94 | 4 |
| `bench.cloth_1x32_basic_ms` | 0.5367 | 0.3387 | 4 |
| `bench.cloth_1x32_full_ms` | 1.25 | 0.7056 | 4 |
| `bench.cloth_1x32_off_ms` | 0.5392 | 0.3435 | 4 |
| `bench.cloth_1x64_basic_ms` | 2.26 | 1.44 | 4 |
| `bench.cloth_1x64_full_ms` | 8.34 | 5.27 | 4 |
| `bench.fracture_bake_cube_ms` | 7.21 | 4.67 | 60 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9114 | 60 |
| `bench.lua_think_50_hooks_ms` | 0.2893 | 0.1408 | 60 |
| `bench.net_snapshot_256_bodies_ms` | 0.0910 | 0.0503 | 60 |
| `bench.particle_fluid_2000_ms` | 3.91 | 2.58 | 60 |
| `bench.rigid_crates_400_ms` | 1.66 | 0.9864 | 60 |
| `bench.rigid_raycast_1000_ms` | 0.4929 | 0.3228 | 60 |
| `stress.crates.fps_avg` | 10.74 | 22.85 | 60 |
| `stress.crates.physics_avg_ms` | 0.3894 | 0.2847 | 60 |
| `stress.fps_1pct_low` | 5.47 | 16.28 | 60 |
| `stress.fps_avg` | 10.80 | 23.34 | 60 |
| `stress.frame_p99_ms` | 117 | 57.86 | 60 |
| `stress.impacts.fps_avg` | 9.08 | 19.00 | 60 |
| `stress.peak_rss_mb` | 436 | 251 | 60 |
| `stress.walk.fps_avg` | 12.94 | 29.14 | 60 |

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
