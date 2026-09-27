# Farm demo: you are the dog

A farm where every animal lives its own life on the AI core
([docs/AI.md](../../docs/AI.md)). You play a German Shepherd:

- **Sheep** graze in their meadow. Run at them and they bolt as a flock,
  regroup, and calm down once you are gone.
- **Cows** in the pasture are curious and come over to look at you.
- **Pigs** root around their pen and wander to the hay pile.
- **Horses** in the north field spook and gallop off.
- **The fox** in the woods keeps its distance.

A bark (Space) is a noise that every animal in range hears. Fences, the
barn and the trees are obstacles on a navmesh (Recast/Detour) built from the
scene itself when the level loads.

| Input | Does |
|---|---|
| WASD / left stick | move |
| Shift | run |
| Space | bark |
| Left click / Esc | capture / release the mouse to look around |
| F1 | show what each animal is thinking (action, score, fear, hunger, animation) |
| F2 | draw the navmesh |

## Running

```
cmake --build build --target farm_demo
KKE_ASSETS_DIR=/path/to/packs build/bin/farm_demo
```

| Variable | Effect |
|---|---|
| `KKE_ASSETS_DIR` | where the packs are (default `assets/synty/`) |
| `KKE_FARM_AUTOPILOT=1` | the dog runs a lap through the meadow and barks (headless checks) |
| `KKE_FARM_DEBUG=1` | start with the F1 panel open |
| `KKE_FARM_NAV=1` | start with the navmesh drawn |
| `KKE_FARM_LINEUP=1` | one of each animal in a row, all facing +X, with their AI off (facing check) |
| `KKE_SKIP_INTRO=1` | skip the logo intro |

## Assets (never committed)

- **POLYGON Farm** (Synty): the level, `scenes/farm.scene.json`. See the
  asset list in [docs/SCENES.md](../../docs/SCENES.md#farm-demo-scenesfarmscenejson).
- **POLYGON Dogs** (Synty): `Unity_SK_Animals_Dog_01` (the GermanShepherd
  and Fox parts) and its clips from `FBX/Animations`.
- **Farm Animals Animated by Quaternius** (CC0): `Sheep`, `Cow`, `Pig`,
  `Horse` FBX.

Put each pack in `assets/synty/` (a symlink to a shared cache works), or
point `KKE_ASSETS_DIR` at the folder that holds them.
`tools/fetch_assets.sh POLYGON_Farm POLYGON_Dogs` downloads the Synty packs.
Without POLYGON Farm, the animals still run on a bare field.

## How it's put together

- `FarmModule::setupAi` puts the built-in species (`sheep`, `cow`, `pig`,
  `horse`, `fox`) in their fields by setting `homeRadius`. It adds water
  places at the troughs and a grain place at the hay pile, and registers
  the player as a `dog` actor, which every species already has an attitude
  towards.
- The game side is small. Each frame the dog's position goes into
  `AiWorld::setTransform`, `AiWorld::update` runs, and every agent's
  position, `yaw` and `anim` ("idle", "walk", "run", "eat", ...) are copied
  onto its model.
- To make your own animal, see docs/AI.md: define a species in JSON or
  YAML, or call `ai.defineSpecies` from Lua.
