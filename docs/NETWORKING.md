# Networking

Multiplayer v1 (issue #2): host a game or join one, on a LAN or on one PC.
You see the other players walk, jump, vault and climb, you push the same
crates, and you see their shots. v2 (issue #28, first part): breakables
break into the same pieces for everyone, what the host's scripts spawn
appears for everyone, and the host refuses moves through walls and
flights. The plan behind it, and what comes after
it, is in ACTION_PLAN.md "Networking (2.1)".

## Try it

Two `kke_demo` windows on one PC:

```sh
KKE_NET=host KKE_NET_NAME=Kees ./kke_demo
KKE_NET=join:127.0.0.1 KKE_NET_NAME=Guest ./kke_demo   # a second terminal
```

Or press F1 in `kke_demo` and use the **Network** panel: Host, Join by
address, or Search LAN (every game on the network and on this PC, with its
player count; click Join). The panel shows each player's round trip, loss
and your bandwidth, and has sliders to simulate a bad connection.

| Variable | Meaning |
|---|---|
| `KKE_NET` | `host`, `host:PORT`, `join:ADDRESS` or `join:ADDRESS:PORT` |
| `KKE_NET_NAME` | your name in the game and in the LAN list |
| `KKE_NET_LAG` | extra one-way delay in ms on everything this game sends |
| `KKE_NET_JITTER` | ± random ms per packet (reorders unreliable packets) |
| `KKE_NET_LOSS` | percent of unreliable packets dropped |

A host takes UDP port 27960, or the next free one up to 27975, so several
hosts can run on one PC and the LAN search still finds all of them. Allow
that range in the firewall to play across machines.

## How it works

```
 game (ShowcaseModule)      NetModule                 NetServer / NetClient      ITransport
 ---------------------      ---------                 ---------------------      ----------
 setLocalPlayer(state) ---> host: + bodies  -------->  snapshots, events    --->  ENet (UDP)
 remotePlayers()       <--- capsules, bodies <-------  interpolation        <---  Loopback (tests)
 sendEvent / onEvent                                   movement checks            ConditionedTransport
```

- **Transport** (`kke/net/Transport.h`): an interface with a reliable
  ordered channel and an unreliable sequenced one. `EnetTransport` is the
  real one (ENet, MIT); `LoopbackTransport` runs any number of peers in
  one process on a simulated network with a manual clock (the tests);
  `ConditionedTransport` adds lag, jitter, loss and duplicates to any of
  them (the panel sliders).
- **Serialization** (`kke/net/BitStream.h`, `Protocol.h`): Glenn Fiedler's
  pattern, one templated `serialize` function per message that both
  writes and reads, so the two can't drift apart. Values are bit-packed to
  their range and resolution: a position is 3 x ~23 bits at 1 mm, a
  rotation is "smallest three" in 35 bits. Every decoder rejects short,
  long and out-of-range packets; a fuzz test throws random bytes at all of
  them.
- **Sessions** (`kke/net/NetSession.h`): one server (the host's game, or a
  process of its own), any number of clients.
  - *Players*: each client moves its own player at once (no input delay)
    and sends its state 30 times a second. The server checks each move
    against speed limits (`MovementLimits`) and sends a `Correction` when
    one breaks them, then shares all players in its snapshots.
  - *Bodies*: the server's physics is the truth. Each snapshot is one UDP
    packet (1100 bytes); which bodies go in is decided per client by a
    priority accumulator: moving bodies gain priority faster than
    sleeping ones, and every body is sent eventually.
  - *Interpolation*: everything that isn't yours is drawn 100 ms in the
    past, between two received states, so it moves smoothly through
    jitter and a lost packet or two. A `ClockOffset` maps the sender's
    clock onto ours.
  - *Events*: reliable, game-defined messages (a shot, a push). The server
    receives a client's event and decides whether to apply and relay it.
  - *Spawned objects*: things the host makes while playing (a server
    script's crate or breakable) are `Spawn` messages: an id, a kind and
    the builder's own description (up to 256 bytes). Each client builds
    its copy; late joiners get the ones still there (a thrown ball is
    "transient" and isn't thrown again for them). Bodies then travel in
    snapshots like the level's; `Despawn` removes them.
  - *Breaks*: `Break` messages carry which borders of a breakable broke on
    the host (below).
  - *Robustness*: protocol version (now 2) and game id checked at join, a
    full server says so, silent peers time out, clients sending bad
    packets, server-only messages or too many events are dropped.
- **NetModule** (`kke/modules/NetModule.h`): the engine module that ties
  it to a game. The game registers its replicated Jolt bodies in the same
  order on every machine (`replicateBody`), gives its player's state each
  frame, and draws `remotePlayers()`. Other players get a kinematic
  capsule in the local Jolt world, so they block you and, on the host,
  push crates.

### Why the client simulates the crates too

The first version made a client's crates kinematic copies of the host's.
They looked right but could not be pushed: your character is blocked by
the local copy before your capsule on the host ever touches the real one.
Now a client simulates its crates like the host does, so walking into one
pushes it at once, and each frame steers every crate toward the host's
state (carried forward by its velocity to about "now"):

- velocity blend toward the host's velocity plus a position-error term
  (6/s, blended at 12/s), so contacts stay stable;
- a crate the host has asleep and that is (nearly) still here slides the
  last centimetres into place, because ground friction would otherwise
  eat the small correcting velocity and leave it a few centimetres off;
  not while you're walking into it, which is you starting a push;
- more than 2 m off (a reset, a missed tumble) it jumps.

Measured with two `kke_demo` instances: a guest pushes a crate 1.8 m, and
host and guest agree on where it stops to within 1 cm; with 80 ms lag each
way, 20 ms jitter and 5% loss both ends still agree within 1 cm. Under lag
a push feels heavier (the host's copy pulls back until it catches up).

### Breakables

FEMFX isn't deterministic across machines, so simulating each copy and
hoping isn't enough: one player's shot would crack the glass on one
screen and leave it whole on another. What *is* the same everywhere is
how a breakable is cut: its pieces are baked from a fracture seed
(`kke::bakeFracture`), and it breaks KKE's own way, border by border
(`kke::BreakGraph`, PhysicsModule "Breakable"). So:

- The host's breakables break from their stresses as always. Each frame
  NetModule asks each replicated one how many borders are broken
  (a cheap count) and, when that changes, sends the new borders as
  `(piece, piece)` pairs with the breakable's seed.
- A client's copies are *followers*: they never break on their own, only
  along the borders the host sends. Same pieces, same borders, same
  result: the pieces the client sees are the host's. Where the debris
  then flies is each machine's own (cosmetic, like before).
- A client whose copy was baked with another seed (another object in that
  slot, a changed world seed) logs an error and leaves it alone instead
  of breaking it wrongly.
- Late joiners get every border broken so far, and breaks for a breakable
  they don't have yet wait until it's there.
- Leaving a game makes them break on their own again.

Games call `replicateBreakable(handle)` for the level's breakables, in the
same order on every machine (kke_demo does it for the breaking yard);
script breakables are handled for you. Measured with two `kke_demo`
windows: the host's script drops a ball on a glass pane, the host's pane
breaks along 80 borders into 35 pieces, the guest's along the same 80 into
the same 35; the yard's stone wall, 6 borders and 2 pieces on both.

A break reaches a client about half a round trip after it happened (plus
the reliable channel's resend if a packet was lost), so on a client a
pane cracks a moment after the ball hits it.

### Movement checks

The server always checked moves against speed limits. With the level in
its own physics world, the host now also refuses (and sends the player
back to where it last was legitimately) a move that

- **goes through a wall**: the path from the last accepted position to
  the new one, 0.5 m above the feet, crosses static level geometry. The
  straight line or "up, over, down" must be clear, so stairs, vaults and
  ledge climbs pass; crates and other players don't count;
- **flies**: with nothing to stand on (0.6 m below) or hold (a wall or
  ledge beside you, for climbs, hangs and wall runs), it has either risen
  more than 3 m, or it has been in the air over 1.5 s without falling as
  (a third of) gravity would make it. Standing on a crate counts; your
  own stand-in capsule doesn't.

`kke/net/WorldMoveCheck.h` has the rules and `MoveCheckSettings` the
numbers (`NetModule::moveCheckSettings`; a glider or jetpack game turns
the fall rule off with `minFallGravity = 0`). The Network panel shows the
refusals and has a switch; the log gets one line per player per 5 s.
Any game can add its own rule through `NetServer::checkMove`.

### Why players are owner-predicted, not replayed

The standard for competitive shooters is server-side input replay: the
client sends inputs, the server runs them, the client rewinds and replays
its unacknowledged inputs when a correction arrives. That needs a player
simulation that can be rewound. `kke::Locomotion` (vault, climb, hang,
shimmy) can't be yet, and co-op and sandbox games don't need it, so the
owner moves its player and the host checks the moves (speed limits,
walls, flying: above). That catches blatant cheats; small ones (running a
little fast inside the slack) need input replay, which is still open in
issue #28.

## In kke_demo

- Players: the local character model, with its own animator per remote
  player, driven by their state (locomotion state, speed, traversal
  progress, crouch, fall height). Without the animation library installed
  they're boxes.
- Crates and the moving platform are the host's. R on a client asks the
  host to reset them.
- Shots: everyone sees every FEMFX ball. The breaking yard's glass,
  plank and stone wall break on the host and in the same pieces for
  everyone (Breakables above).
- Scripts: what `sv_*.lua` spawns shows up for everyone (docs/SCRIPTING.md
  "Multiplayer").

## Tests

`tests/test_net.cpp` (19 tests, all in CI):

- BitStream: round trips at the edges of every range, quaternions,
  strings, reading past the end.
- Protocol: every message round trips; truncated, padded, wrong-type and
  random packets are rejected (fuzzing).
- Loopback network: connect, both channels, disconnect; loss, delay and
  reordering only where UDP would (reliable stays complete and in order).
- Sessions on the loopback network: join and see each other and the
  host, bodies by priority within one packet, events and relays, a wrong
  version, a wrong game and a full server turned away, impossible moves
  corrected while a real teleport is allowed, a peer sending garbage
  kicked, leaving seen by everyone, other players still smooth at 80 ms
  ±25 ms lag with 10% loss.
- Real UDP: two ENet hosts on one PC, LAN discovery finds the host, a
  client joins and both see each other.

v2 adds (in the same file): `Spawn`, `Despawn` and `Break` round trips
and fuzzing, script spawn descriptions (damaged or out-of-range ones
refused), spawns reaching clients and only the persistent ones reaching
late joiners, breaks sent once and replayed to late joiners (2500 borders
split over several messages), a client sending server-only messages
kicked, and the game's move check refusing and correcting.

`tests/test_break_graph.cpp`: a follower given the host's borders ends up
with the host's pieces; borders that don't exist can't be broken.

`tests/test_move_check.cpp`: walking, jumping and a 30 m fall pass; through
a wall is refused; a vault, a ledge climb and five seconds hanging pass;
rising or hovering in the open is flying; a crate holds you up, your own
capsule doesn't; the honest fall after a refusal passes.

`tests/test_rigid_world.cpp`: bodies switch between dynamic and kinematic
(kinematic ones hold still and push, dynamic again they fall).

## Not yet

- Input replay for competitive games (#28: needs a rewindable
  kke::Locomotion), rollback for fighting games, lockstep for RTS
  (ACTION_PLAN.md).
- Dedicated server process and Docker image; secure connect tokens
  (yojimbo), internet P2P with NAT traversal (GameNetworkingSockets).
- Voice chat (Opus).
- Several local players per connection (couch + online).
- Breaking on a client before the host says so (predicted breaks): today
  a client's pane cracks half a round trip after the hit.
- FEMFX objects that aren't breakables (a thrown ball, soft bodies) aren't
  in snapshots: each machine simulates its own copy.
