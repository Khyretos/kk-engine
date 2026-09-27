# AI.md — animals, enemies, companions and soldiers

Everything that thinks for itself in a KKE game runs on one core,
`kke::ai` (`engine/include/kke/ai/`). A sheep that bolts from a dog, a
goose that chases it off, a companion that fetches a stick and a soldier
holding a line are the same machine with different settings.

Each agent, every update, runs four layers:

| Layer | What it does | Where |
|---|---|---|
| **Perceive** | Sight (range, cone, line of sight), hearing (noises and footsteps), smell (scent trails that drift with the wind), touch (close, any direction). Each sense gives a strength, and *awareness* of each other thing rises with it and fades without it. | `kke/ai/Perception.h` |
| **Need** | Hunger, thirst and tiredness rise over time; grazing, drinking and resting lower them. Species can add their own. | `Species::needs` |
| **Decide** | Utility AI: every action gets a score from its *considerations* (an input read through a response curve); the best one wins. A player's order is an action that wins while it stands. | `kke/ai/Utility.h` |
| **Move** | The action's behaviour steers (Reynolds' steering behaviours and boids), along navmesh paths (Recast/Detour) when the level has one. | `kke/ai/Steering.h`, `kke/ai/NavMesh.h` |

All of it is pure CPU logic with no GPU, deterministic for a seed, and
covered by `tests/test_ai.cpp` (farm scenarios included: sheep scatter
from a dog and regroup, a cow comes to look, a goose attacks, a fox hunts
chickens and runs from the farmer, orders, navmesh walls).

## Using it from C++

```cpp
#include "kke/ai/AiWorld.h"
#include "kke/ai/NavMesh.h"

kke::ai::AiWorld ai(/*seed*/ 1);          // knows the farm species already
kke::ai::NavMesh nav;
nav.build(levelVertices, levelIndices);   // walls, fences, hills: automatic
ai.setNavMesh(&nav);
ai.lineOfSight = [&](glm::vec3 a, glm::vec3 b) { return !physics.rayHits(a, b); };

ai.addAgent(sheepId, "sheep", pos);       // your own ids (thing ids, entities)
ai.addActor(playerId, "dog", playerPos);  // perceived, never moved by the AI
ai.addPlace("water", troughPos, 1.5f);    // food, water, beds

// every frame
ai.setTransform(playerId, playerPos, playerVel, playerYaw);
ai.update(dt);
for (const kke::ai::Agent& a : ai.agents()) drawAnimal(a.id, a.position, a.yaw, a.anim);
for (const kke::ai::AiEvent& e : ai.takeEvents()) { /* Attack: deal damage; Heard: play a bleat */ }
```

- **Ids are yours.** The AI never owns meshes, bodies or animation. It
  moves agents kinematically (on the navmesh, or on `groundHeight`), or,
  when your game moves the body (a character controller, a ragdoll), read
  `Agent::desiredVelocity` and write back with `setTransform`.
  `setEnabled(id, false)` pauses an agent (ragdolled, carried, asleep).
- **`Agent::anim`** says what to play: `idle`, `walk`, `run`, `eat`,
  `drink`, `rest`, `alert`, `sniff`, `attack`. `Agent::focus` is what it's
  attending to (the thing it flees, chases, watches or guards against) and
  `lookAt`/`hasLookAt` where to turn the head. Procedural animation reads
  these.
- **Events** (`AiWorld::takeEvents`): `Spotted`, `Lost`, `Heard`, `Scared`,
  `Calmed`, `Attack` (in reach, cooldown over: *the game* deals damage),
  `Arrived` (an order's goal reached; for Interact, `other` is the thing),
  `ActionChanged`.
- **Noises:** `makeNoise({pos, loudness, source})`. Loudness is the range
  in metres: a bark is about 25, a shout 30, footsteps 2 walking and 5
  running. Footsteps of moving agents and actors are heard automatically
  (`Species::footstepsWalk/Run`).
- **Scent:** anything with `Species::scent > 0` leaves a trail (dog, fox,
  farmer by default). `ai.scent().wind` blows it; downwind of a trail an
  animal smells it from much further.
- **Debug:** `describe(id)` is one line ("sheep 12 flee 2.70 fear 0.90 ...");
  `actionScores(id)` and `input(id, name)` show why it chose what it did;
  `NavMesh::debugTriangles` draws the mesh.

## Species

A species is data: speeds, senses, temperament, who it fears / hunts /
likes / hates, what it eats and drinks, whether it herds, and optionally
its own list of actions. Built in (`builtinSpecies()`): **sheep, cow, pig,
chicken, horse, goose, cat, dog, fox, farmer.** Change them or add your own
from a JSON or YAML file (`AiWorld::loadSpecies`, the same format either
way, [DATA_FILES.md](DATA_FILES.md)); an entry with a built-in id only
changes the keys it gives:

```yaml
species:
  - id: wolf
    runSpeed: 9
    senses: { sightRange: 30, smell: 3 }
    temperament: { boldness: 0.7, curiosity: 0.2, aggression: 0.5, sociability: 0.8 }
    hunts: [sheep, chicken]
    fears: [farmer]
    flocks: true          # a pack
  - id: sheep
    walkSpeed: 0.9        # only this changes
```

How one agent feels about another (`AiWorld::attitude`), first match wins:
a per-agent override (`setAttitude`, `ai.feel`) → teams (`setTeam`: same
nonzero team friendly, different nonzero teams hostile) → same species
friendly → the species lists (`friends`, `hunts`, `hostileTo`, `fears`;
`"*"` means anyone; an animal with aggression above 0.6 is hostile to what
it would fear) → curious if its curiosity is above 0.25, otherwise it
ignores them.

## Actions

Without an `actions` list a species gets `defaultActions()`:

| Action | Behaviour | Scores high when | Weight |
|---|---|---|---|
| order_moveto / follow / attack / hold / flee / interact | goto, follow, attack, hold, flee, interact | it has that order | 3 |
| flee | runs away (fanning out around walls on the navmesh) | `fear` | 2 |
| fight | goes for what it's hostile to, back home when chased off | `hostile` | 1.8 |
| hunt | chases prey (if the species hunts) | `prey`, and more when hungry | 1.5 |
| investigate | walks up to a curious thing, stops at `investigateDistance`, sniffs | `curious`, not afraid | 0.9 |
| watch | stops and looks at something it half noticed | `alert` | 0.7 |
| graze / drink | goes to a food / water place and eats / drinks | `hunger` / `thirst`, and a place known | 1 |
| rest | lies down | `tiredness` | 0.9 |
| regroup | goes back to its herd | `alone` (herd animals) | 0.6 |
| wander / idle | strolls around home / stands | `restless` (changes every 5-15 s) | 0.35 / 0.3 |

The inputs a consideration can read: `fear`, `hostile`, `threat`, `prey`,
`curious`, `alert`, `food`, `water`, `alone`, `restless`, `leader_far`,
`order_<kind>`, the mood (`boldness`, `curiosity`, `aggression`,
`sociability`), every need by name, and any value the game or a script
sets (`setInput(id, "health", 0.3)`, `ai.setInput`). So a fighting game
adds "retreat when hurt" in data:

```yaml
actions:
  - name: retreat
    behavior: flee
    weight: 2.5
    considerations:
      - { input: health, curve: logistic, mid: 0.3, steepness: -12 }  # low health
      - { input: threat, curve: linear }
```

Curves: `linear` (m slope, b shift), `inverse`, `quadratic` (k exponent),
`logistic` (an S: `mid`, `steepness`), `logit`, `step` (`threshold`),
`constant` (`value`); every one takes `invert`. A score multiplies its
considerations (with Dave Mark's compensation, so more considerations
don't mean lower scores) and the weight; the running action keeps a
`momentum` bonus so agents don't flicker; `cooldown` stops an action being
picked again right after it ends.

Why utility AI and not behaviour trees: animals' reasons are continuous
("a bit hungry, quite scared"), and "the one that wants it most wins"
reads the same in Lua, in a node graph and to a child. Orders slot in as
actions that win. A behaviour-tree layer (for scripted boss phases) can
sit on top later; it would drive orders and inputs, not replace this.

## Orders (companions and squads)

`AiWorld::order(id, Order{kind, target, position, distance, run})`:

| Kind | Does | Ends |
|---|---|---|
| MoveTo | walks (or runs) to `position` | on arrival: `Arrived`, then holds there |
| Follow | stays within `distance` of `target`, runs to catch up | when changed |
| Attack | goes for `target`, attacking in reach | target gone, or changed |
| Hold | stays at `position`, faces and attacks what comes in reach; `focus` is the hostile it's watching (a shooter aims at it) | when changed |
| Flee | runs from `target` (or `position`) | far enough |
| Interact | goes to `target` | on arrival: `Arrived` with the thing (fetch, pet, open) |

`clearOrder` ("do as you like") hands it back to its own wants. A
companion is a species with the owner as a friend and a Follow order; a
platoon is agents on one team given orders one by one or together (focus
fire = the same Attack target for all). Group formations and cover are
the platoon demo's to build on top of this.

## Navigation

`NavMesh::build(vertices, indices, settings)` takes the level's collision
triangles (y up, metres) and makes the walkable surface for one agent
size (`NavMeshSettings::agentRadius`, `agentHeight`, `agentMaxClimb`,
`agentMaxSlope`). Queries: `findPath` (corner points, partial when
unreachable), `moveAlongSurface` (per-frame sliding), `walkable` (straight
line clear?), `randomPointNear`, `nearestPoint`. `save()`/`load()` cache
it next to a level. Not thread-safe per mesh. Built with
[Recast & Detour](https://github.com/recastnavigation/recastnavigation)
(zlib), the library Unity, Unreal and Godot's navigation grew from.

Without a navmesh everything still works on open ground (steering only),
with `addObstacle` circles for trees and posts and `groundHeight` for
hills.

## Lua

`kke::ai::bindAi(vm, world, locate)` binds `ai.*` (`kke/ai/AiScript.h`),
and `fireAiEvents(vm, world.takeEvents())` runs the hooks.

```lua
ai.add(me, "sheep")                        -- give a thing a mind
ai.follow(dog, player, 2)                  -- orders: goTo, follow, attack, hold, flee, fetch, free
ai.setMood(dog, "boldness", 1)             -- boldness, curiosity, aggression, sociability
ai.setNeed(sheep, "hunger", 1)
ai.noise(Vec(3, 0, 4), 25, dog)            -- a bark
print(ai.doing(sheep), ai.need(sheep, "hunger"), ai.knows(sheep, dog))
ai.setTeam(goblin, 2); ai.feel(cat, dog, "fear"); ai.setInput(me, "health", 0.4)
ai.defineSpecies{ id = "wolf", runSpeed = 9, hunts = { "sheep" } }

hook.Add("Scared", "bleat", function(e) play.sound("bonk", play.position(e.who)) end)
hook.Add("Arrived", "fetch", function(e) if e.thing then ai.follow(e.who, player) end end)
```

Events: `Spotted {who, what}`, `LostSight {who, what}`, `Heard {who,
where, by}`, `Scared {who, of}`, `Calmed {who}`, `Attacks {who, target}`,
`Arrived {who, at, thing}`, `StartsDoing {who, action}`.

## The three play-to-make levels

Following [PLAY_TO_MAKE.md](PLAY_TO_MAKE.md), every block above is the
same thing at all three levels:

- **Advanced:** the Lua above.
- **Intermediate:** every documented `ai.*` binding is a node and every
  event a *when* node, automatically (the node library is built from the
  Lua API): *When it gets scared → Make a noise*, *When tapped → Follow
  (me)*. Categories: Minds, Orders, Senses, Learning.
- **Simple:** the sandbox palette's Sheep, Cow, Pig and Horse
  (`PlayBlockKind::Animal`, see [PLAY_TO_MAKE.md](PLAY_TO_MAKE.md)) are
  recipes of those nodes. *When put down → Be a "sheep"* is `ai.add(me,
  "sheep")`, so "look inside" opens a real graph. In Play the sandbox runs
  the AI and moves each animal. People are what the animals see, and the
  bat's swing is a noise they hear. `kke::ai::clipForAnim`
  (`kke/ai/Clips.h`) picks each model's clip for what it is doing.

## The farm demo

`games/farm_demo` ([README](../games/farm_demo/README.md)) is the whole core
in one small game. You play a dog, and sheep, cows, pigs, horses and a fox
live on built-in species. The level has only small tweaks: each species
gets a `homeRadius` and there are water and grain places. The navmesh is
built from the scene's own meshes. F1 shows each animal's action, score,
fear, hunger and animation. `KKE_FARM_AUTOPILOT=1` runs it headless.

## Teaching by example

An animal can learn what you show it: this is imitation learning, done on
the device. `kke/ai/Learning.h` has the full description.

1. **Show.** `world.teach(id, "rest")` (Lua `ai.teach(thing, "rest")`,
   node *Show what to do*) records one example for that animal's species:
   the numbers its considerations read right now (fear, food, alone,
   tiredness, custom inputs, ...) and the action you picked.
2. **Learn.** `world.learn("cow")` (`ai.learn("cow")`, *Learn from what it
   was shown*) trains a small neural network on every example so far. It
   is [genann](https://github.com/codeplea/genann), with one hidden layer
   of 8 and sigmoid outputs, and takes milliseconds. It returns how many
   examples it now gets right.
3. **Lean.** From then on, each think adds `learnedWeight × probability` to
   every action that already scores above 0. With the default weight of
   1.5, a clearly taught action beats a mild instinct. It doesn't beat
   orders (weight 3) or strong fear (flee, 2), and it never makes an
   animal do something that makes no sense right now.

`world.unlearn("cow")` (`ai.unlearn`) goes back to instinct and keeps the
examples. `LearnedPolicy::save` / `load` write the examples and the
weights as JSON or YAML (`kke.ai.policy` v1). `world.setPolicy` puts a
loaded one back. Training is reproducible: the start weights and the
shuffle come from the world's seed, not `rand()`.

In the farm demo, Tab picks a lesson, E shows it to the nearest animal,
and L lets every kind that was shown something learn from it.
Reinforcement learning, where an animal learns from rewards instead of
from examples, is not built yet. It would use the same features and
actions.

## Performance

Perception runs 10 times a second and decisions 4 times a second per
agent, spread over frames; movement runs every frame. Neighbour queries
use a uniform grid (`SpatialGrid`).

## Not yet

- Learning from rewards (reinforcement learning). Teaching by example
  (above) is built.
- Tiled / dynamic navmeshes (DetourTileCache is compiled in, not wrapped
  yet), off-mesh links for jumps and ladders, per-area costs (mud, water).
- Crowds of hundreds on one path (DetourCrowd is compiled in; steering and
  separation handle a farm's worth today).
