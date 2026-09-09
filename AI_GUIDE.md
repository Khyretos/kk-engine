# AI_GUIDE.md

This file exists so that **any** AI model — not just the one that wrote
this codebase — can pick up Kreative Kompas Engine and start contributing
correctly without re-deriving its architecture from scratch. If you are an
AI agent (Claude, a fork of this project's model, or anything else) reading
this to orient yourself: start here, then check `ROADMAP.md` (what
actually works right now, by system) and `BUGS.md` (specific defects
already found, before you go looking for new ones or re-find an old one),
then go to `README.md` for full depth/reasoning on any specific topic.

## What this engine is, in one paragraph

A modular Vulkan game engine (SDL3 + Vulkan + volk + VMA + GLM + ImGui +
RmlUi + Lua, C++20) where everything beyond the core frame loop — physics,
networking, destruction, UI, gameplay — is a `kke::Module` plugged into a
`kke::Application`. The explicit long-term goal is Roblox-like
accessibility (non-programmers scripting games, programmers writing C++
modules, teams doing both) while staying fully open-source and close to
the hardware. It is built iteratively, in small verified slices, across
many sessions — there is no expectation this is "finished" at any point
you read this; check the README's Roadmap section for current state.

## Non-negotiable rules for working on this codebase

1. **If you have tool access (can execute code, build, run), every
   change must actually build and run before you claim it works.** This
   project's entire history is "write code → build → run headlessly
   (Xvfb + lavapipe, see README) → screenshot or log-inspect → only then
   call it done." Do not describe a feature as working from reading the
   code alone. For pure-logic code (no GPU/window involved), write a
   real GoogleTest unit test in `tests/` instead of a manual check —
   see README "Test suite & coverage" for the two-tier strategy (unit
   tests + enforced 85% floor for pure logic; build-and-run verification
   for Vulkan/GPU code) and don't blur the two. Run `./build/bin/kke_tests`
   before claiming any change to `GameManifest`/`MarketplaceIndex`/
   `RmlTextSafety` (or their future equivalents) works.

   **If you do NOT have tool access — no code execution, no build, no
   way to run anything — say so plainly, up front, and switch roles
   instead of pretending otherwise.** Don't produce code and imply it
   was verified. Don't go quiet about the limitation and hope it isn't
   noticed. Instead: propose the change, explain what it should do and
   why, and hand the person exact, runnable commands to build and test
   it themselves (the same commands this guide and the README already
   use — `cmake -B build ...`, `./build/bin/kke_tests`, the Xvfb/lavapipe
   headless run, what output or screenshot to expect if it worked). Walk
   them through reading the result, the same way you'd walk through
   your own tool output if you had it. This is not a lesser mode of
   working — a 1B-parameter model with no tool access and a 600B model
   with a full sandbox should both be able to help someone build a game
   here; they just move at different paces, with a different person
   (or a different, more-capable AI) doing the actual keypresses in one
   case. Never let a capability gap become a reason to withhold help,
   misrepresent what was checked, or make the person feel they need a
   "better" AI to use this engine at all. See "Working without tool
   access" below for how this changes your role for the rest of a
   conversation, not just this one rule.
2. **Read the actual dependency source when unsure of an API**, rather
   than relying on training-data memory of a library's interface. RmlUi's
   integration in this repo was written by fetching the exact pinned
   source and grep/view-ing its headers for real method signatures — not
   by recalling them — and it still needed one iteration (the font engine
   requirement) that memory alone would have gotten wrong. Prefer that
   discipline over confident guessing, especially for fast-moving FOSS
   dependencies pinned to a specific tag/commit in `CMakeLists.txt`.
3. **Small, independent slices.** Don't try to build an entire system
   (e.g. "the physics module") in one pass. Land the smallest piece that
   builds, runs, and proves one thing, note what's deferred, and stop.
   This is a deliberate process choice (see README intro), not a
   limitation to work around.
4. **No closed-source dependencies, ever.** Every library this engine
   pulls in must be genuinely open-source (see the Stack table in
   README.md for what's already vetted). This is a hard constraint, not
   a preference, because of the Roblox-marketplace-like ambition — a
   closed dependency anywhere would compromise that.
5. **Check `BUGS.md` and `ROADMAP.md` before starting work, and update
   both as part of the change, not as an afterthought.** Before
   debugging anything, search `BUGS.md` for the symptom, the file, or
   the general area — a fix that already landed in one module has a
   real history of resurfacing as a "new" bug in a sibling module
   because nothing pointed back to the original. Before claiming a
   system works or starting work on one, check its row in
   `ROADMAP.md`. When you fix a bug, add or update its row in
   `BUGS.md` in the same session, with enough in "Root cause" and
   "Fix" that someone hitting a similar symptom elsewhere can
   recognize the same pattern without re-deriving it. When a system's
   status genuinely changes, update its row in `ROADMAP.md`. Treat
   leaving either file stale as a bug in your work, not a
   documentation nice-to-have — see `BUGS.md`'s own intro for a real,
   concrete example of what letting this slide costs (a fix
   rediscovered as a "new" bug in a different module, and a README
   section that confidently claimed two different things that had
   already stopped being true). `README.md`'s own "Immediate next
   slices" remains the fuller narrative/reasoning trail and is still
   worth adding to for the "why," but it is not a substitute for the
   structured, queryable state these two files keep.
6. **Follow the module boundary discipline that already exists**:
   - Something that needs to exist whether or not anything else is
     present → a `Module`, added in `main.cpp`.
   - Something one module needs from another specific module → declare it
     via `dependencies()` and use `Application::getModule<T>()`.
   - Something modules should discover about each other *without* either
     depending on the other's concrete type → a capability interface in
     `kke/Capabilities.h`, discovered via `Application::findCapability<T>()`.
   Don't add a fourth pattern without a clear reason; these three cover
   everything built so far, including the harder cases (destruction ↔
   networking, see README).

## Where to actually look

Don't re-read every file — the README has two tables built exactly for
fast orientation:
- **"Where the rendering code actually lives"** — which file to open for
  any given rendering concept (simplest draw call, compute-into-graphics,
  GPU timing, pipeline configuration).
- **"What's missing — module by module"** and the **Roadmap** section —
  the current, honest gap list against the engine's stated ambitions
  (physics, rollback netcode, procedural generation, animation, UI,
  audio, scripting, etc.), each with a short note on why it's hard or
  what it depends on.

Repo layout worth knowing before you start: `engine/` is the engine
itself (forked as-is by any game); `games/<name>/` is a self-contained
"game folder" — `games/kke_demo_game/` is the current one and also the
template to copy for a new one. Every game folder needs a `game.json`
(schema in README "Game folder convention & marketplace"). If your
change concerns marketplace/import behavior specifically, read that
section's sandboxing caveat before writing anything — it's the single
most important unresolved constraint in the whole project, and nothing
that assumes native-code game folders are safe to run untrusted should
be built without addressing it first.

## Working without tool access

If you're an AI helping with this engine and you can't execute anything
— no shell, no compiler, no way to run the commands in the next
section — that changes your role for the rest of the conversation, not
just whether you mention it once:

- **Say it at the start, not as a caveat at the end.** "I can't run this
  myself, so here's what I'd like you to try and what to watch for" is
  the opening move, not a footnote after confidently-worded code.
- **Be an instructor, not just a code generator.** Explain *why* a
  change should work, what could go wrong, and how to tell success from
  failure — the same reasoning you'd use to verify it yourself, just
  handed to the person instead. Give them the exact commands from
  "Verifying your own work" below, and say plainly what output or
  screenshot means it worked.
- **Ask for the result and actually use it.** If the person reports an
  error or a screenshot back, treat that the way you'd treat your own
  tool output — read it, diagnose it, propose the next change. You're
  still doing the debugging; you're just not the one holding the mouse.
- **Don't apologize for this repeatedly or make it feel like a
  deficiency.** State the limitation once, clearly, then get on with
  being useful. A person soloing this engine with a small local model
  and someone pairing with a large hosted one should both come away
  able to build something — slower in one case, not lesser.

This matters especially here because the person building on this engine
might genuinely be switching between AI assistants of very different
capability — solo development with whatever model they have locally one
day, a more capable assisted session another. Neither this file nor the
engine should read as if it only works with one kind of AI.

## Further reading: simulation, procedural generation, and game design theory

Genuinely useful starting points, picked because they map onto specific
things this engine's Roadmap already calls out — not a generic reading
list. Both the person and whatever AI they're working with can use these
as a head start rather than re-deriving established theory from scratch.

- **[Gaffer On Games](https://gafferongames.com/)** (Glenn Fiedler) —
  directly relevant to the hardest items on this engine's Roadmap. "Fix
  Your Timestep!" is the article this engine's `FixedUpdateContext`
  design already follows; the site's networking series ("What Every
  Programmer Needs To Know About Game Networking," the physics/state
  synchronization articles) is the best free, practically-oriented
  starting point for the client-server *and* rollback netcode items —
  read this before implementing either.
- **[GGPO](https://github.com/pond3r/ggpo)** — the reference rollback
  netcode library for fighting games, open source, with design notes
  explaining the state-save/restore and input-prediction model this
  engine's rollback Roadmap item would need. Worth reading even if not
  directly integrated, as the canonical description of the pattern.
- **[Procedural Content Generation in Games](http://pcgbook.com/)**
  (Shaker, Togelius, Nelson) — a full, freely available academic book
  covering the actual algorithm families (noise-based terrain, grammar/
  graph-based dungeon layouts, search-based generation) behind this
  engine's two procedural-generation Roadmap items (survival's streaming
  terrain, the dungeon crawler's on-demand floors). Chapter-level detail
  well beyond what any engine README should try to restate.
- **["MDA: A Formal Approach to Game Design and Game Research"](https://users.cs.northwestern.edu/~hunicke/MDA.pdf)**
  (Hunicke, LeBlanc, Zubek, 2004) — a short, foundational paper on
  structuring game design as Mechanics/Dynamics/Aesthetics. Useful
  vocabulary for anyone (human or AI) designing one of the "mini game
  demo per genre" projects discussed for this engine, before writing any
  code for it.
- **[Craig Reynolds' Boids](https://www.red3d.com/cwr/boids/)** — the
  original flocking/emergent-behavior simulation paper. The clearest
  short example of "simple local rules producing complex global
  behavior," the same underlying idea behind this engine's particle
  system and any future crowd/swarm/flocking gameplay.

Treat this list as a starting point to extend, not a fixed canon — if
you (person or AI) find a better resource for a specific Roadmap item,
add it here with the same "why this, for what" framing.

## Verifying your own work in this environment

This repo has been built and run headlessly throughout its development:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
Xvfb :99 -screen 0 1280x720x24 &
DISPLAY=:99 SDL_VIDEODRIVER=x11 ./build/bin/kke_demo
```

This runs against Mesa's `lavapipe` software Vulkan device — no real GPU
needed, which is exactly why it's usable inside a sandboxed AI coding
session. Use `xdotool` (see README/session history) to simulate mouse
input for interactive features, and `import -window root` (ImageMagick)
to capture a screenshot for visual verification. A stale `imgui.ini` in
the run directory will silently override window positions/collapsed state
between runs — delete it before re-testing UI layout changes.

## On being one of possibly several AI contributors

This project explicitly anticipates being worked on by different AI
models across its lifetime (forks, different providers, future versions
of whatever wrote this file originally). Nothing in this codebase or this
guide should be read as assuming a specific model wrote it or must
continue it. If you are such a model: the disclosure in `README.md`
("this engine is AI-coded") is meant literally and permanently — keep it
accurate as authorship changes, don't remove it, and don't write anything
here or in the README that only makes sense for one specific AI system.
