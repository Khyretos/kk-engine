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
- **R1c, the draw buffers (done).** `kke::rhi::Device` (`kke/rhi/Device.h`)
  makes `rhi::Buffer`s (vertex, index, uniform, storage; Upload memory the
  CPU writes, or Device memory filled once), and a draw names one by
  `ref()`. `Application::gpu()` is the frame's device; meshes, debug draw,
  sphere impostors, cloth, particle instances, physics objects and rubble,
  and models' instance, skinned and deformed buffers are made by it.
  `VulkanGpuDevice` wraps `kke::Buffer`; buffers that ray tracing or
  compute skinning also read get their extra Vulkan bits there.
- **R1d, bind groups (done).** The device makes bind group layouts
  (bindings: uniform buffer, storage buffer or texture, and the stages
  that read them) and bind groups, writes what each binding points at and
  takes groups back. Material textures (Application's cache), model
  surfaces, physics materials, GPU particles, hair and particle systems
  make their sets this way instead of sizing pools of their own: Vulkan's
  pools now grow, so the 1024-texture cache limit is gone.
- **R1e, textures (done).** The device makes sampled 2D textures from
  their pixels (every mip level, 8-bit sRGB or linear, or half-float),
  with the sampler a description asks for (repeat, or around the horizon
  only; trilinear or nearest level; anisotropic). `kke::Texture` keeps
  decoding and building mips on the CPU and hands the levels over;
  bind groups name a texture by `ref()`. The decal array, shadow maps
  and post-process targets stay Vulkan for now.
- **R1f, the pipeline state in the RHI's words (done).** `PipelineConfig`
  and the engine's `Vertex` say topology, culling, winding, blending,
  stencil, push constants and vertex layouts with `kke/rhi/PipelineState.h`
  types instead of Vulkan's; `Pipeline` translates them. The 51 pipelines
  read the same; only their spelling changed.
- **R1g, the lighting and shadow sets (done).** Set 0 (the lighting
  uniforms, the environment, GI, reflections, fog, shadow rays, many
  lights, decals) and set 1 (the shadow maps) are bind groups the device
  makes and writes; a texture a compute pass also writes says so
  (`TextureRef::computeWritten`, Vulkan's GENERAL layout).
- **R1h, shaders and pipelines from the device (done).** `ShaderModule`
  (SPIR-V words in; Vulkan keeps a VkShaderModule, the GL backend will turn
  them into GLSL ES) and `GraphicsPipeline` are made by `rhi::Device`
  (`createShaderModule`, `createGraphicsPipeline` from a
  `GraphicsPipelineDesc`: both shaders, the pass as a `RenderPassRef`, the
  state in the RHI's words, bind group layouts). `kke::Pipeline` reads its
  .spv files, describes itself (`Pipeline::describe`) and asks the device;
  layouts a module still makes itself stand in as borrowed bind group
  layouts. The command list still takes `kke::Pipeline`; render passes move
  in R1i. Output must not change (benchmarks within 2%, pictures identical).
- **R1b, the modules moved (done).** Models (scene and shadow), physics
  objects and rubble, particle effects (CPU batches and GPU-stepped),
  particle systems, ocean, hair, sphere impostors and cloth record their
  80 draw-path calls through `ctx.list`.
  `VulkanCommandList::groups` turns descriptor sets into bind groups
  until the device makes them. Still on a VkCommandBuffer: the fluid
  surface (its own render pass), thumbnails, the RmlUi and ImGui
  renderers, the sky and the Renderer's own passes.
- **R1i, the rest of the graphics path.** Renderer's scene, shadow and overlay
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
