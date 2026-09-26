# Make your own game

From a fresh clone to your own game running, in four steps. You'll end with
a character you can walk, run, jump, vault and climb with, in a small level
whose layout lives in a Lua file you can edit while the game runs.

## 1. Build the engine

Install the system packages and build once, as in
[Install and build](../BUILDING.md). On Debian or Ubuntu:

```bash
git clone https://github.com/Khyretos/kk-engine.git
cd kk-engine
cmake --workflow --preset default
```

The first build fetches every library from source and takes a while; later
builds only compile what changed.

## 2. Try the starter game

The build already made the starter game from `games/template/`:

```bash
cd build/bin
./starter_game
```

Click the view to take the mouse, then:

| Key | Does |
|---|---|
| ++w++ ++a++ ++s++ ++d++ | move (relative to the camera) |
| mouse | look |
| ++space++ | jump; in front of the fence it vaults, in front of the block it climbs |
| ++shift++ | sprint |
| ++alt++ | walk |
| ++v++ | first or third person |
| ++esc++ | let go of the mouse |
| ++f1++ | developer panels: the Scripts panel with its console, and frame stats |

A controller works too, with the same actions on its buttons and sticks.

## 3. Make your copy

From the repository root:

```bash
tools/new_game my_game            # or: cmake -DNAME=my_game -P tools/new_game.cmake
cmake --build build
cd build/bin && ./my_game
```

`tools/new_game` copies `games/template/` to `games/my_game/`, names the
executable, the window and the manifest after it, and adds it to the build
(through `games/my_games.cmake`). Use lower-case letters, digits and `_`.

Your game folder:

| File | What it is |
|---|---|
| `main.cpp` | The list of modules your game is made of, and the lights |
| `PlayerModule.cpp` | The player: movement, camera, and the `player` table for Lua |
| `scripts/game.lua` | The level and the rules |
| `game.json` | The manifest: name, description, tags (shown by the marketplace) |
| `CMakeLists.txt` | How it's built; `GAME_NAME` is the executable's name |

## 4. Change something while it runs

Leave the game running, open `games/my_game/scripts/game.lua` and change
the colour of the stairs:

```lua
local accent = Vec(0.2, 0.7, 0.9)
```

Save. The game reloads the script within half a second: the level is
rebuilt with blue stairs, and nothing is built twice. If you make a
mistake, the error (file and line) shows in the log and in the Scripts panel
(++f1++), and the game
keeps running; fix it and save again.

The game reads its scripts straight from your source folder, so there is
nothing to rebuild for Lua changes. C++ changes (`main.cpp`,
`PlayerModule.cpp`) need `cmake --build build` and a restart.

## Next

Carry on with [the tutorials](index.md): walk around and shape the level,
throw things, script a pickup, and add sound.
