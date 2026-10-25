# Windows test: FAILED

- **Runner:** Kees's PC (native, lavapipe)
- **Vulkan device:** llvmpipe (LLVM 22.1.8, 256 bits)

## What failed

- 2 benchmark run(s) did not finish cleanly

## Unit tests

1267 tests, 0 failed, 0 errors.

## Demos (kke_benchmark)

| Demo | Status | FPS (CPU Vulkan) | Why |
|---|---|---|---|
| showcase_walk | ✅ ok | 3.8 | normal exit |
| showcase_stress | ✅ ok | 3.6 | normal exit |
| physics_demo | ❌ hung | 2.6 | normal exit |
| melt_demo | ❌ hung | - | exited with code 1 (0x00000001) |
| jiggle_demo | ✅ ok | 11.9 | normal exit |
| sea_demo | ✅ ok | 13.3 | normal exit |
| audio_demo | ✅ ok | 6.4 | normal exit |
| rmlui_demo | ✅ ok | 11.9 | normal exit |
| imgui_demo | ✅ ok | 17.0 | normal exit |
| sandbox | ✅ ok | 16.9 | normal exit |
| synty_demo | ✅ ok | 17.0 | normal exit |
| climb_race | ✅ ok | 7.0 | normal exit |
| flying_pileup | ✅ ok | 9.1 | normal exit |
| procedural_demo | ✅ ok | 7.6 | normal exit |
| cloth_stress | ✅ ok | 6.2 | normal exit |
| cloth_hair | ✅ ok | 0.1 | normal exit |
| cloth_tour | ✅ ok | 7.0 | normal exit |
| farm_demo | ✅ ok | 12.1 | normal exit |
| pet_companion | ✅ ok | 15.0 | normal exit |
| platoon | ✅ ok | 5.9 | normal exit |
| duel | ✅ ok | 8.4 | normal exit |
| goblin_horde | ✅ ok | 7.5 | normal exit |
| racing_pileup | ✅ ok | 4.5 | normal exit |
| tennis_singles | ✅ ok | 2.5 | normal exit |
| tennis_doubles | ✅ ok | 2.5 | normal exit |
| cookbook | ✅ ok | 12.1 | normal exit |

Frame rates come from lavapipe (Vulkan on the CPU): they show a demo runs, not how fast a real GPU would be.
