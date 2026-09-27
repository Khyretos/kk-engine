# The demos

Every demo in `games/` is a working game or scene built only from the
engine's own blocks. Each one has a README that explains it the same way,
so you can use any of them as the starting point for a game of your own:

| Section | What it tells you |
|---|---|
| Run it | The executable, the build option it needs, the environment switches |
| Controls | Keyboard, mouse and controller, checked against the input code |
| How it plays | The rules, the goal, the menus |
| How it works | System by system: what runs each frame, in which file, and why |
| Design decisions | What was chosen, why, and what was left out |
| Tuning | The numbers that change the feel, and where they live |
| Engine features it uses | Which engine blocks, with links to their guides |
| Assets | Exactly which files and Synty packs it loads, and what happens without them |
| Make a game like this | The steps from a copy of the demo to your own game |
| Files | Every source file and what is in it |

Run any demo from `build/bin/` (building: [BUILDING.md](../BUILDING.md)).
`KKE_SKIP_INTRO=1` skips the logo intro.

## Which demo to start from

| You want to make... | Start from | Then look at |
|---|---|---|
| Your first game, in Lua only | [starter game](../../games/template/README.md) | [first Lua game](../../games/first_lua_game/README.md), the [cookbook](../cookbook/index.md) |
| A race or a party game for the couch, with online play | [Climb Race](../../games/climb_race/README.md) | the [start menu](../LOBBY.md), [networking](../NETWORKING.md) |
| A fighting game | [Duel](../../games/duel/README.md) | [melee combat](../COMBAT.md) |
| An action game against crowds | [Goblin Horde](../../games/goblin_horde/README.md) | [melee combat](../COMBAT.md), [AI](../AI.md) |
| A strategy or squad game | [Platoon](../../games/platoon/README.md) | [command kit](../../games/command_kit/README.md), [commands and orders](../COMMANDS.md) |
| A pet, a companion, or animals that react to you | [Pet companion](../../games/pet_companion/README.md), [Farm](../../games/farm_demo/README.md) | [AI](../AI.md) |
| Creatures that walk without animation clips | [Procedural animation](../../games/procedural_demo/README.md) | [procedural animation](../PROCEDURAL_ANIMATION.md) |
| A third-person adventure, or anything using every system at once | [kke_demo showcase](../../games/showcase/README.md) | [the showcase guide](../SHOWCASE.md) |
| A building or level-editing game, or a game made while playing | [Sandbox](../../games/sandbox/README.md) | [play to make](../PLAY_TO_MAKE.md) |
| Menus, a HUD, settings, an inventory | [RmlUi demo](../../games/rmlui_demo/README.md) | [input](../INPUT.md) |
| Debug panels and tools for yourself | [ImGui demo](../../games/imgui_demo/README.md) | |
| A game in plain C++ from the smallest pieces | [kke_basics](../../games/kke_demo_game/README.md) | [C++ cookbook](../cookbook/cpp.md) |

## Coming next

These demos are being moved to RmlUi screens and full controller support
first; their pages follow once that is done: `audio_demo`, `jiggle_demo`,
`melt_demo`, `physics_demo`, `sea_demo` and `synty_demo`
(the [source](../../games/) is in `games/`).
