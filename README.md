# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `73e2934521` (2026-09-28T12:46:10Z) on AMD EPYC 9V74 80-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 113 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1517 | 0.1096 | 107 |
| `bench.audio_mix_32_voices_ms` | 0.0811 | 0.0614 | 113 |
| `bench.cloth_16x24_basic_ms` | 3.72 | 2.90 | 57 |
| `bench.cloth_16x24_full_ms` | 8.06 | 5.31 | 57 |
| `bench.cloth_1x32_basic_ms` | 0.4239 | 0.3282 | 57 |
| `bench.cloth_1x32_full_ms` | 1.00 | 0.6111 | 57 |
| `bench.cloth_1x32_off_ms` | 0.4226 | 0.3291 | 57 |
| `bench.cloth_1x64_basic_ms` | 1.79 | 1.38 | 57 |
| `bench.cloth_1x64_full_ms` | 34.02 | 5.27 | 57 |
| `bench.cloth_cape_basic_ms` | 0.3955 | 0.3091 | 46 |
| `bench.cloth_cape_full_ms` | 7.07 | 4.63 | 46 |
| `bench.fracture_bake_cube_ms` | 6.19 | 4.62 | 113 |
| `bench.hair_1x100_straight_ms` | 0.1723 | 0.1071 | 52 |
| `bench.hair_1x160_braids_ms` | 0.4129 | 0.2855 | 34 |
| `bench.hair_1x160_cornrows_ms` | 0.0448 | 0.0317 | 34 |
| `bench.hair_1x200_3b_ms` | 0.4134 | 0.2824 | 37 |
| `bench.hair_1x200_4c_ms` | 0.2333 | 0.1606 | 37 |
| `bench.hair_1x200_bantu_ms` | 0.5557 | 0.3801 | 37 |
| `bench.hair_1x400_curly_ms` | 1.86 | 1.17 | 52 |
| `bench.hair_1x400_long_ms` | 0.9137 | 0.5540 | 52 |
| `bench.hair_8x200_long_ms` | 3.57 | 2.16 | 52 |
| `bench.impact_synth_8_materials_ms` | 0.9443 | 0.9068 | 113 |
| `bench.lua_think_50_hooks_ms` | 0.2108 | 0.1408 | 113 |
| `bench.net_snapshot_256_bodies_ms` | 0.0652 | 0.0503 | 113 |
| `bench.particle_fluid_2000_ms` | 3.40 | 2.58 | 113 |
| `bench.rigid_crates_400_ms` | 1.36 | 0.9864 | 113 |
| `bench.rigid_raycast_1000_ms` | 0.3520 | 0.3228 | 113 |
| `stress.crates.fps_avg` | 14.16 | 22.85 | 113 |
| `stress.crates.physics_avg_ms` | 0.3693 | 0.2801 | 113 |
| `stress.fps_1pct_low` | 8.23 | 16.28 | 113 |
| `stress.fps_avg` | 14.06 | 23.34 | 113 |
| `stress.frame_p99_ms` | 88.99 | 57.86 | 113 |
| `stress.impacts.fps_avg` | 11.96 | 19.00 | 113 |
| `stress.peak_rss_mb` | 392 | 251 | 113 |
| `stress.walk.fps_avg` | 16.44 | 29.14 | 113 |

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
