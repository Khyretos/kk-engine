# Cookbook

Recipes for making games with KKE, from your first line of Lua to
pathfinding, procedural animation and inverse kinematics. Each one is a
small, complete piece of working code with a picture of it running.

![Some of the recipes: a maze, hills from noise, A* and flocking](media/maze.jpg)

## How to use a recipe

Most recipes are **one Lua file**. Make a game from the starter template
([Make your own game](../tutorials/getting-started.md)), start it, and save
the recipe into its `scripts/` folder: it runs the moment you save, and
again every time you change it. Every recipe has a download link, and
works in the starter game's level as it is.

Some recipes are **C++**, in the cookbook game (`games/cookbook`, run it
as `./cookbook` from `build/bin`). It is the starter game plus every common
camera and an animated mannequin; the pages quote its code.

The chapters go from simplest to most involved:

| Chapter | You'll learn |
|---|---|
| [First steps](first-steps.md) | Variables, `if`, loops, functions and tables, each spawning something you can see; timers |
| [Input](input.md) | Your own controls, hold and release, reading the standard actions, binding keys and pads in C++ |
| [Moving things](moving.md) | Tuning the character, a ball you steer, a pet that follows you, patrols, launch pads |
| [Cameras](cameras.md) | First person, third person, orbit, top-down, isometric, side-on, fixed and cinematic cameras; screen shake |
| [Gameplay](gameplay.md) | Trigger zones and doors, shooting with rays, explosions, a full round with a HUD, a timer and a saved best |
| [Algorithms](algorithms.md) | Maze generation, terrain from noise, A* pathfinding, flocking |
| [Animation and IK](animation.md) | Procedural motion, springs, two-bone IK, blend spaces, look-at and foot placement on a skeleton |
| [Physics](physics.md) | Bounce and friction, things that really break, physics from C++ |
| [Sound](audio.md) | Sound materials, impact sounds, playing your own |
| [Multiplayer](networking.md) | Host and player scripts, messages, a shared scoreboard |
| [Play to make](play-to-make.md) | The same game logic three ways: pictures, a node graph, and Lua |
| [C++](cpp.md) | Your own module, your own Lua bindings, testing your code |

New to Lua? It's a small language; [Programming in Lua](https://www.lua.org/pil/contents.html)
is the classic introduction, and the [Lua API reference](../reference/lua-api.md)
lists everything the engine adds. [Scripting in Lua](../SCRIPTING.md)
explains hooks, timers, hot reload and the sandbox.

## Why the recipes don't go stale

Documentation is only useful when it's right. Every recipe here is checked
by CI on every change to the engine:

- The Lua recipes are real files (`docs/cookbook/recipes/`), shown on
  these pages as they are. CI starts the starter game headless with each
  one (`tools/docs_site/run_recipes.py`) and fails on any script error;
  the unit tests also compile every one of them.
- The C++ on these pages is quoted from files that are compiled into the
  cookbook game and the unit tests (`tests/test_cookbook.cpp`), which run
  it and check it does what the page says.
- The play-to-make Lua runs in the unit tests against the same building
  blocks the sandbox uses.

The screenshots come from the same runs:
`tools/docs_site/run_recipes.py --shots` takes a picture of each recipe
running and saves it here.
