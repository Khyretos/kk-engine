---
hide:
  - navigation
---

<p align="center">
  <img src="../assets/branding/banner.png" alt="Kreative Kompas Engine: direct your creativity" width="100%">
</p>

# Kreative Kompas Engine

A modular, open-source Vulkan game engine in C++17, built from "lego
pieces": every gameplay or rendering system is a **module** you plug into an
application, and gameplay can be written in **Lua** that reloads while the
game runs. It aims to run on a single-core machine with no GPU and scale up
from there.

[Make your first game](tutorials/getting-started.md){ .md-button .md-button--primary }
[Download](https://github.com/Khyretos/kk-engine/releases){ .md-button }
[Source on GitHub](https://github.com/Khyretos/kk-engine){ .md-button }

!!! info "KKE is AI-coded"
    The code is written by Claude (Anthropic) working directly in the
    repository, in small slices that are built, run and verified
    (screenshots, logs, tests) before they count as done. Read it and
    question it like any codebase you didn't write yourself.

![Break the targets, a game written only in Lua, running in kke_demo](../games/first_lua_game/screenshot.png)

## What you get

| Area | What you get |
|---|---|
| **Core** | Module system with a clear lifecycle, fixed-timestep loop, per-module fault isolation, async logging, thread pool |
| **Rendering** | Vulkan, PBR, soft shadow maps, GPU particles, instancing, frustum culling |
| **Physics** | Jolt rigid bodies and a character controller; AMD FEMFX deformable bodies that bend, fracture and break; jiggle physics |
| **Simulation** | Particle liquids with temperature, meltable solids, an ocean with buoyancy |
| **Characters** | FBX/OBJ import, skinning, animation blending, locomotion with vaulting, climbing, ledge hang and shimmy, ragdolls |
| **UI** | RmlUi (HTML/CSS-style) game UI, Dear ImGui debug panels, player settings, colour emoji |
| **Audio** | 3D sound, occlusion, synthesized impact sounds, a sound visualizer for deaf and hard-of-hearing players |
| **Input** | Rebindable actions for keyboard, mouse, gamepads, HOTAS and twin sticks |
| **Networking** | Host/join over ENet, LAN discovery, server-authoritative physics |
| **Scripting** | Lua 5.4, Garry's Mod-style hooks and timers, hot reload, sandboxed |

What is solid, partial or not started yet, system by system, is in the
[roadmap](../ROADMAP.md); known defects are in [BUGS.md](../BUGS.md).

## Start here

1. [Install and build](BUILDING.md): one command after the system packages.
2. [Make your own game](tutorials/getting-started.md): copy the starter
   template and run it.
3. [The tutorials](tutorials/index.md): walk around, break things, script
   a pickup, add sound.
4. [Scripting in Lua](SCRIPTING.md) and the [Lua API reference](reference/lua-api.md).

## Support KKE

KKE is built by one person and is free under the MIT licence. If you want
to help it exist, [Ko-fi](https://ko-fi.com/khyretos) is the place. News is
in the [devlog](devlog/index.md).
