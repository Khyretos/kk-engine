# KKE benchmark history

Written by `benchmarks/track.py` from the Benchmarks workflow on every push to `main`; don't edit by hand. What the numbers mean: [docs/BENCHMARKS.md](https://github.com/Khyretos/kk-engine/blob/main/docs/BENCHMARKS.md).

Latest: `1ac71efad2` (2026-09-27T13:37:08Z) on AMD EPYC 7763 64-Core Processor                , llvmpipe (LLVM 20.1.2, 256 bits). 73 run(s) tracked.

Every run is on a shared GitHub runner (4 vCPUs, lavapipe software Vulkan), so single points wobble; look for steps that stay.

| Metric | Latest | Best | Runs |
|---|---:|---:|---:|
| `bench.audio_mix_32_voices_full_ms` | 0.1897 | 0.1096 | 67 |
| `bench.audio_mix_32_voices_ms` | 0.0975 | 0.0614 | 73 |
| `bench.cloth_16x24_basic_ms` | 4.67 | 2.91 | 17 |
| `bench.cloth_16x24_full_ms` | 8.72 | 5.31 | 17 |
| `bench.cloth_1x32_basic_ms` | 0.5316 | 0.3282 | 17 |
| `bench.cloth_1x32_full_ms` | 1.03 | 0.6111 | 17 |
| `bench.cloth_1x32_off_ms` | 0.5323 | 0.3291 | 17 |
| `bench.cloth_1x64_basic_ms` | 2.24 | 1.38 | 17 |
| `bench.cloth_1x64_full_ms` | 9.97 | 5.27 | 17 |
| `bench.cloth_cape_basic_ms` | 0.5055 | 0.3091 | 6 |
| `bench.cloth_cape_full_ms` | 7.68 | 4.63 | 6 |
| `bench.fracture_bake_cube_ms` | 7.22 | 4.63 | 73 |
| `bench.hair_1x100_straight_ms` | 0.2016 | 0.1071 | 12 |
| `bench.hair_1x400_curly_ms` | 2.20 | 1.17 | 12 |
| `bench.hair_1x400_long_ms` | 1.06 | 0.5540 | 12 |
| `bench.hair_8x200_long_ms` | 4.15 | 2.16 | 12 |
| `bench.impact_synth_8_materials_ms` | 1.04 | 0.9068 | 73 |
| `bench.lua_think_50_hooks_ms` | 0.2890 | 0.1408 | 73 |
| `bench.net_snapshot_256_bodies_ms` | 0.0884 | 0.0503 | 73 |
| `bench.particle_fluid_2000_ms` | 3.88 | 2.58 | 73 |
| `bench.rigid_crates_400_ms` | 1.60 | 0.9864 | 73 |
| `bench.rigid_raycast_1000_ms` | 0.4949 | 0.3228 | 73 |
| `stress.crates.fps_avg` | 10.77 | 22.85 | 73 |
| `stress.crates.physics_avg_ms` | 0.4673 | 0.2847 | 73 |
| `stress.fps_1pct_low` | 5.53 | 16.28 | 73 |
| `stress.fps_avg` | 10.58 | 23.34 | 73 |
| `stress.frame_p99_ms` | 119 | 57.86 | 73 |
| `stress.impacts.fps_avg` | 9.00 | 19.00 | 73 |
| `stress.peak_rss_mb` | 392 | 251 | 73 |
| `stress.walk.fps_avg` | 12.26 | 29.14 | 73 |

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
