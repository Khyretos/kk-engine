# RESEARCH.md: where KKE looks for better ways to do things

Sources the engine's work starts from when a gap is closed or something is
optimised. The rule for every gap still holds (docs/GAPS.md): beat the
reference design, measure against a baseline, and put the numbers in the
metrics row and the pull request.

## Research groups and authors to follow

- **[Yuanming Hu's publications](https://yzhu.io)** (Kees, 2026-10-18):
  physics simulation on GPUs (material point method, differentiable
  physics, the Taichi language). The first place to look for fast
  liquids, snow, sand and soft bodies.

## Papers we plan to build on

| Paper | For | Plan |
|---|---|---|
| Hu et al., "A Moving Least Squares Material Point Method with Displacement Discontinuity and Two-Way Rigid Body Coupling", SIGGRAPH 2018 ([PDF](https://yzhu.io/publication/mpmmls2018siggraph/paper.pdf)) | ENG-G12 liquids | A GPU MLS-MPM water solver (compute shaders) with two-way coupling to Jolt rigid bodies: floating crates push the water and the water pushes them. Benchmarked against the current particle fluid. **Done:** `kke::MpmWater` + `kke::MpmWaterGpu` + `kke::WaterBodyLink`, games/water_demo (OPTIMIZATION.md #46). Not taken from the paper: its displacement discontinuity (cutting thin shells), which water against boxes doesn't need. |
| Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015 | ENG-F03 volumetric fog | Done: the energy-conserving integration step, without the jitter and the temporal filter (docs/RENDERING_PRINCIPLES.md). |
| Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 | PBR, reflections | Done: the split-sum environment lighting. |
| Quilez, "Better Fog" | Height fog | Done: the analytic exponential height fog. |

## Engines we learn from

Read for techniques, not taken as dependencies (reviewed 2026-10-25,
Kees's ask; review page <https://claude.ai/artifact/AJXnSvsvhYnsTr3GUAeiN1>).
No code is copied from them; copying any would need Kees's OK first.

| Project | Licence | What we took | Still to try |
|---|---|---|---|
| [Galaxy Engine](https://github.com/NarcisCalin/Galaxy-Engine) (Narcis Calin), a C++/raylib particle simulation sandbox | MIT | Its SPH neighbour search: particles sorted into a cell-ordered copy so the 27-cell queries read contiguous memory, every per-particle pass split over the cores, and the same queries on the GPU (spatial hash in a compute shader). Done for kke::ParticleFluid (docs/OPTIMIZATION.md #44, #45) | Particle-to-particle gravity: a Barnes-Hut tree over Morton-sorted particles walked on the GPU without a stack (`src/Physics/morton.cpp`, `quadtree.cpp`, `globalLogic.cpp` there), for space-style games |
| [GalaxyCore / Galaxy Network](https://github.com/defraswiew/GalaxyCore), a C# netcode SDK for Unity, Stride and UNIGINE | None published (closed DLLs): not FOSS, not usable | Nothing yet | A broad phase for server fog of war past ~32 players (`net/Visibility.cpp` checks every pair): its measurements favour a dynamic AABB tree for moving players (~0.6-1.9 ms per 1,000 moves among 100k objects, against ~1 s for an octree) |

## Rendering references

[docs/RENDERING_PRINCIPLES.md](RENDERING_PRINCIPLES.md) holds the rendering
arguments with their sources (the Fox Engine talks and others), and
[docs/HISTORY.md](HISTORY.md) the early Vulkan learning material (Sascha
Willems' samples, LearnOpenGL, vkguide.dev).
