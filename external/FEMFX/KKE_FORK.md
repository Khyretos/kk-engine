# KKE's FEMFX fork

AMD FEMFX is vendored here and is **our fork**: part of the Kreative
Kompas Engine, shaped to what KKE needs. Changes are welcome; every one
is listed here and marked `KKE addition` / `KKE change` in the source,
so an upstream diff stays easy to read.

| Change | Files | Why |
|---|---|---|
| Always `-O2`, even in Debug builds | `CMakeLists.txt` | Dense SIMD solver: -O0 made one glass break 200-400 ms/step (BUGS.md BUG-029). |
| `FmIsTetMeshSleeping()` | `inc/AMD_FEMFX.h`, `src/Simulation/FEMFXTetMesh.cpp` | Renderer skips sleeping objects (BUG-028). |
| `FmGetTetMaxStress()` + `FmTetFractureMaterialParams::lastMaxStress` | `inc/AMD_FEMFX.h`, `src/Simulation/FEMFXTetMesh.{h,cpp}`, `src/Simulation/FEMFXUpdateTetState.cpp` | The per-tet stress FEMFX compares against the fracture threshold was computed and thrown away. KKE arms fracture *relative to each tet's settled stress* so props don't break under their own weight (BUG-043, `PhysicsModule::TetSpawnOptions::armFractureAfterSeconds`). |

Candidates for future changes (see OPTIMIZATION.md backlog): sleep
thresholds tuned for debris, per-tet stress relative to rest built in,
contact reports for impact sounds/effects.
