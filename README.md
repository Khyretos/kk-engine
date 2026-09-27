# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `be90a93b4d` (2026-09-27T13:27:47Z) on Intel(R) Xeon(R) 6973P-C, llvmpipe (LLVM 20.1.2, 256 bits). 71 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1159 | 0.1103 | 65 |
| `bench.audio_mix_32_voices_ms` | 0.0616 | 0.0614 | 71 |
| `bench.cloth_16x24_basic_ms` | 2.93 | 2.93 | 15 |
| `bench.cloth_16x24_full_ms` | 5.83 | 5.68 | 15 |
| `bench.cloth_1x32_basic_ms` | 0.3305 | 0.3305 | 15 |
| `bench.cloth_1x32_full_ms` | 0.7087 | 0.6762 | 15 |
| `bench.cloth_1x32_off_ms` | 0.3321 | 0.3321 | 15 |
| `bench.cloth_1x64_basic_ms` | 1.56 | 1.44 | 15 |
| `bench.cloth_1x64_full_ms` | 7.26 | 5.27 | 15 |
| `bench.cloth_cape_basic_ms` | 0.3097 | 0.3097 | 4 |
| `bench.cloth_cape_full_ms` | 5.54 | 4.88 | 4 |
| `bench.fracture_bake_cube_ms` | 5.14 | 4.67 | 71 |
| `bench.hair_1x100_straight_ms` | 0.1169 | 0.1128 | 10 |
| `bench.hair_1x400_curly_ms` | 1.25 | 1.25 | 10 |
| `bench.hair_1x400_long_ms` | 0.6290 | 0.5928 | 10 |
| `bench.hair_8x200_long_ms` | 2.59 | 2.33 | 10 |
| `bench.impact_synth_8_materials_ms` | 1.07 | 0.9114 | 71 |
| `bench.lua_think_50_hooks_ms` | 0.1804 | 0.1408 | 71 |
| `bench.net_snapshot_256_bodies_ms` | 0.0724 | 0.0503 | 71 |
| `bench.particle_fluid_2000_ms` | 3.02 | 2.58 | 71 |
| `bench.rigid_crates_400_ms` | 1.10 | 0.9864 | 71 |
| `bench.rigid_raycast_1000_ms` | 0.3711 | 0.3228 | 71 |
| `stress.crates.fps_avg` | 15.13 | 22.85 | 71 |
| `stress.crates.physics_avg_ms` | 0.3602 | 0.2847 | 71 |
| `stress.fps_1pct_low` | 8.82 | 16.28 | 71 |
| `stress.fps_avg` | 15.59 | 23.34 | 71 |
| `stress.frame_p99_ms` | 86.26 | 57.86 | 71 |
| `stress.impacts.fps_avg` | 13.05 | 19.00 | 71 |
| `stress.peak_rss_mb` | 414 | 251 | 71 |
| `stress.walk.fps_avg` | 19.22 | 29.14 | 71 |

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
