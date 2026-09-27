---
name: make-a-game
description: Take someone from nothing to a playable KKE game, then grow it into a whole game (goal, rules, HUD, sound, saving) one checked step at a time.
---

# Make a game with KKE

A game is a folder in `games/`, copied from `games/template/`. The copy
already has a character that walks, runs, jumps, vaults and climbs, a
camera, and a small level. Everything else is Lua in its `scripts/`
folder, which reloads while the game runs.

## Step 1: build once (skip if `build/bin/starter_game` exists)

```bash
git clone https://github.com/Khyretos/kk-engine.git && cd kk-engine
cmake --workflow --preset default        # first time: fetches libraries, takes a while
```

System packages per platform: `docs/BUILDING.md`.

## Step 2: make the game's folder

```bash
tools/new_game my_game                   # lower-case letters, digits, _
cmake --build build --target my_game
cd build/bin && ./my_game
```

Now `games/my_game/scripts/game.lua` is the level. Edit it and save while
the game runs; it rebuilds itself.

## Step 3: agree on the game in one sentence

Before writing code, write down with the person: **what the player does,
how they win, how they lose.** "Collect 10 coins before the timer runs
out; falling off the edge restarts." Everything after this is small steps
toward that sentence.

## Step 4: grow it in small steps, each one checked

Do **one** step per change, and check it (Step 5) before the next. A good
order for most games:

| Step | Put it in | Start from (docs/cookbook/recipes/) |
|---|---|---|
| 1. The level: ground, walls, platforms | `scripts/game.lua` | the template's `game.lua`, `pyramid.lua` |
| 2. The thing to do: pickups, targets, enemies | `scripts/<thing>.lua` | `zone_door.lua`, `launch_pad.lua`, `patrol.lua`, `pet.lua` |
| 3. Controls it needs beyond walking | the same file | `throw.lua`, `toggle.lua`, `shooter.lua` |
| 4. Win and lose, timer, score on screen | `scripts/round.lua` | `round.lua` |
| 5. Sound | where things happen | `sounds.lua` |
| 6. Remember the best score | `round.lua` | `round.lua` (`store`) |
| 7. Polish: generated levels, smarter enemies | new files | `maze.lua`, `terrain.lua`, `astar.lua`, `flocking.lua` |

The lua-scripting skill has the whole API and the rules; read it before
writing Lua. Each file should do one thing, with hook and timer names
prefixed by the file's name.

Talk to other scripts with your own events:
`hook.Run("CoinCollected", 1)` in one file,
`hook.Add("CoinCollected", "round.coins", function(n) ... end)` in another.

## Step 5: check every step

```bash
tools/check_game my_game                  # OK or FAILED, with the script's file and line
tools/check_game my_game --shot look.jpg  # and a screenshot to look at
```

Can't run commands? Ask the person to save the file with the game
running and tell you what the log (or F1, Scripts) says, and what they
see. Their answer is your test result: read it before the next step.

## Step 6: name it and share it

- `games/my_game/game.json`: title, description, tags.
- Art: `models.load("SM_...")` uses installed asset packs; plain boxes
  and spheres with colours are a fine look to start with.
- A downloadable build: `docs/RELEASES.md` shows how the engine's own
  builds are packaged; a game's is made the same way.

## When it needs C++

Walking, cameras and physics are already there. New *kinds* of systems
(a vehicle, a card-game table, a custom camera) are C++ modules in the
game's folder: see the cpp-module skill. Try Lua first; ask the person
before adding C++, since it needs a rebuild each time.
