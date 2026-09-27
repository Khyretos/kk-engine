---
name: explain-kke
description: Explain KKE or teach game making to someone - beginners, children, or programmers - choosing the right level (pictures, node graph, Lua, C++) and answering from the docs, not from guesses.
---

# Explaining KKE and teaching game making

KKE's founding idea: **a five-year-old can make a game**, by playing in
it. There are four levels, all working the same building blocks. Pick
the one that fits the person, not the most powerful one.

| Level | For | Where |
|---|---|---|
| Pictures: drag things into the world, use tools on them | young children, anyone who just wants to play and make | the `sandbox` game, Play mode |
| Node graph: boxes and wires ("when hit" → "knock over") | people who think in steps but don't type code | the sandbox, **Look** tool |
| Lua scripts | anyone ready to type a few lines | `games/<game>/scripts/` |
| C++ modules | programmers building new systems | `games/<game>/*.cpp`, `engine/` |

Whatever a level makes, the next level shows how it's done: a node graph
shows its Lua with **Show Lua**. Moving up a level is a choice, never a
requirement. `docs/PLAY_TO_MAKE.md` is the design; `docs/cookbook/play-to-make.md`
shows one idea at every level.

## How to explain

1. **Answer from the docs.** Check `docs/cookbook/` (how to do things),
   `docs/SCRIPTING.md` (what Lua can do) and `docs/README.md` (every
   topic) before explaining. If the docs don't cover it, say so, and say
   what you inferred.
2. **Show, then explain.** A short script they can run and see, then one
   sentence per line that matters. Link the cookbook page that goes
   further.
3. **One idea at a time**, each with something that visibly changes:
   "Change `0.8` to `0.2` and save: the crate turns green."
4. **Use their words**: "a thing that follows you", not "a steering
   behaviour", until they ask for the name.
5. **Real names for real ideas, once they're ready**: A*, noise, boids,
   IK. The algorithms page (`docs/cookbook/algorithms.md`) explains each
   with pictures.

## For a child

- Short sentences. One step. Then "What do you see?"
- Start with pictures (sandbox). Move to Lua only when they want to
  change *how* something works, and start with changing numbers and
  colours in `game.lua`.
- Never say something is too hard. Say "Let's try a small piece of it."
- Every mistake can be undone: saving a fixed script brings everything
  back.

## For a programmer

- Architecture: a game is a `kke::Application` with a list of
  `kke::Module`s (`main.cpp`); Lua is the gameplay layer on top, sandboxed
  and hot-reloaded. `docs/cookbook/cpp.md`, then `AI_GUIDE.md` for engine
  internals.
- Answer "why is it built this way" from the header comments in
  `engine/include/kke/*.h`, which each explain their design.

## Words people ask about

| Word | Plain meaning |
|---|---|
| hook | "when this happens, run my function" (every frame, on a touch, at start) |
| timer | "run this later, or every few seconds" |
| body | a thing physics moves: it falls, bounces, gets pushed |
| static | a body that never moves: floors, walls |
| raycast | "draw an invisible line; what does it hit first?" |
| action | a control the player can rebind: "jump" is an action, Space is its key |
| module | a C++ part of the game: physics, sound, the player |
| hot reload | save the file and the running game uses it at once |
| `sv_` script | runs only on the machine that decides (the host) in multiplayer |
