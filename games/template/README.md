# Starter game template

The starting point for your own game: a character to walk, run, jump,
vault and climb with, a third-person camera, and a small level with crates
to push, built in Lua.

```bash
tools/new_game my_game     # copies this folder to games/my_game
cmake --build build
cd build/bin && ./my_game
```

- `main.cpp`: the modules the game is made of, and the lights.
- `PlayerModule.cpp`: movement (kke::Locomotion), the camera (kke::CameraRig)
  and the `player` table for Lua.
- `scripts/game.lua`: the level and the rules. Save it while the game runs
  and it reloads.

Walkthrough: [Make your own game](../../docs/tutorials/getting-started.md),
then the [tutorials](../../docs/tutorials/index.md). The template itself
builds as `starter_game`.
