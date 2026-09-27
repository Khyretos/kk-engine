# Pet Companion

You and a pug in a fenced garden. Tell it to come, sit, stay, fetch, drop it
or go somewhere, pet it, or throw its ball, with a mouse and keyboard, a
controller or a finger.

- **Orders**: the engine's command layer (`kke/Orders.h`,
  [docs/COMMANDS.md](../../docs/COMMANDS.md)) and the same `order.*` Lua and
  nodes every game has (`scripts/pet.lua` praises every fetch and sits the
  dog after every third).
- **Brain**: the AI core's built-in `dog` species (`kke/ai/AiWorld.h`,
  [docs/AI.md](../../docs/AI.md)), pug-sized. Orders reach it through
  `kke::AiOrderBridge`; with none it sniffs about, rests and watches you.
- **What makes it a good companion**: it reads what you do (walk and it
  trots along, look at something and it comes to see, walk up to it and it
  waits for a pat, throw a ball and it fetches without being told), it
  never stands in your way, and it shows how it feels (it jumps for joy
  after a good fetch or a pat; the mood line on the HUD).
- **Legs**: `kke::ProceduralGait` on the pug's skeleton, a look-at on its
  head, a procedural sit.

## Controls

| | Mouse and keyboard | Controller | Touch |
|---|---|---|---|
| Move, look | WASD, Esc frees or captures the mouse | sticks | |
| Order at the pointer | click, or right click with the mouse captured | RB | tap (tap the dog to pet it) |
| Order wheel | hold Tab or the middle button | hold LB, right stick picks | hold a finger |
| Come / Sit / Stay / Fetch / Drop | 1-5 | D-pad | the buttons |
| Pet | E | Y | the Pet button |
| Pick up / throw the ball | click (mouse captured) | RT | the Throw button |

## Running

```sh
./pet_companion
KKE_PET_DEMO=1 ./pet_companion      # plays through every order and logs what the dog did
KKE_PET_QUIT=30 ./pet_companion     # quits after 30 s
```

## Assets used

Not in the repository (see `tools/fetch_assets.sh`); without them the
garden is built from blocks.

- Synty POLYGON Town: `SM_Env_Fence_Wood_Straight_01`, `SM_Prop_Doghouse_01`,
  `SM_Env_Tree_01`, `SM_Env_Tree_02`, `SM_Env_Hedge_01`, `SM_Prop_Barrel_01`,
  `SM_Env_Grass_01`, `SM_Item_Ball_Soccer_01`
- Quaternius Farm Animals (CC0): `Pug`
- Universal Animation Library mannequin (CC0, in the repository):
  `assets/animations/UAL1_Standard.fbx`
