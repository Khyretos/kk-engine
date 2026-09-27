# Duel

A one-on-one fist fight in a boxing ring, against a bot or a second player
on the same machine. Best of three rounds; a round ends on a knockout or
after 90 seconds (the healthier fighter takes it).

It shows off the melee core (`kke::Combatant`, `kke::CombatWorld`,
[docs/COMBAT.md](../../docs/COMBAT.md)), the engine AI core driving the
opponent ([docs/AI.md](../../docs/AI.md)), Jolt ragdoll knockdowns with a
blended stand-up, two-bone IK for the guard, and local two-player input.

## Controls

| | Controller | Player 1 keyboard | Player 2 keyboard |
|---|---|---|---|
| Move (you always face the opponent) | left stick | WASD | arrow keys |
| Jab: quick, cheap | X | J / left mouse | numpad 1 |
| Uppercut: slow, knocks down | Y | K / right mouse | numpad 2 |
| Knee: breaks a guard | B | L | numpad 3 |
| Block (just as the punch lands: parry) | RB / LT | Left Shift | numpad 0 |
| Dodge | A | Space | numpad Enter |
| Next round / rematch | Start | R | |
| Second player takes the red corner | Back | F2 | |

Blocking costs stamina and chips a little health; a knee through a block
breaks the guard and leaves the blocker open. A parry stuns the attacker.
Heavy hits wear down poise; at zero you go down (a ragdoll) and get up a
couple of seconds later. Running out of stamina means no punches and no
dodges until it comes back.

## The bot

The red corner's tactics come from the engine AI core: the `boxer`
species in [data/boxer.yml](data/boxer.yml) scores *press*, *punish*,
*breathe* (back off when tired) and *circle* from inputs the game sets
(its stamina, whether you are open or guarding, a drifting patience). The
core's steering is its footwork, and its Attack events (in reach, cooldown
over) are when it throws. Edit that file to change its style; JSON works
too. Blocks, parries and dodges stay in `SparringBot.cpp` as reflexes,
because they are about timing, not choices.

## Environment

| Variable | Effect |
|---|---|
| `KKE_DUEL_LEVEL=easy\|normal\|hard` | the bot's reaction time, block and parry rates, aggression |
| `KKE_DUEL_BOTS=1` | both corners are bots (a demo that plays itself) |
| `KKE_DUEL_QUIT=<s>` | quit after that many seconds, logging a tally every 10 s |
| `KKE_DUEL_SEED=<n>` | the bots' dice |

## Assets

No Synty packs. The fighters are Quaternius' Universal Animation Library
mannequin (CC0): `UAL1_Standard.fbx` in `assets/animations/`, plus the
melee clips from UAL 2 (`UAL2.fbx` there or under `KKE_ASSETS_DIR`). Without
UAL 2 it still fights with UAL 1's punches. The ring is built in code.
