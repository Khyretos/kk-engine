# FEMFX <-> Jolt bridge

FEMFX (deformable, breakable objects) and Jolt (rigid bodies, the
character) are two separate worlds. `kke::PhysicsBridgeModule` (issue #7)
lets them meet. Add it next to `PhysicsModule` and `RigidBodyModule`, as
`kke_demo` does.

## What it does, every fixed step

1. **Jolt -> FEMFX.** Jolt bodies and characters near *moving* FEMFX
   pieces are mirrored into FEMFX as kinematic boxes
   (`PhysicsModule::setExternalBoxes`). Pieces land on crates and walls,
   bounce off them, and get shoved by the character. A box is exact for
   Jolt boxes and the shape's bounds for spheres, capsules and hulls.
   Triangle-mesh bodies (Synty level meshes) are left out.
2. **FEMFX -> Jolt.** FEMFX reports no contact forces, so the bridge
   measures them. It records the velocities of the vertices near each
   movable box before the step. After the step, a vertex at the box's
   surface whose velocity changed (beyond gravity) away from the box was
   pushed by it, and the box gets the opposite push
   (`kke::boxContactImpulse`). A shard resting on a crate presses it down
   with its weight. A shard flying into a crate's side pushes it along.
   The pushes go into Jolt as impulses (`RigidWorld::addImpulse`).

## Why only near moving pieces

FEMFX treats rigid bodies as always awake, and a rigid body touching a
sleeping piece wakes it (docs/PERFORMANCE_NOTES.md: that once kept a
settled pile of 480 pieces awake). So a box exists only while a piece
near it is awake, or while the Jolt body itself moves into a piece.
Settled scenes have no boxes and cost nothing. The proxies also have
FEMFX sleeping on, so pieces resting on them can fall asleep.

## Collision groups

FEMFX starts with every collision group colliding only with itself.
`PhysicsModule` pairs the pieces (group 0) with the proxies
(`kExternalCollisionGroup`, 4) and with ragdoll limbs (group 3). Without
that pairing the proxies exist but nothing hits them.

## Rubble: small pieces leave FEMFX (issue #31)

FEMFX costs ~0.15-0.2 ms per awake piece a step; Jolt a few microseconds
per body. So once a breakable's break has played out (the piece is past
its break grace and not about to split), the bridge hands pieces over:

- small ones (at most `rubbleMaxSize`, 0.35 m, and `rubbleMaxTets`, 64), and
- any piece that is a single baked chunk (nothing left in it to break),
  up to `rubbleMaxChunkSize`, 0.8 m.

Each becomes a Jolt convex hull of its vertices with the piece's mass,
velocity and spin (`PhysicsModule::describeRubble`), and the piece leaves
the FEMFX scene (`convertToRubble`). PhysicsModule keeps drawing its
frozen surface where Jolt puts it (`setRubbleTransform`), and a
breakable's embedded render points (Synty props) follow it too. At most
`rubblePerStep` (6) go per fixed step, so a pane shattering into 30
shards is spread over a few steps. `rubbleBudget` (800) caps the Jolt
debris: past it the oldest resting piece is removed.

Rubble isn't mirrored back into FEMFX as boxes: FEMFX pieces pass through
shards lying on the ground (a detail; dozens of shards would fill the
proxy budget). Pieces broken by FEMFX's own fracture (not a breakable)
stay in FEMFX.

## One interface for both engines

`kke::IPhysicsWorld` (kke/Capabilities.h) is what both modules answer:
raycasts, blasts (a velocity change fading with distance), stats and
bounds queries. `kke/PhysicsWorld.h` asks every module at once:

```cpp
auto worlds = app.findCapability<kke::IPhysicsWorld>();
kke::IPhysicsWorld::Hit hit = kke::physicsRaycast(worlds, from, dir, 50.0f); // closest, either engine
kke::physicsBlast(worlds, hit.point, 3.0f, 8.0f);                            // crates, shards and glass alike
```

Ragdolls go through `IRagdollPhysics` the same way; `bestRagdollPhysics()`
picks Jolt's when both engines are there (joint limits, limbs collide),
FEMFX's otherwise.

## Things that exist in both worlds

A support block that is both a FEMFX object and a Jolt static box
(`kke_demo`'s breaking yard) must not be mirrored: FEMFX would find a
kinematic box inside its own block. `PhysicsBridgeModule::ignore(body)`
leaves such a body out.

## Trying it

- `KKE_DEMO_BRIDGE=1 ./kke_demo` (a FEMFX build) drops an iron ball on the
  yard's glass. The shards fall on the three small crates under it.
  Then it rolls a second ball along the ground into the pyramid's
  bottom crate. The log says how far the crates moved.
- `KKE_BRIDGE=0` turns the bridge off, for comparison: the shards then
  fall through the crates to the ground.
- `KKE_RUBBLE=0` keeps every piece in FEMFX, for comparison.
- `KKE_BRIDGE_LOG=1` logs once a second how many boxes are mirrored and
  how many pushes went into Jolt, and what the bridge costs a step. The "FEMFX <-> Jolt" panel (F1) shows
  the same, with an on/off switch and a push scale.

## Measured (FEMFX build, Xvfb, 2026-09-26)

| | Bridge on | Bridge off |
|---|---|---|
| Glass shards on the three crates | 120-300 N s of pushes a second, crates settle 3-4 mm | shards pass through |
| Iron ball rolled into a crate | crate knocked 0.84 m back | ball passes through |
| Bridge cost | 0.04-0.05 ms a fixed step (6-14 boxes) | 0 |

Rubble (same run, `KKE_DEMO_BRIDGE=1`, Debug build, Xvfb, 2026-09-26):

| | Rubble on | `KKE_RUBBLE=0` |
|---|---|---|
| Glass pieces left in FEMFX | 0 of 31 (all to Jolt within a second) | 31, all awake |
| FEMFX step, the second the glass breaks | 15.5 ms avg, 26 ms max | 30 ms avg, 45 ms max |
| FEMFX step afterwards | ~9.5 ms (8-9 awake pieces) | ~22 ms (40 awake pieces) |
| Bridge cost, handoff second | 0.15 ms a step | 0.09 ms |

`kke_physics_benchmark` doesn't add the bridge, so its numbers are
unchanged.

## Limits

- Kinematic proxies can't be moved by FEMFX within a step, so heavy
  FEMFX objects push Jolt bodies one step late, through the measured
  impulses.
- Rubble doesn't deform or break again once it's in Jolt.
- FEMFX's `rigidBodiesExternal` scene mode (a tighter coupling than
  kinematic proxies) hasn't been evaluated.
