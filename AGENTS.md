# AGENTS.md

Instructions for AI assistants (any model, any tool) helping someone with
Kreative Kompas Engine (KKE). People use KKE to **make games**; most of
the time that's what you're helping with, and it's mostly Lua.

## 1. Find the job, read its skill

Each skill is one short file with the steps, the rules and copyable code.
Read the one that fits before you answer; you rarely need more than one.

| The person wants to... | Read |
|---|---|
| start a new game, or grow one into a whole game | [skills/make-a-game](skills/make-a-game/SKILL.md) |
| write or fix a script: levels, controls, enemies, rules, UI, saving | [skills/lua-scripting](skills/lua-scripting/SKILL.md) |
| understand something, or learn ("how does...", "what is...") | [skills/explain-kke](skills/explain-kke/SKILL.md) |
| test a change, or find out why something doesn't work | [skills/check-and-debug](skills/check-and-debug/SKILL.md) |
| write C++: a new game module, new Lua functions, engine work | [skills/cpp-module](skills/cpp-module/SKILL.md) |

Small context window? Load only the skill you need: each works alone.
Large one? https://khyretos.github.io/kk-engine/llms-full.txt is the
whole making-games documentation in one file (`tools/docs_site/llms.py
OUT_DIR` writes it locally).

## 2. Rules that always apply

1. **Say what you checked.** If you ran it, say how (`tools/check_game`
   printed OK). If you can't run anything, say so once at the start and
   give the person the exact command to run and what they should see.
   Never present untested code as working.
2. **Lua first.** A game's level, rules, controls and UI are Lua in
   `games/<game>/scripts/`. They reload while the game runs. Reach for
   C++ only for what Lua can't do (a new kind of system, heavy maths per
   frame on thousands of things).
3. **Copy from what works.** `docs/cookbook/recipes/` has tested scripts
   for most things people ask for (the table in the lua-scripting skill).
   Start from the closest one instead of from nothing.
4. **Only use functions that exist.** The API is in the lua-scripting
   skill and in `docs/SCRIPTING.md`. Don't invent functions; if something
   is missing, say so and suggest the closest real one.
5. **Zero warnings.** A warning in the build or in the game's log is a
   bug to fix properly, never to silence.
6. **Match the person.** A child or a beginner gets one small step at a
   time, in plain words, with something they can see change. Someone
   experienced gets the code and the why.

## 3. Where things are

| Path | What |
|---|---|
| `games/template/` | The starter game every new game copies (`tools/new_game NAME`) |
| `games/<name>/scripts/*.lua` | A game's Lua: level, rules, everything |
| `docs/cookbook/` | Tested recipes from "hello" to A*, boids and IK, with screenshots |
| `docs/tutorials/` | Step-by-step: make a game, then grow it |
| `games/<demo>/README.md` | Each demo explained: how it works, why, how to make a game like it ([docs/demos](docs/demos/index.md) says which to start from) |
| `docs/SCRIPTING.md` | The Lua API in full |
| `docs/ASSETS.md` | Asset folders, naming, which packs each game needs (`kke_assets`) |
| `docs/TOOLS.md` | How to use the kke_ command-line tools |
| `tools/check_game` | Runs a game headless and reports script errors |
| `engine/` | The engine (C++20, Vulkan). See [AI_GUIDE.md](AI_GUIDE.md) before changing it |

Changing the engine itself (not a game)? [AI_GUIDE.md](AI_GUIDE.md) has
the architecture and the contributor rules; read it too.
