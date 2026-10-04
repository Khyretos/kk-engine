# Make games with an AI assistant

KKE is written to be explained by AI as well as by people. Any assistant
can help you make a game here, whether it is a large hosted one or a
small model on your own computer, with or without permission to run
commands. It explains, writes the scripts, tests them when it can, and
tells you what to try when it can't. The same files teach the assistant
and you.

## What the assistant reads

| File | What it is |
|---|---|
| [`AGENTS.md`](../AGENTS.md) | The entry point: which skill fits which job, and the rules that always apply |
| [`skills/make-a-game`](../skills/make-a-game/SKILL.md) | From nothing to a whole game, one checked step at a time |
| [`skills/lua-scripting`](../skills/lua-scripting/SKILL.md) | The whole Lua API on one page, the rules, and which recipe to start from |
| [`skills/check-and-debug`](../skills/check-and-debug/SKILL.md) | Testing a change and reading errors, with or without running anything |
| [`skills/explain-kke`](../skills/explain-kke/SKILL.md) | Explaining and teaching, from children to programmers |
| [`skills/cpp-module`](../skills/cpp-module/SKILL.md) | C++ modules, Lua bindings and tests |
| [`llms.txt`](https://khyretos.github.io/kk-engine/llms.txt) | The index for assistants that read websites ([llmstxt.org](https://llmstxt.org)) |
| [`llms-full.txt`](https://khyretos.github.io/kk-engine/llms-full.txt) | All of the above, plus the tutorials, cookbook and Lua API, in one file |

Each skill works on its own and is short enough for a small model's
context. Every Lua snippet in them has been run in the starter game, and
the C++ one compiles with warnings as errors.

## With your assistant

**A coding assistant working in the repository** (Claude Code, Codex,
Cursor, Copilot, Gemini CLI, Aider and others) reads `AGENTS.md` by
itself. Claude Code reads it through `CLAUDE.md` and picks up the
skills from `.claude/skills/`, which points to `skills/`. Just ask:

- "Make me a game where you collect coins before the time runs out."
- "Add an enemy that chases me around the walls."
- "Why does my crate fall through the floor?"
- "Explain how the maze generator works."

**A chat assistant without the repository**: give it
`https://khyretos.github.io/kk-engine/llms-full.txt` if it can read
links, or download the file and attach it.

**A small or local model**: paste `AGENTS.md` and the one skill that
fits (usually `lua-scripting`) at the start of the conversation. That
is enough to write working scripts.

## When the assistant can run commands

`tools/check_game` lets an assistant (or you) test a game without
looking at it. It runs the game for a few seconds on a virtual screen
and prints what the scripts printed, every warning and error with its
file and line, and then OK or FAILED:

```bash
tools/check_game my_game                  # the game's own scripts
tools/check_game my_game --shot look.jpg  # and a screenshot
tools/check_game my_game --keys "e space" # press keys while it runs
```

It works on a server with no screen and no GPU. An assistant is told
never to call a change done until this passes. `--headless` uses a
virtual screen even on a desktop, and `--pad` plugs in a virtual
gamepad that moves its sticks.

### Testing the whole engine on your own PC

`tools/runner/kkrun` is for a local model (or you) that tests engine
changes, not just one game. Each job is one command that ends with a
line starting PASS or FAIL (exit code 0 means PASS), and writes its logs
and screenshots to `build/runner-results/<time>/`, with `latest`
pointing at the newest:

```bash
tools/runner/kkrun build                  # configure + build, 6 jobs at once, zero warnings
tools/runner/kkrun tests                  # kke_tests
tools/runner/kkrun demo climb_race        # one demo for 8 s with a virtual pad (and a screenshot)
tools/runner/kkrun shots racing party     # a screenshot of each
tools/runner/kkrun online                 # a host and a joiner in two windows (--game racing)
tools/runner/kkrun summary                # every result, the games' warnings, PASS or FAIL
tools/runner/kkrun all --headless         # all of the above in order
```

`build` and `all` start a new results folder; the other jobs add to the
newest one, so running them one by one collects everything in one
place. The build is RelWithDebInfo (like the `everything-release`
preset) with warnings as errors; `--type` and `--jobs` change that.

With a desktop session (`DISPLAY` set) games open a real window on the
GPU; with `--headless`, or no desktop, they run inside Xvfb on lavapipe
(software Vulkan) when it is installed, otherwise on the GPU. Every run
prints which mode and which Vulkan device it used. `online` always runs
headless, in two Xvfb screens. Keys are pressed with xdotool, or with
python-xlib when xdotool isn't installed. On Arch:
`sudo pacman -S --needed xorg-server-xvfb vulkan-swrast xdotool imagemagick`.

## When it can't

An assistant with no way to run anything says so at the start. From
then on it guides you: which file to save, what you should see, and
where errors appear (the terminal, or **F1** and then Scripts in the
game). Paste back what happened, and it carries on from there. That's
slower, but it works just as well.

## Keeping it right

The skills name only functions that exist. When the engine gains or
changes a Lua function, update `skills/lua-scripting/SKILL.md` along with
[Scripting in Lua](SCRIPTING.md). `llms.txt` and `llms-full.txt` are
generated from these files each time the docs site builds
(`tools/docs_site/llms.py`), so they never go stale.
