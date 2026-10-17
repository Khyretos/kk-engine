# Plan: graphics backends and the editor (after the gaps)

Written 2026-10-18 from Kees's questions in the project chat. Nothing here
starts before every row in [GAPS.md](../GAPS.md) is closed: OpenGL and
the editor come after the gaps, in that order.

## 1. RHI, NVRHI, miniaudio, GLFW: what kk-engine has

| Library | In kk-engine? | Verdict |
|---|---|---|
| **miniaudio** (public domain / MIT-0) | Yes, 0.11.22, behind `AudioModule` and `VoiceModule` | Already used; nothing to add |
| **GLFW** (zlib) | No: SDL 3.4 (zlib) does windows, input, gamepads, touch, Android and audio devices | Not needed; GLFW only does windows and input, and SDL does more of both |
| **NVRHI** (MIT, NVIDIA) | No | Does not fit: its backends are D3D11, D3D12 and Vulkan, so it gives **no OpenGL** and no Metal. Its useful idea, automatic barriers and resource states, is worth copying into our own layer |
| **"RHI"** (render hardware interface) | Not as a layer: 41 engine files call Vulkan directly (`VulkanDevice`, `Renderer`, the passes) | This is what an OpenGL backend needs; see section 2 |

Other FOSS layers we considered for the RHI:
- **bgfx** (BSD-2): has GL, GLES, Vulkan, Metal and D3D. It owns its own
  shader language and frame model, and it has no ray query, so our ray
  tracing, GI and refits would not fit in it.
- **Diligent Engine** (Apache-2.0): has GL, GLES, Vulkan, D3D and Metal, plus
  ray tracing. It is large, and it would replace our whole renderer, not
  sit under it.
- **SDL_GPU** (zlib, already in SDL 3): has Vulkan, D3D12 and Metal. It has no
  GL and no ray tracing.

Recommendation: write our own thin RHI, shaped around what the engine
already does (passes, buffers, images, pipelines, barriers), with automatic
barriers in the NVRHI style. Then put a Vulkan backend (today's code, moved)
and an OpenGL backend under it.

## 2. The graphics backend as a module

Kees's idea, the backend as a module so games never change, is sound. It is
how Unreal, Godot and bgfx work. Some things already hold:
- Games don't touch Vulkan. Lua, scenes and assets know nothing about the
  backend, and only one game file calls Vulkan (`games/rmlui_demo/BackdropModule.cpp`).
- Shaders are GLSL compiled to SPIR-V. An OpenGL 4.6 backend can load
  SPIR-V. For GL 4.3 and GLES, SPIRV-Cross (Apache-2.0) turns it back
  into the GLSL they need, as a build step.

Two changes to the idea:
1. **Choose at run time, not only at build time.** One build ships both
   backends and falls back to OpenGL when no Vulkan driver is found, so a
   player never has to pick a download. A build flag can still leave a
   backend out to make the binary smaller (for example a GLES-only Android
   build).
2. **Feature tiers, not identical output.** OpenGL has no ray query and no
   buffer device addresses. macOS stops at GL 4.1, with no compute
   shaders. So the GL tier drops ray-traced reflections (screen-space
   ones still work), turns GI down, and runs cloth, particles and skinning
   on the CPU where there is no compute. `Capabilities` already asks
   "can this GPU do X" (`rayQuery()`), and the tiers build on that.

Costs, honestly: moving 41 files behind the RHI is the biggest refactor
the renderer will have. Every later feature has to be built and tested
twice, and CI needs a GL job (Mesa llvmpipe can run it headless, as
lavapipe does for Vulkan today). What we gain is GPUs from before
2016, older Intel integrated graphics, and GLES 3.x Android devices
without Vulkan. That is a real but shrinking group, and the reason to do
this after the gaps, not before.

Order:
- **R1:** the RHI interfaces, with Vulkan moved under them and no change in
  output. Benchmarks before and after, within 2%.
- **R2:** an OpenGL 4.3 backend at the classic tier, run headless in CI.
- **R3:** GLES 3.1 for Android.
- **R4:** the run-time fallback, plus the tier table in the docs.

## 3. The editor (after alpha)

The concept: **opening a project is playing it.**

- **Launcher**: a small window listing your projects, recent ones first,
  with "new from template" (`tools/new_game`), and where each project
  lives on disk.
- **Live mode (default)**: the project starts as the running game, and the
  editor opens on top of it. Panels read the live world (entities,
  components, Lua state, lights). Lua already hot-reloads, so a script
  you save changes the game you are looking at. Edits made in a panel
  are written back to the scene and data files, so the game you stop is
  the game you saved. Writing live edits back to files is the hard part,
  and it gets its own design: a stable ID per entity, and a serializer
  per component.
- **Classic mode**: the same panels with the game paused, a free camera,
  gizmos, and all the dials, for people who want a usual editor.
- **Panels**: every panel can be docked, torn off, tabbed or moved to any
  side in one drag, and layouts are saved per project. This combines
  Godot's docks with Visual Studio's tab groups. Dear ImGui's docking
  branch (MIT; we use 1.91.6, which has a `-docking` tag) gives this
  without building it ourselves.
- **Look**: a custom kk theme on ImGui (purple primary, orange secondary,
  white text), with our own widgets for curves, colour, timelines and the
  node graph (`sandbox` already has a graph editor). RmlUi (MIT, already
  in the engine) can draw the launcher and splash screens, where a more
  designed, CSS-like look pays off.
- **Icons (FOSS)**: Material Symbols (Apache-2.0), Font Awesome Free
  (icons CC BY 4.0, font SIL OFL 1.1), Lucide (ISC) or Phosphor (MIT) for
  UI icons. Noto Emoji (OFL) or Twemoji (CC BY 4.0) for emoji. They are
  merged into ImGui's font atlas with IconFontCppHeaders (zlib). The
  credits go in `docs/DEPENDENCIES.md`.

Order:
- **ED1:** the launcher.
- **ED2:** live mode with an outliner and an inspector that are read-only.
- **ED3:** edits written back to files.
- **ED4:** docking, saved layouts and the kk theme.
- **ED5:** classic mode with gizmos.
- **ED6:** icons and the stylized widgets.
