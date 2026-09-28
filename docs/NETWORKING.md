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

Climb Race has it in its start menu: **Online: Host** on one machine,
**Online: Join** on the other, pick the game and press Join, with up to
four players on each screen (games/climb_race/README.md "Online").

| Variable | Meaning |
|---|---|
| `KKE_NET` | `host`, `host:PORT`, `join:ADDRESS`, `join:ADDRESS:PORT` or `join:CODE[@RELAY]` |
| `KKE_NET_RELAY` | `host[:port]` of a relay: hosting gets a join code, and codes typed without `@relay` are asked there (docs/SERVER_HOSTING.md "Join codes") |
| `KKE_NET_NAME` | your name in the game and in the LAN list |
| `KKE_NET_LAG` | extra one-way delay in ms on everything this game sends |
| `KKE_NET_JITTER` | ± random ms per packet (reorders unreliable packets) |
| `KKE_NET_LOSS` | percent of unreliable packets dropped |
| `KKE_NET_REPLAY` | `1`: host with input replay (competitive; see "Input replay") |

A host takes UDP port 27960, or the next free one up to 27975, so several
hosts can run on one PC and the LAN search still finds all of them. Allow
that range in the firewall to play across machines, or use a join code:
with a relay set, the host's panel shows a code like `K7M-Q2P@relay.example.org`
and friends type it in the Address box instead of an address; nobody
forwards a port (docs/SERVER_HOSTING.md "Join codes").

## Encryption

Every connection is encrypted, on a LAN too, with nothing to set up
(`kke/net/SecureTransport.h`, issue #44). The client and the server each
make a throwaway X25519 key; the server adds its long-term key and proves
it holds it; both derive one key per direction with BLAKE2b; every packet
is then sealed with XChaCha20-Poly1305 (Monocypher, already used for
`kke_seal`) and a counter as the nonce. So:

- nobody on the path (café Wi-Fi, a relay, an ISP) can read the password,
  chat, voice or moves, nor change or replay a packet: a packet that
  fails its check is dropped, and a peer sending more than 20 of those
  is dropped too;
- a joiner who knows the server's key (a join code: the relay hands it
  over) refuses any other, so nobody in the middle can pose as the
  server; typing a plain address has no key to compare with (encrypted
  all the same, like a first visit over https without a certificate);
- keys are forward secret: a server key stolen later doesn't open old
  recordings.

It costs 25 bytes per packet and a few microseconds of CPU. `kke_server`
keeps its key in `saveDir/server.key` (owner-only) and prints its
fingerprint at start; a game hosting makes a new key each time. The
handshake has its own version, so a game from before encryption can't
join a new one (and is told nothing it could use).

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
    sleeping ones, and every body is sent eventually. Players work the
    same way once they fill more than 60% of the packet (up to 254 on a
    server): the ones in your group first, then the nearest, and every
    player is sent often. A player left out of one snapshot is listed
    as still there in a few bits, so nobody blinks out.
  - *Interpolation*: everything that isn't yours is drawn 100 ms in the
    past, between two received states, so it moves smoothly through
    jitter and a lost packet or two. A `ClockOffset` maps the sender's
    clock onto ours.
  - *Events*: reliable, game-defined messages (a shot, a push). The server
    receives a client's event and decides whether to apply and relay it.
  - *Calls*: a script's `net.call` is an event (`script_net::kScriptCall`)
    with a request id; the server's `net.handle` runs it and answers that
    player alone (`kScriptReply`) with the result or why not. A handler
    runs all or nothing (a store transaction, with its sends and spawns
    held back until it succeeds), the idea of SpacetimeDB's reducers
    (`kke/ScriptCalls.h`, docs/SCRIPTING.md "Calls"). A server without
    scripts answers "nothing answers" at once rather than passing the
    call on.
  - *Synced tables*: the server's scripts keep tables of keyed rows
    (`net.table`); a player's `net.watch` subscribes to one, whole or
    filtered on fields (court == 3), with `kTableSubscribe`. The server
    sends the matching rows, "ready", then per tick only the inserts,
    updates and deletes (`kTableRows`, packed into as few reliable
    events as fit). Reconnecting or a new host re-subscribes and shows
    only the differences. SpacetimeDB's tables and subscriptions,
    without SQL (`kke/net/SyncedTables.h`, `kke/ScriptTables.h`,
    docs/SCRIPTING.md "Synced tables").
  - *Groups*: one server holding many rooms, like the ten courts of a
    sport center. Every player and body is in a group (0 by default);
    a client is sent the players and bodies of its group and of the
    groups it shows, so a spectator in the lobby (group 9) can watch
    court 3 without the other nine courts' traffic. A non-solid player
    has no capsule in the server's physics, so spectators walk through
    the ball and never move it (`NetServer::setGroup`, `showGroups`,
    `setBodyGroup`, `setSolid`; in Lua `net.setGroup` and friends,
    docs/SCRIPTING.md "Groups"). Hiding and priority are about what is
    sent, not about cheating: fog of war (`sendPlayer`) still decides
    on top.
  - *Spawned objects*: things the host makes while playing (a server
    script's crate or breakable) are `Spawn` messages: an id, a kind and
    the builder's own description (up to 256 bytes). Each client builds
    its copy; late joiners get the ones still there (a thrown ball is
    "transient" and isn't thrown again for them). Bodies then travel in
    snapshots like the level's; `Despawn` removes them.
  - *Breaks*: `Break` messages carry which borders of a breakable broke on
    the host (below).
  - *Robustness*: protocol version (now 9) and game id checked at join,
    then the server's password if it has one (compared in constant time;
    `KKE_NET_PASSWORD`, or the panel's Password field) and its access list
    (`NetServer::admit`: bans, allow list), a full server says so, silent peers time out, clients sending bad
    packets, server-only messages or too many events are dropped.
- **Dedicated servers**: `NetConfig::dedicated` gives all slots to
  clients (no host player). `kke_server`, its roles, the directory and
  Docker are in docs/SERVER_HOSTING.md. A server's text for players
  (MOTD, `say`) is the event `kEventServerMessage`; NetModule logs it and
  shows it in the panel.
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

### Input replay

Two models for players, chosen by the host:

- **Owner-predicted** (the default; co-op, sandbox, most games): your game
  moves your player and sends where it is; the host checks each move
  (above). Nothing to set up, nothing ever snaps back for an honest
  player, and it catches the blatant cheats. Small ones (running a little
  fast inside the slack) get through.
- **Input replay** (competitive; tick **Input replay** next to Host, or
  `KKE_NET_REPLAY=1`): your game sends *inputs* (move direction, sprint,
  walk, crouch, jump, where you look) 60 times a second and the host runs
  everyone's movement itself. Where a player is, is whatever the host's
  own `kke::Locomotion` made of their inputs: speed hacks, fly hacks,
  teleports and walking through walls have nothing to send. This is the
  model of competitive shooters (Valve's Source, Overwatch, Rocket League).

Nobody wants to wait for the host before their player moves, so with
input replay your game still moves your player the moment you press
(prediction). It keeps each input and the state after it; when the host's
answer for an input disagrees (by more than 5 cm, or in another movement
state: a vault the host didn't do), it goes back to that input, takes
the host's position and plays every input since again (rewind and
replay). With an honest client and the same level both sides agree and
nothing moves; when they don't, the jump is hidden by fading a drawing
offset (`playerDrawOffset()`).

What makes it work (`kke/net/InputReplay.h`, `LocomotionReplay.h`):

- *Exactly the same numbers on both sides.* Inputs are quantized before
  the client steps them, to what the wire carries (`quantize()`), and
  both sides step fixed 1/60 s ticks whatever the frame rate.
- *Rewindable movement.* `RigidWorld::characterState()` saves a
  character completely (Jolt's own state of the `CharacterVirtual`:
  position, velocity, contacts, ground; its input, height and clock) and
  `setCharacterState()` puts it back; a `Locomotion` is plain data, so a
  copy is its whole state (`restore()`). A replayed character is stepped
  on its own (`stepCharacter()`, `setCharacterManual()`), one input at a
  time, so it can be replayed without stepping the whole world.
- *The host waits rather than guesses* (`InputQueue`). Each host tick a
  player may play one input. If theirs hasn't come (a slow frame, a
  jittery link) they stand still a moment on the host and the ticks are
  made up when the inputs arrive together, so the host runs exactly the
  inputs the client predicted with: a bad connection costs latency, not
  corrections. More than one input per tick on average can't be had:
  a client sending faster only fills the queue, whose oldest inputs are
  skipped (a jump in them still happens).
- *Loss.* Every input packet carries the newest 16 unacknowledged inputs,
  so a lost packet's inputs arrive with the next. The host answers with
  `InputAck` (the last input it played and the state after it) with
  each snapshot.

In a game: `NetModule::setPlayer(&locomotion, character)` once, then each
frame `if (!net.stepPlayer(input, dt)) locomotion.update(...)` instead of
updating it yourself: offline, hosting or with owner prediction it
returns false and nothing changes. The host creates a character and a
`Locomotion` for each joining client (at `replaySpawn(id)`) and runs
them. The showcase does all of this (`ShowcaseModule::stepNetPlayer`).

For now, with input replay: players don't block each other (the host's
characters don't collide with one another, so a client's prediction
doesn't bump into stand-ins either); a teleport or respawn is the host's
to make (one made by the client alone is put back); crates and other
bodies near you are the host's, so pushing one is predicted only as far
as your copy of it goes. Everything else (events, voice, spawns, breaks)
works as with owner prediction.

## Several players on one screen

Split screen online, like Halo: two (or more) people on one couch join
someone else's game from one machine, and the host can have friends on
its couch too. Up to 8 players share one connection (slot 0 is the
game's own player, slots 1 to 7 are guests); each is a player of its own
for everyone else, with its own id, name, state and movement checks.

```cpp
net.addLocalPlayer(1, "Sam");            // before or after host() / join()
// each frame, like setLocalPlayer(state) for the first one:
net.setLocalPlayer(1, samState);
net.localPlayerId(1);                    // its id in the game (0 until the host gave one)
net.isLocalPlayer(id);                   // one of this screen's? (don't draw it as a remote)
net.removeLocalPlayer(1);                // Sam puts the controller down
```

- On the wire (protocol 6, and 7 for 32 extra bytes): a client's `Guest` message asks for a player
  in a slot, the host's `GuestAck` gives its id or says why not (the game
  is full, a ban, input replay). `PlayerState` and `Correction` carry the
  slot. A host's own guests need no messages (`NetServer::addLocalGuest`).
- Guests count toward `maxPlayers`, so a full game turns the next guest
  or joiner away; the host's LAN and directory entries count them too.
- Nobody is sent their own screen's players in snapshots; the fog of war
  shows a player to a screen when any of its players can see it.
- Guests share their connection's events (the event's `fromPlayer` is
  the connection's first player) and voice (one microphone per machine).
- A guest leaving costs only it; the connection going takes its guests
  with it; kicking a guest tells its owner and keeps the connection.
- `NetPlayerState::extra` (up to 32 bytes, the game's own) travels with
  every state: Climb Race puts where the hands and feet are in it.
- Not with input replay yet: there each connection plays one player, and
  a guest is refused with the reason.

## In Climb Race

A whole game on top of the above (games/climb_race/Net.cpp, NetRace.h),
and a template for a menu-driven one:

- The start menu (docs/LOBBY.md) gets three rows with `lobby().addOption`:
  Online (Off / Join / Host), Game (what `NetModule::searchLan()` found:
  `lanGames()` gives address, port, host name, players and whether it's
  this game) and Join. Host calls `host()`, Join calls `join(address, port)`.
- Every lobby seat is a player: the first is NetModule's own, the others
  `addLocalPlayer(slot, name, character)`, with the colour as the
  character ("#5aa6ff"). The host's CPU climbers are host guests too, so
  they're players everyone sees.
- The host decides the race with game events (base 0x4300): Setup (the
  mountain, and which player climbs which face), Ready and Go (the
  countdown waits for every machine), Finish and Loose (a hold came off).
- Each climber's pose is its NetPlayerState: feet, facing, state (on foot,
  climbing, mantling) and, in `extra`, the hands and feet relative to it
  (1 mm), the hips, and each hand's rock normal and grip: 29 bytes.
- `standIns = false`: the game draws the other players itself (a climber
  on the right face), so NetModule makes no capsules for them.
  `checkMoves = false`: a mantle over an edge looks like going through the
  rock to the wall check; the speed limits still apply.

The Flying demo (games/flying_demo/Net.cpp) does the same with planes,
and raises the server's speed limits to a stunt plane's before hosting:
`NetModule::movementLimits` (a `net::MovementLimits`: horizontal, rise
and fall speed, slack, teleport cooldown) is what the host's server
checks every move against; set it before `host()`.

Tennis (games/tennis/Net.cpp, NetTennis.h, events from 0x5400) shows a
ball game: nothing about the ball streams. The hitter's machine sends the
hit (where, velocity, spin) and every machine flies the same maths from
the numbers as they come off the wire; the host is the umpire (Serve and
Point events, each point numbered so a late hit is dropped) and sends
its ball a few times a second only to put drift right. Its sport center
runs up to ten matches at once for everyone: each event names its match,
the host's CPU players for all of them go in one event ten times a
second, and a client at a court's gate asks the host, which keeps the
queues (a Gate request, a Board reply).

When a host quits, it tells every player ("the host ended the game")
before it closes, so they log it as news, not the warning a lost
connection gets.

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
- Internet servers: the Network panel (F1) lists the public servers a
  directory knows for this game (docs/SERVER_HOSTING.md "Servers helping
  each other"): `KKE_DIRECTORIES=host:port` fills it at start, Refresh
  asks again, Join connects (with the Password field's password).

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

Several players on one screen adds: a client with two guests and a host
with one, where everyone sees everyone else (the extra bytes too), nobody
gets their own screen's players, a guest leaving and a connection going;
guests filling a server so a late joiner is turned away, a kicked guest's
owner told and still connected; guests refused under input replay; a
guest's impossible move corrected by its slot, not the first player's.
The fuzz test covers `Guest`, `GuestAck` and states with extra bytes.
The host ending the game tells its players so (`endedByServer()`), which
is not a lost connection.

Groups add: a snapshot's "still there" list round trips; 100 players on
one server all keep moving for everyone, fresh and never shown as
hidden, though a snapshot holds only some of them; two courts don't see
each other's players or ball, a spectator in the lobby sees the court it
watches and is a ghost, moving a player shows it on its new court and
leaving forgets its group.

`tests/test_break_graph.cpp`: a follower given the host's borders ends up
with the host's pieces; borders that don't exist can't be broken.

`tests/test_move_check.cpp`: walking, jumping and a 30 m fall pass; through
a wall is refused; a vault, a ledge climb and five seconds hanging pass;
rising or hovering in the open is flying; a crate holds you up, your own
capsule doesn't; the honest fall after a refusal passes.

`tests/test_rigid_world.cpp`: bodies switch between dynamic and kinematic
(kinematic ones hold still and push, dynamic again they fall).

`tests/test_input_replay.cpp` (input replay): the host plays each input
once and in order, waits for a late one and makes the ticks up, skips a
lost one after a few ticks, and a client with a fast clock or one that
goes quiet and bursts gets no extra distance; a press in a skipped input
still happens; a fair client is never corrected, a push only the server
knows about is rewound and replayed to the server's exact position, as
is another movement state in the same place; inputs round trip bit for
bit as the client stepped them; a Jolt character and its Locomotion put
back mid-run move exactly the same again through a vault; and over a
simulated network with lag, jitter and loss a Locomotion player vaults
the same on both sides, a client with a wall missing from its level is
stopped by the host's, and a cheat's fast clock buys nothing.

## Not yet

- Rollback for fighting games, lockstep for RTS (ACTION_PLAN.md); with
  input replay: player-vs-player collision, host-side respawns, and
  lag-compensated hit checks (rewinding the other players to what the
  shooter saw).
- Remembering a server's key per address (so a plain-address join can
  warn when it changes); a relay per region picked by ping.
- Breaking on a client before the host says so (predicted breaks): today
  a client's pane cracks half a round trip after the hit.
- FEMFX objects that aren't breakables (a thrown ball, soft bodies) aren't
  in snapshots: each machine simulates its own copy.

## Voice

Voice chat is built in (`kke::VoiceModule`, `KKE_ENABLE_VOICE`, on by
default), with nothing to sign up for:

- **Codec**: Opus (BSD), 48 kHz mono, 20 ms frames at 24 kbit/s by default,
  with in-band forward error correction: a single lost packet is rebuilt
  from the next one. `Voice` messages go on the unreliable channel.
- **Talking**: push-to-talk (the `voice.talk` action, **B** by default,
  rebindable like any action; kke_demo moves it to **P** and **LB**, since
  B is one of its Lua toys), voice activated (a gate that follows the
  room's noise floor, with a short hangover so word ends aren't cut), or
  open mic. "Hear myself" in the Voice panel tests the microphone.
- **Who hears it** is decided by the server (`net::VoiceRules`, set on
  `NetModule::voiceRules` by a host): **Nearby** (proximity: within
  `proximityRange`, 40 m by default, and heard from where the speaker
  stands), **Team** (a `team(playerId)` function; unset = everyone), or
  **Everyone** (heard like a radio, not from a place). The server stamps
  who spoke (a client can't pretend to be someone else), caps each
  speaker at 60 packets a second, and never gives out anyone's address:
  all voice goes through it. `kke_server`'s console has `mute` / `unmute`.
- **Hearing**: each speaker has a jitter buffer (60 ms cushion, reorders,
  drops what arrives after its turn, conceals up to 100 ms of loss) and a
  live `AudioStream` voice in the mixer (category Voice), so voices are
  placed, occluded and reverberated like any other sound. Mute or set
  the volume of anyone locally; `speaking(id)` lets a game draw a
  speaker icon.
- **Proximity chat you can see** (`kke::VoiceHudModule`, add it after
  the VoiceModule): the direction a voice comes from matters most, so it
  is heard *and* shown.
  - Heard: a Nearby voice plays from the speaker's mouth (their network
    position + `Settings::mouthHeight`, or the game's `speakerPosition`
    hook), panned, delayed between the ears in Binaural mode, muffled
    when behind you or a wall, and fading out to `hearingRange`. Hosting,
    the server's `proximityRange` follows `hearingRange`, so nobody out of
    range is even sent the voice. The pause menu's "Headphones: sharper
    direction" switches the mixer to Binaural (a delay and head shadow
    per ear instead of a plain pan).
  - Shown: a ring over each talker's head that pulses with their voice,
    with their name; someone out of view gets an arrow at the edge of the
    screen pointing their way (top = ahead, bottom = behind).
  - The list: players near you who talked lately, nearest first, one
    numbered slot each with a direction arrow, distance and level. A slot
    keeps its number while they talk and nothing moves while the menu is
    open. The pause menu (kke::DemoPanelModule) gets a "Voices nearby"
    section with **one mute per slot** ("1  Mute Sam  (8 m left)"), how to
    talk (hold a button, or when I speak), and markers on/off. Muting is
    local; a muted player's slot stays, greyed, so you can unmute them.
  - The data behind it is `VoiceModule::talkers()` (who, speaking, muted,
    distance, azimuth, elevation, level), for a game that draws its own.
  - Used by games/party and games/tennis (the sport center), both at a
    30 m hearing range.
- **Off**: `setEnabled(false)` turns voice chat off for this player:
  nothing is sent and nobody is heard until it's turned back on (Party's
  Voice row in the menu and its pause panel).
- **Cleaning the microphone** (`kke::voice::VoiceCleaner`), before
  coding, on by default, switches in the Voice panel:
  - *Echo cancellation* (SpeexDSP's MDF filter, BSD): without headphones
    the microphone hears the speakers, and the others would hear
    themselves back. The mixer hands voice chat everything it plays
    (`AudioMixer::setOutputTap`); an adaptive filter learns the path from
    speaker to microphone (up to 200 ms of delay and room echo) and
    subtracts it, then a residual echo suppressor takes out what's left.
    The panel shows how many dB it took out. Needs the audio output at
    48 kHz (the default).
  - *Noise suppression* (RNNoise 0.1.1, BSD; its model is in the source,
    416 KB): a small neural network trained on speech takes out fans,
    keyboards, traffic and hiss, which also keeps the voice gate from
    opening on noise.
  - In the tests: steady noise 29 dB quieter; a speaker echo 25 ms late
    with a reflection 21 dB quieter after 4 s of learning.
- `KKE_VOICE=off` keeps the microphone closed; `KKE_VOICE_NOISE=off` /
  `KKE_VOICE_ECHO=off` switch the cleaning off; `KKE_VOICE_TONE=440`
  sends a test tone instead of the microphone (not cleaned), to check a
  connection with nobody talking.

Checked end to end: two kke_demo instances, the host sending a tone, the
client (at 7 fps under a software GPU) played 1188 frames with nothing
skipped. Tests: test_voice.cpp (jitter buffer, voice gate, streamed mixer
voices, routing, flood cap, Opus round trip with a lost frame, noise
suppression, echo cancellation, the mixer's output tap). With a real
microphone and speakers it waits for a person: HW-018 in
docs/HARDWARE_TESTS.md.

