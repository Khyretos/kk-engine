# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `f34abdd530` (2026-09-27T13:06:32Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 70 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1173 | 0.1103 | 64 |
| `bench.audio_mix_32_voices_ms` | 0.0704 | 0.0614 | 70 |
| `bench.cloth_16x24_basic_ms` | 3.11 | 3.04 | 14 |
| `bench.cloth_16x24_full_ms` | 5.68 | 5.68 | 14 |
| `bench.cloth_1x32_basic_ms` | 0.3533 | 0.3387 | 14 |
| `bench.cloth_1x32_full_ms` | 0.6762 | 0.6762 | 14 |
| `bench.cloth_1x32_off_ms` | 0.3537 | 0.3435 | 14 |
| `bench.cloth_1x64_basic_ms` | 1.48 | 1.44 | 14 |
| `bench.cloth_1x64_full_ms` | 6.76 | 5.27 | 14 |
| `bench.cloth_cape_basic_ms` | 0.3311 | 0.3311 | 3 |
| `bench.cloth_cape_full_ms` | 4.88 | 4.88 | 3 |
| `bench.fracture_bake_cube_ms` | 4.96 | 4.67 | 70 |
| `bench.hair_1x100_straight_ms` | 0.1128 | 0.1128 | 9 |
| `bench.hair_1x400_curly_ms` | 1.27 | 1.27 | 9 |
| `bench.hair_1x400_long_ms` | 0.5928 | 0.5928 | 9 |
| `bench.hair_8x200_long_ms` | 2.33 | 2.33 | 9 |
| `bench.impact_synth_8_materials_ms` | 0.9702 | 0.9114 | 70 |
| `bench.lua_think_50_hooks_ms` | 0.1520 | 0.1408 | 70 |
| `bench.net_snapshot_256_bodies_ms` | 0.0564 | 0.0503 | 70 |
| `bench.particle_fluid_2000_ms` | 2.75 | 2.58 | 70 |
| `bench.rigid_crates_400_ms` | 1.10 | 0.9864 | 70 |
| `bench.rigid_raycast_1000_ms` | 0.3448 | 0.3228 | 70 |
| `stress.crates.fps_avg` | 16.93 | 22.85 | 70 |
| `stress.crates.physics_avg_ms` | 0.3110 | 0.2847 | 70 |
| `stress.fps_1pct_low` | 10.45 | 16.28 | 70 |
| `stress.fps_avg` | 16.99 | 23.34 | 70 |
| `stress.frame_p99_ms` | 74.07 | 57.86 | 70 |
| `stress.impacts.fps_avg` | 14.38 | 19.00 | 70 |
| `stress.peak_rss_mb` | 415 | 251 | 70 |
| `stress.walk.fps_avg` | 20.21 | 29.14 | 70 |

![bench.audio_mix_32_voices_full_ms](charts/bench_audio_mix_32_voices_full_ms.svg)

![bench.audio_mix_32_voices_ms](charts/bench_audio_mix_32_voices_ms.svg)

![bench.cloth_16x24_basic_ms](charts/bench_cloth_16x24_basic_ms.svg)

![bench.cloth_16x24_full_ms](charts/bench_cloth_16x24_full_ms.svg)

![bench.cloth_1x32_basic_ms](charts/bench_cloth_1x32_basic_ms.svg)

![bench.cloth_1x32_full_ms](charts/bench_cloth_1x32_full_ms.svg)

![bench.cloth_1x32_off_ms](charts/bench_cloth_1x32_off_ms.svg)

![bench.cloth_1x64_basic_ms](charts/bench_cloth_1x64_basic_ms.svg)

![bench.cloth_1x64_full_ms](charts/bench_cloth_1x64_full_ms.svg)

![bench.cloth_cape_basic_ms](charts/bench_cloth_cape_basic_ms.svg)

![bench.cloth_cape_full_ms](charts/bench_cloth_cape_full_ms.svg)

![bench.fracture_bake_cube_ms](charts/bench_fracture_bake_cube_ms.svg)

![bench.hair_1x100_straight_ms](charts/bench_hair_1x100_straight_ms.svg)

![bench.hair_1x400_curly_ms](charts/bench_hair_1x400_curly_ms.svg)

![bench.hair_1x400_long_ms](charts/bench_hair_1x400_long_ms.svg)

![bench.hair_8x200_long_ms](charts/bench_hair_8x200_long_ms.svg)

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
