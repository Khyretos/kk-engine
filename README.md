# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `7a9b9fbd7a` (2026-09-28T09:25:07Z) on AMD EPYC 9V45 96-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 106 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1097 | 0.1096 | 100 |
| `bench.audio_mix_32_voices_ms` | 0.0657 | 0.0614 | 106 |
| `bench.cloth_16x24_basic_ms` | 2.91 | 2.90 | 50 |
| `bench.cloth_16x24_full_ms` | 5.84 | 5.31 | 50 |
| `bench.cloth_1x32_basic_ms` | 0.3313 | 0.3282 | 50 |
| `bench.cloth_1x32_full_ms` | 0.7008 | 0.6111 | 50 |
| `bench.cloth_1x32_off_ms` | 0.3293 | 0.3291 | 50 |
| `bench.cloth_1x64_basic_ms` | 1.39 | 1.38 | 50 |
| `bench.cloth_1x64_full_ms` | 24.99 | 5.27 | 50 |
| `bench.cloth_cape_basic_ms` | 0.3094 | 0.3091 | 39 |
| `bench.cloth_cape_full_ms` | 5.26 | 4.63 | 39 |
| `bench.fracture_bake_cube_ms` | 4.62 | 4.62 | 106 |
| `bench.hair_1x100_straight_ms` | 0.1185 | 0.1071 | 45 |
| `bench.hair_1x160_braids_ms` | 0.2855 | 0.2855 | 27 |
| `bench.hair_1x160_cornrows_ms` | 0.0317 | 0.0317 | 27 |
| `bench.hair_1x200_3b_ms` | 0.2824 | 0.2824 | 30 |
| `bench.hair_1x200_4c_ms` | 0.1606 | 0.1606 | 30 |
| `bench.hair_1x200_bantu_ms` | 0.3801 | 0.3801 | 30 |
| `bench.hair_1x400_curly_ms` | 1.27 | 1.17 | 45 |
| `bench.hair_1x400_long_ms` | 0.6286 | 0.5540 | 45 |
| `bench.hair_8x200_long_ms` | 2.45 | 2.16 | 45 |
| `bench.impact_synth_8_materials_ms` | 0.9070 | 0.9068 | 106 |
| `bench.lua_think_50_hooks_ms` | 0.1445 | 0.1408 | 106 |
| `bench.net_snapshot_256_bodies_ms` | 0.0511 | 0.0503 | 106 |
| `bench.particle_fluid_2000_ms` | 2.58 | 2.58 | 106 |
| `bench.rigid_crates_400_ms` | 0.9898 | 0.9864 | 106 |
| `bench.rigid_raycast_1000_ms` | 0.3235 | 0.3228 | 106 |
| `stress.crates.fps_avg` | 18.99 | 22.85 | 106 |
| `stress.crates.physics_avg_ms` | 0.2865 | 0.2801 | 106 |
| `stress.fps_1pct_low` | 11.53 | 16.28 | 106 |
| `stress.fps_avg` | 19.02 | 23.34 | 106 |
| `stress.frame_p99_ms` | 65.37 | 57.86 | 106 |
| `stress.impacts.fps_avg` | 16.14 | 19.00 | 106 |
| `stress.peak_rss_mb` | 394 | 251 | 106 |
| `stress.walk.fps_avg` | 22.48 | 29.14 | 106 |

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

![bench.hair_1x160_braids_ms](charts/bench_hair_1x160_braids_ms.svg)

![bench.hair_1x160_cornrows_ms](charts/bench_hair_1x160_cornrows_ms.svg)

![bench.hair_1x200_3b_ms](charts/bench_hair_1x200_3b_ms.svg)

![bench.hair_1x200_4c_ms](charts/bench_hair_1x200_4c_ms.svg)

![bench.hair_1x200_bantu_ms](charts/bench_hair_1x200_bantu_ms.svg)

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
