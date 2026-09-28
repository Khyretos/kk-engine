# Documentation

Design notes for Kreative Kompas Engine, one file per system. The same
pages, searchable, are at <https://khyretos.github.io/kk-engine/>. Start with the
[project README](../README.md); the live status of every system is in
[ROADMAP.md](../ROADMAP.md) and known defects are in [BUGS.md](../BUGS.md).

| Document | Contents |
|---|---|
| [BUILDING.md](BUILDING.md) | System packages, build presets, running each demo, troubleshooting |
| [DEPENDENCIES.md](DEPENDENCIES.md) | Every third-party library, tool, font and asset: version, licence, and what the licence asks of games shipped with KKE |
| [RELEASES.md](RELEASES.md) | Downloadable Windows and Linux builds: how a tag becomes a release |
| [tutorials/](tutorials/index.md) | Make your own game from the starter template, then walk, break, pick up and add sound |
| [cookbook/](cookbook/index.md) | Recipes from your first line of Lua to cameras, A*, flocking, IK and multiplayer, each run and pictured by CI |
| [demos/](demos/index.md) | Every demo explained in depth (how it works, why, how to make a game like it), and which one to start from |
| [AI_ASSISTANTS.md](AI_ASSISTANTS.md) | Making games with any AI assistant: AGENTS.md, the skills, llms.txt, tools/check_game |
| [HISTORY.md](HISTORY.md) | The development log: the original README, kept whole |
| [AUDIO.md](AUDIO.md) | The audio engine: mixer, 3D sound, occlusion, impact synthesis, accessibility |
| [INPUT.md](INPUT.md) | Rebindable actions, devices, triggers and chords |
| [DEMO_PANEL.md](DEMO_PANEL.md) | A settings panel for a demo or tool that works with a controller, the keyboard and the mouse |
| [LOBBY.md](LOBBY.md) | The start menu: controllers press A to join, players pick a look, player 1 sets the CPU players |
| [COMMANDS.md](COMMANDS.md) | Orders for companions and squads: selection, formations, radial wheel, Lua and nodes |
| [MOVEMENT.md](MOVEMENT.md) | How characters move, and the animation principles behind it |
| [AI.md](AI.md) | The AI core: senses, needs, utility decisions, steering, navmesh, orders, `ai.*` in Lua |
| [PROCEDURAL_ANIMATION.md](PROCEDURAL_ANIMATION.md) | Animation without clips: gaits for any number of legs, look-at, FABRIK IK, active ragdolls that stagger and get up |
| [EQUIPMENT.md](EQUIPMENT.md) | Holding and wearing things: sockets and slots, grips in the palm with the fingers closed round them, and arms that never go through the body |
| [COMBAT.md](COMBAT.md) | Melee combat and crowds: attacks, blocks, parries, poise, hordes that wait their turn |
| [JIGGLE.md](JIGGLE.md) | Jiggle physics for bones, skin and soft bodies |
| [PHYSICS_BRIDGE.md](PHYSICS_BRIDGE.md) | How FEMFX deformable pieces and Jolt rigid bodies and characters meet |
| [CLOTH.md](CLOTH.md) | Cloth: capes, flags, blankets, nets; fabric presets, no-clipping protection levels, what it costs |
| [HAIR.md](HAIR.md) | Hair: guide strands on Jolt soft bodies, styles, drawn on the GPU, no clipping into the head, what it costs |
| [VEHICLES.md](VEHICLES.md) | Cars and other wheeled vehicles on Jolt: suspension, tyres, engine, gearbox, drifting, damage |
| [PARTICLE_EFFECTS.md](PARTICLE_EFFECTS.md) | Smoke and sparks: tyre smoke, engine fires, sparks off metal |
| [RAGDOLLS.md](RAGDOLLS.md) | Humanoid and animal ragdolls, their joint limits and how to override them |
| [NETWORKING.md](NETWORKING.md) | Transport, protocol, authority and replication |
| [STORAGE.md](STORAGE.md) | Saving data: SQLite built in, Valkey and PostgreSQL optional |
| [SERVER_HOSTING.md](SERVER_HOSTING.md) | Running a server: kke_server, Docker, roles, directories, security |
| [ANTI_CHEAT.md](ANTI_CHEAT.md) | Fair games without invasive software: shipping builds, server authority, fog of war, game rules, input checks, sealed data |
| [DRM.md](DRM.md) | Kreative DRM: optional, offline-after-activation licences the developer controls |
| [MODDING.md](MODDING.md) | DLC and mods: pack.json, load order, Nexus Mods / Steam Workshop / mod.io, playing together with one copy |
| [DATA_FILES.md](DATA_FILES.md) | Every data file can be JSON or YAML; which one wins when both exist |
| [SCRIPTING.md](SCRIPTING.md) | Lua gameplay scripts |
| [PLAY_TO_MAKE.md](PLAY_TO_MAKE.md) | Make the game while playing it: Simple, node graph and Lua levels |
| [SCENES.md](SCENES.md) | Levels built from asset packs |
| [SHOWCASE.md](SHOWCASE.md) | kke_demo: the stations, controls, HUD and demo scripts |
| [OPTIMIZATION.md](OPTIMIZATION.md) | Performance rules, measured log and backlog |
| [MOODS.md](MOODS.md) | Moods: sky (gradient or CC0 HDR picture), sun, fog, colour look and ambience from one YAML file; which ones ship |
| [RENDERING_PRINCIPLES.md](RENDERING_PRINCIPLES.md) | Rendering doctrine: no dithering, a clean image every frame, what we took from Threat Interactive |
| [PERFORMANCE_NOTES.md](PERFORMANCE_NOTES.md) | What RayFire and Chaos do for destruction at scale, and what KKE took from it |
| [PLATFORMS.md](PLATFORMS.md) | What runs where, hardware targets (Steam Deck, phones...), build presets, consoles |
| [SCALING.md](SCALING.md) | Worst cases, multiplayer limits and platform support |
| [BENCHMARKS.md](BENCHMARKS.md) | The stress test, kke_bench, hardware profiles, tracked results |
| [HARDWARE_TESTS.md](HARDWARE_TESTS.md) | Checks only real hardware can answer |
| [PLAYTEST_CHECKLIST.md](PLAYTEST_CHECKLIST.md) | Look-and-feel pass through every demo, with a box to tick per check |
| [GO_TO_MARKET.md](GO_TO_MARKET.md) | How KKE reaches people, and how it is funded |
| [DOCS_SITE.md](DOCS_SITE.md) | The docs website: how it is built and published |
