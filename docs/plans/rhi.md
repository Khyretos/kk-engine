# Plan: the RHI, one OpenGL ES 3 / WebGL2 backend, and a web export

Decided by Kees (2026-10-18): right after ENG-F13, build the RHI, then one
OpenGL ES 3 backend that also is WebGL2, then a `web` export with one demo
playing in the browser, then back to the gap list (F14 on). Extends
[backends-and-editor.md](backends-and-editor.md) sections 1 and 2.

## What the GL tier draws, and what it doesn't

WebGL2 is OpenGL ES 3.0: no compute shaders, no storage buffers, no ray
queries. So the GL tier is the classic forward renderer:

| Kept on GL | Off on GL (Vulkan only, the feature tiers) |
|---|---|
| The scene pass: lit models, terrain, sky, water, physics objects, particles (closed form), decals not (they read storage buffers) | Screen-space GI, reflections, refraction, volumetric fog, ray-traced anything |
| Cascaded shadows and point shadows | SMAA, TAA, bloom, depth of field, motion blur (all compute) |
| MSAA, the tone map pass (fragment), LUT grading | Many lights (clustered: compute + storage buffers): GL lights the 16 key lights |
| RmlUi and ImGui | GPU particles stepped, GPU cloth, GPU skinning, occlusion's depth map |

That halves the RHI: the ~20 compute passes stay as they are, behind a
"Vulkan only" door the GL tier never opens. Only the graphics path moves.

## Steps

- **R1a, the command list (done).** `kke::rhi::CommandList`
  (`kke/rhi/CommandList.h`): bind a pipeline, bind groups (what a
  descriptor set binds), push constants, vertex and index buffers, draws,
  viewport and scissor: every command the graphics path records (the
  survey: 30 push constants, 28 set binds, 20 vertex binds, 25 draws, 11
  index binds, 9 viewport and scissor). `RenderContext::list` and
  `ShadowRenderContext::list` carry the frame's; `VulkanCommandList` is
  each call's one vkCmd*. `Pipeline::bind` and `Mesh` draw through it,
  and the grid and debug-draw modules are the first moved.
- **R1a then the resources.** `kke::rhi` Device: Buffer, Texture (2D,
  array, cube, 3D), Sampler, ShaderModule (SPIR-V in, the backend
  converts), GraphicsPipeline, BindGroupLayout and BindGroup made by the
  device instead of each module's descriptor-set code. Vulkan implements
  each by wrapping today's code; output must not change (benchmarks within
  2%, pictures identical).
- **R1b, the modules moved (done).** Models (scene and shadow), physics
  objects and rubble, particle effects (CPU batches and GPU-stepped),
  particle systems, ocean, hair, sphere impostors and cloth record their
  80 draw-path calls through `ctx.list`.
  `VulkanCommandList::groups` turns descriptor sets into bind groups
  until the device makes them. Still on a VkCommandBuffer: the fluid
  surface (its own render pass), thumbnails, the RmlUi and ImGui
  renderers, the sky and the Renderer's own passes.
- **R1c, the rest of the graphics path.** Renderer's scene, shadow and overlay
  passes, Pipeline, Mesh, Texture, ShadowMap, LightingBuffer's uniform
  part, SkyRenderer, and every module's render(): ModelModule,
  PhysicsModule, ParticleEffects' instanced path, DebugDraw, Grid, Ocean,
  the RmlUi and ImGui renderers. A module's render() takes a CommandList
  instead of a VkCommandBuffer. Compute passes keep a VkCommandBuffer.
- **R2, OpenGL ES 3.** One backend for desktop GL 3.3 core, GLES 3.0
  (Android without Vulkan) and WebGL2. Shaders: the same GLSL, compiled to
  SPIR-V as now, turned into GLSL ES 300 by SPIRV-Cross (Apache-2.0) at
  build time; push constants become a uniform buffer, descriptor sets
  become binding points. CI runs it headless on Mesa llvmpipe.
- **R3, the web export.** Emscripten (SDL3, Jolt, Lua, RmlUi, miniaudio all
  build for it) and the GL backend as WebGL2. `kke_export web <game>`
  writes `index.html`, the `.wasm` and a data file: it plays from our site
  and uploads to itch.io as it is. Multiplayer over WebSockets (browsers
  have no UDP); FEMFX waits for its SIMD port (G11).
- **R4, the run-time choice.** Vulkan when the driver is there, GL
  otherwise; the settings menu greys out what the GL tier can't do, and
  the project still lets a developer turn everything on (Kees's rule).

Each step is its own PR, drafted by the Coder from Claude's checked
reference, with the gap rule: beat the reference design and say by how
much.
