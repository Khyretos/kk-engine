# Platoon

A squad of six on a training ground, five enemies dug in behind barriers
across it. Select soldiers and order them, all together or one at a time,
with a mouse and keyboard, a controller or a finger.

- **Orders**: go there (in a line, wedge, column or circle), hold position,
  take cover, attack a target, focus fire, follow a soldier, regroup, and
  queue them with Shift. They are the engine's orders (`kke/Orders.h`,
  [docs/COMMANDS.md](../../docs/COMMANDS.md)), the same `order.*` Lua and
  nodes every game has (`scripts/platoon.lua`).
- **Brains**: every soldier on both sides is an agent of the AI core
  (`kke/ai/AiWorld.h`, [docs/AI.md](../../docs/AI.md)), one "soldier"
  species with the team deciding who is an enemy. Orders reach it through
  `kke::AiOrderBridge`. Without orders your soldiers fight whatever they
  see; the enemy holds its line.
- **This layer**: formations (`kke::formationSlots`, slots matched to
  soldiers so paths don't cross), cover spots behind the crates and
  barriers, and the shots. The AI says who fires at whom; the game rolls
  the hit (line of sight and cover count) and deals the damage.

## Controls

| | Mouse and keyboard | Controller | Touch |
|---|---|---|---|
| Select | click, drag a box, Shift+click adds | A on the reticle, D-pad left/right: one soldier, D-pad up: everyone | tap soldiers (each tap adds) |
| Everyone | Space | D-pad up | Everyone button |
| Groups | 1-9 (Ctrl+1-9 stores; 1 all, 2 Alpha, 3 Bravo) | | |
| Order at the pointer | right click (Ctrl: focus fire / hold there, Shift: queue) | RB (LT: focus fire / hold there) | tap the ground or an enemy |
| Order wheel | hold Tab or the middle button | hold LB, right stick picks | hold a finger |
| Hold / cover / regroup / formation | H / C / R / G | X / Y / D-pad down / View | the buttons |
| Attack, focus fire a target | right click it | RB on it | the button, then tap the enemy |
| Camera | WASD pan, Q/E turn, wheel zoom | left stick pan, right stick turn and zoom | |

## Running

```sh
./platoon
KKE_PLATOON_DEMO=1 ./platoon     # plays a whole assault by itself and logs it
KKE_PLATOON_QUIT=60 ./platoon    # quits after 60 s
```

The self-play moves in a wedge, takes cover, sends one soldier alone,
focuses fire on one enemy, then clears the rest one by one and regroups.

## Assets used

Not in the repository (see `tools/fetch_assets.sh`); without them the
cover is built from blocks.

- Synty POLYGON Prototype: `SM_Prop_Crate_01`, `SM_Prop_Crate_02`,
  `SM_Prop_Crate_03`, `SM_Prop_Barrier_01`
- Universal Animation Library mannequin (CC0, in the repository):
  `assets/animations/UAL1_Standard.fbx`
