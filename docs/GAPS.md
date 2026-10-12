# GAPS.md: what kk-engine still misses, in the order it gets closed

Written 2026-10-12 from ROADMAP.md (🟡 and 🔴 rows), BUGS.md (open rows),
ACTION_PLAN.md, the open pull requests on Forgejo and the engine part of
the Kreative Kompanion big-projects plan (E0 and E1; its E2 feature list,
demo games, docs rewrite, MCP server and assistant model come after this
list). Each gap is one task: a branch, a pull request, and a row here that
moves to "Done" with its PR number.

How a task is done: the local Coder (Ollama `qwen3.5:9b-q8_0` on
soucouyant) drafts every code unit, up to 3 attempts fed with its own
build and test errors; Claude reviews, corrects only what is left, and
records each correction as a numbered lesson in
[docs/lessons-learned.md](lessons-learned.md) with the exact situation in
`docs/qwen-situations/<lesson>/`. Per-task metrics go in
`docs/qwen-log/<task>.jsonl` and the task's row in
[docs/qwen-log/README.md](qwen-log/README.md). A pull request merges only
when every CI check is green on its final head.

Status: ⬜ not started, 🔨 in progress, ✅ done.

## E0: align the copies

| ID | Gap | Done when | Status |
|---|---|---|---|
| ENG-A01 | Kees's checkout on soucouyant (`/media/development/Software/kk-engine`) is 17 commits behind Forgejo main. | It fast-forwards to Forgejo main; his untracked animation folders are untouched. | ⬜ |
| ENG-A02 | Open PR #24 (Android build with FEMFX off) says the FEMFX-on build and an Android build were not verified. | Both builds pass with `-Werror`; the PR is merged with green CI. | ⬜ |
| ENG-A03 | The nightly lint bot's PRs #23, #25 and #26 have no CI status (bot pushes start no workflows). | Each one gets a lint run and is merged, or is closed because a newer one replaces it. | ⬜ |

## E1a: open bugs (BUGS.md)

| ID | Bug | Done when | Status |
|---|---|---|---|
| ENG-B067 | rmlui_demo trader: "Fine, thirty-five" charges 40 gold, the inventory gold does not refresh after paying, the Mana Draught does nothing. | The deal charges 35, the gold label updates, the draught restores 30 mana; a test covers the price. | ⬜ |
| ENG-B068 | `SM_Prop_Barrel_01` is in both the `crate` and `barrel` play blocks, so Barrel recipes never fire. | `blockOf("SM_Prop_Barrel_01")` is `barrel`; a test checks no mesh is in two blocks. | ✅ PR #27 |
| ENG-B069 | Sandbox graph editor: a change and a Done click in one frame return `Closed`, so the graph is not recompiled. | A changed graph closed in the same frame is marked dirty and recompiled; a test covers it. | ⬜ |
| ENG-B072 | duel: a round won on time still shows "K.O."; two-player mode re-adds both AI agents every frame; only player 1 can skip the wait between rounds. | "Time" shows for a timeout, the agents are set once, either player skips. | ⬜ |
| ENG-B074 | `KKE_UI_ROOT`, `KKE_SHOWCASE_START` and most `KKE_CLIMB_*` switches use `std::getenv`, so shipping builds keep them. | They go through `kke::dev::env`; the shipping build ignores them. | ⬜ |
| ENG-B075 | Command kit: `CommandInput` has a fixed 8-slot wheel while `CommandHud` lays out any count; `Scenery` scans every pack; a tree's collider includes its canopy. | The wheel follows `setWheel`'s count, `Scenery` takes `onlyPacks`, trees collide on the trunk only. | ⬜ |
| ENG-B076 | The `modules` lists in sandbox, showcase, rmlui_demo and imgui_demo's game.json leave out modules the games add; some descriptions are stale. | Every list matches the modules the game adds; a test compares them. | ⬜ |
| ENG-B087 | GCC 16 Release: third-party opus and sqlite still warn (`-Wstringop-overflow`, `-Wstringop-overread`). | A GCC 16 Release build prints no warnings, without hiding warnings in engine code. | ⬜ |

## E1b: engine gaps (ROADMAP 🟡/🔴)

| ID | Area | Gap | Status |
|---|---|---|---|
| ENG-G03 | Rendering | Normal maps and per-pixel roughness/metallic textures (today one value per object). | ⬜ |
| ENG-G05 | Lighting | Cascaded shadow maps (one map follows the camera today, #20). | ⬜ |
| ENG-G04 | Lighting | Point-light shadows (only the directional light casts). | ⬜ |
| ENG-G07 | Lighting | More than 4 lights, and a Lua `light.*` API. | ⬜ |
| ENG-G06 | Lighting | Reflections from a prefiltered environment map (ambient is SH only). | ⬜ |
| ENG-G08 | Models | GPU skinning in `ModelModule` (CPU skinning today). | ⬜ |
| ENG-G09 | UI | RmlUi Vulkan `filter`, `box-shadow`, `backdrop-filter` (silently ignored today). | ⬜ |
| ENG-G17 | Rendering | Tone mapping as one post pass on an HDR scene target, 10-bit output (#35). | ⬜ |
| ENG-G18 | Rendering | Particle system v2: emitters and a Lua API (#22). | ⬜ |
| ENG-G12 | Physics | Liquids: transparency, refraction and thickness; meltable solids from meshes and refreezing. | ⬜ |
| ENG-G10 | Physics | FEMFX <-> Jolt bridge leftovers: rubble breaking again, same-step pushes from heavy FEMFX bodies. | ⬜ |
| ENG-G13 | Network | FEMFX breakables per machine; relay and join codes (#44); world saves (#45); shared roles (#46). | ⬜ |
| ENG-G14 | Mods | Pack system in every game and a mod screen (#61). | ⬜ |
| ENG-G19 | Animation | 2D blend spaces, CCD for long chains, rest-pose matching, an animation demo (#24). | ⬜ |
| ENG-G11 | Physics | FEMFX on ARM/WebAssembly through a SIMDe port. | ⬜ |
| ENG-G15 | Platforms | macOS bundle and `.icns`, Windows `.exe` icon build, phone player builds (#56), code signing (#57). | ⬜ |
| ENG-G16 | Content | Synty packs beyond POLYGON Prototype in the demos; asset browser leftovers. | ⬜ |

ENG-G01 (bugs) is E1a above. ENG-G02 (textures from files) is done: ROADMAP
"Real image/texture loading from files" is 🟢.
