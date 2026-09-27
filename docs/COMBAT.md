# Melee combat

`kke/Combat.h` is game-agnostic melee: timing, stamina, blocking, parries,
poise and knockdowns. It knows nothing about input, animation or AI; a game
tells each fighter what to do and reads back what happened.

Used by [games/duel](../games/duel/README.md) (one on one) and the goblin
horde demo (one against many, sweeping attacks).

## Pieces

- **`AttackDesc`**: one attack. `windup` → `active` → `recovery` seconds,
  `damage`, `staminaCost`, `poiseDamage`, reach (`reach`, `height`,
  `radius` of the strike point), `knockback`, `hitStun`, `blockStun`,
  `chip` (damage through a block), `guardDamage` (stamina taken from a
  blocker), `unblockable`, `sweep` (hits everyone in reach, not just the
  first). Presets: `light()` (a jab), `heavy()` (slow, sweeps, knocks
  down), `kick()` (breaks guards).
- **`CombatStats`**: a body. Health, stamina and its regeneration (slower
  while blocking), poise and its regeneration, block angle, parry window,
  stun lengths, dodge time and cost, knockdown time, capsule size.
  Presets: `fighter()`, `grunt()` (a small, fragile crowd enemy).
- **`Combatant`**: the state machine. Idle → Windup → Active → Recovery,
  plus Stunned, Dodging, Knockdown and Dead. `attack(desc)`,
  `setBlocking`, `dodge`, `getUp`, `canAct`, `windupLeft` (what an AI
  reads to react), the bars and their fractions.
- **`CombatWorld`**: all combatants in teams. `add(team, stats)`,
  `step(dt)` resolves every active strike against capsules (a uniform grid,
  so hundreds of grunts cost little) and returns `HitEvent`s: Hit, Blocked,
  GuardBroke, Parried, Knockdown, Killed, with the damage, the point, the
  push and how high on the body it landed (for hit reactions).

## Rules in one paragraph

A block facing the attack (within `blockAngle`) takes `chip` damage and
costs stamina (`guardDamage`); out of stamina, the guard breaks and the
blocker is stunned. A block raised within `parryWindow` of the hit parries
it: no damage, and the attacker is stunned. Every hit costs poise; at zero
the target is knocked down (`knockdownTime`, then `getUp`). A dodge is
brief invulnerability that costs stamina. Nothing can act while stunned,
knocked down or dead.

## Around it

- Animation: pick the clip from `Combatant::state()` and `currentAttack()`;
  time the clip to `windup + active + recovery`.
- Knockdowns: `kke/Ragdoll.h` (`IRagdollPhysics`), blended back with
  `blendPoses` on the stand-up; `kke/ProceduralAnim.h` for flinches.
- AI: the engine AI core ([AI.md](AI.md)) decides tactics and when to
  throw (its Attack events); the game maps that to `attack()`.

## Crowds of modular characters

- **`kke/Sidekick.h`** loads Synty SIDEKICK characters: `readSidekickCharacter`
  reads a `.sk` part list (YAML), `loadSidekickCharacter` merges the part
  meshes onto one skeleton (`mergeSkinnedModels`, bones unified by name),
  applies the character's colour map and joins meshes by material.
- **`kke/MeshLod.h`** (meshoptimizer): `weldModel` and
  `simplifyModel(model, ratio, options)`. A Sidekick goblin goes from 18k
  to 2.7k triangles at ratio 0.15 with `acrossSeams` and `prune`, which is
  what makes a horde affordable with CPU skinning.
