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
| Hu et al., "A Moving Least Squares Material Point Method with Displacement Discontinuity and Two-Way Rigid Body Coupling", SIGGRAPH 2018 ([PDF](https://yzhu.io/publication/mpmmls2018siggraph/paper.pdf)) | ENG-G12 liquids | A GPU MLS-MPM water solver (compute shaders) with two-way coupling to Jolt rigid bodies: floating crates push the water and the water pushes them. Benchmarked against the current particle fluid. |
| Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite", SIGGRAPH 2015 | ENG-F03 volumetric fog | Done: the energy-conserving integration step, without the jitter and the temporal filter (docs/RENDERING_PRINCIPLES.md). |
| Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 | PBR, reflections | Done: the split-sum environment lighting. |
| Quilez, "Better Fog" | Height fog | Done: the analytic exponential height fog. |

## Rendering references

[docs/RENDERING_PRINCIPLES.md](RENDERING_PRINCIPLES.md) holds the rendering
arguments with their sources (the Fox Engine talks and others), and
[docs/HISTORY.md](HISTORY.md) the early Vulkan learning material (Sascha
Willems' samples, LearnOpenGL, vkguide.dev).
