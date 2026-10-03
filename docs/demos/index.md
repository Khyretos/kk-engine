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
| A driving game: races, drifting, a destruction derby, a rally, damage | [Racing](../../games/racing/README.md) | [vehicles](../VEHICLES.md), [particle effects](../PARTICLE_EFFECTS.md), the [start menu](../LOBBY.md) |
| A flying, driving or other vehicle game; flight sticks | [Flying](../../games/flying_demo/README.md) | [input](../INPUT.md), the [start menu](../LOBBY.md) |
| A party game of many short minigames (Fall Guys, Mario Party, Squid Game style) | [Party](../../games/party/README.md) | [Climb Race](../../games/climb_race/README.md), the [start menu](../LOBBY.md), [networking](../NETWORKING.md) |
| A sports game, or anything with a soft ball | [Tennis](../../games/tennis/README.md) | [physics (FEMFX)](../../games/physics_demo/README.md), the [start menu](../LOBBY.md) |
| A fighting game | [Duel](../../games/duel/README.md) | [melee combat](../COMBAT.md) |
| An action game against crowds | [Goblin Horde](../../games/goblin_horde/README.md) | [melee combat](../COMBAT.md), [AI](../AI.md) |
| A strategy or squad game | [Platoon](../../games/platoon/README.md) | [command kit](../../games/command_kit/README.md), [commands and orders](../COMMANDS.md) |
| A pet, a companion, or animals that react to you | [Pet companion](../../games/pet_companion/README.md), [Farm](../../games/farm_demo/README.md) | [AI](../AI.md) |
| Creatures that walk without animation clips | [Procedural animation](../../games/procedural_demo/README.md) | [procedural animation](../PROCEDURAL_ANIMATION.md) |
| A third-person adventure, or anything using every system at once | [kke_demo showcase](../../games/showcase/README.md) | [the showcase guide](../SHOWCASE.md) |
| A building or level-editing game, or a game made while playing | [Sandbox](../../games/sandbox/README.md) | [play to make](../PLAY_TO_MAKE.md) |
| Menus, a HUD, settings, an inventory | [RmlUi demo](../../games/rmlui_demo/README.md) | [input](../INPUT.md) |
| Debug panels and tools for yourself | [ImGui demo](../../games/imgui_demo/README.md) | |
| Breakable, bendable, squashable things | [physics_demo](../../games/physics_demo/README.md) | [physics bridge](../PHYSICS_BRIDGE.md), [ragdolls](../RAGDOLLS.md) |
| Lava, melting and pouring | [melt_demo](../../games/melt_demo/README.md) | |
| Boats, water, floating and sinking | [sea_demo](../../games/sea_demo/README.md) | |
| Soft bodies, jelly, jiggling characters | [jiggle_demo](../../games/jiggle_demo/README.md) | [jiggle physics](../JIGGLE.md) |
| Capes, flags, nets, curtains and blankets | [cloth_demo](../../games/cloth_demo/README.md) | [cloth](../CLOTH.md) |
| Sound that fills a place: rooms, walls, doors, footsteps | [audio_demo](../../games/audio_demo/README.md) | [audio](../AUDIO.md) |
| Store-bought characters: load, skin, pose and ragdoll them | [synty_demo](../../games/synty_demo/README.md) | [scenes](../SCENES.md), [ragdolls](../RAGDOLLS.md) |
| A small tech demo with a settings panel a controller can drive | any of the six above, or procedural_demo | [the demo panel](../DEMO_PANEL.md) |

