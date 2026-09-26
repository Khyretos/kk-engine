# Anti-cheat

How Kreative Kompas Engine keeps games fair without a kernel driver, a
system scan or anything else that reaches past the game itself.

## The short version

Most cheating comes in through a handful of doors. KKE closes each one
from inside the game and its server:

| The cheat | What stops it | Where |
|---|---|---|
| Opening the developer menu, console or debug switches in a released game | Shipping builds don't contain them | `KKE_SHIPPING`, `kke/DevTools.h` |
| A hacked client saying "I'm over there", "I hit him", "I have 9999 gold" | The server decides what matters and checks the rest | `kke/net/Authority.h`, [NETWORKING.md](NETWORKING.md) "Movement checks" |
| Wallhacks, radar hacks: seeing players through walls | The server doesn't send players nobody could see or hear yet | `kke/net/Visibility.h`, `NetModule::fogOfWar` |
| "I hit for a million", "I'm flying without a plane" | The game's own rules, stated as facts and checked on the server | `kke/GameRules.h` |
| Rapid-fire controllers, macro keys, scripted adapters, input bots | Their timing is too perfect for a human hand; it gets flagged | `kke/InputSanity.h` |
| Edited game files (a script with more damage, a level with a wall removed) | Data is sealed with the developer's signature; a changed file shows | `kke/PackSeal.h`, `kke_seal` |

None of this bans anyone by itself. Checks refuse what's impossible
(a move through a wall is undone) and flag what's suspicious (a macro is
reported). Punishment stays a human decision, or the game's.

## What we learned from the big systems

**Roblox** turned on *FilteringEnabled* for every game in 2018: changes a
client makes to the world no longer reach the server or other players.
Its guidance to developers is one sentence: *never trust the client*.
Every RemoteEvent is checked on the server (is that player allowed, is
the value in range, is it too often). On top sits Hyperion (from the
Byfron purchase), a user-mode anti-tamper layer on the Windows client:
obfuscation and integrity checks that make writing an exploit expensive,
without a kernel driver. The lesson: server authority does the heavy
lifting; client hardening only raises the price.

**Valve** runs VAC in user mode: it looks for known cheat programs and
modified game memory, and bans in delayed waves so cheat makers can't
tell which change got them caught. *Overwatch* (in CS:GO) sent reported
matches to experienced players to review as replays, and *VACnet*
trained a model on those verdicts to spot aimbots from aim data alone.
*Trust Factor* matches players by account behaviour rather than banning
the borderline ones. The lessons: detection from behaviour works without
touching the player's PC, humans review before bans, and slow feedback
beats instant feedback for the cheat makers.

**Server-side statistics** (EA's FairFight, the Minecraft server plugins
such as NoCheatPlus, Call of Duty's Ricochet server side) watch what
players do rather than what's on their PC: speed, reach, aim snaps,
impossible reaction times. Nothing installed, nothing to bypass, works
on every platform.

**Fog of war for data** (Valorant, CS2): the server doesn't send a
client the enemies it can't see, so a wallhack has nothing to show.

**Lockstep and input replay** (StarCraft, fighting games with rollback):
clients send only inputs; everyone simulates the same thing, so a
client can't send an outcome at all.

What we deliberately *don't* do: kernel drivers (Riot Vanguard, EasyAntiCheat's
kernel mode, BattlEye), scanning other processes, screenshots of the
desktop, hardware bans. They're invasive, they break on Linux, Steam Deck
and in virtual machines, and they are the part players hate.

## Shipping builds

```bash
cmake -B build-ship -G Ninja -DCMAKE_BUILD_TYPE=Release -DKKE_SHIPPING=ON
```

`KKE_SHIPPING=ON` compiles out:

- the ImGui developer panels (F1 in the sandbox and template; settings'
  debug overlay), which can't be turned back on;
- the Lua console line in the Scripts panel, and script hot reload
  (a shipping build runs the scripts it started with);
- the debug environment switches read through `kke::dev::env`:
  `KKE_VIRTUAL_INPUT` (virtual gamepads, which could inject input),
  `KKE_SKIP_INTRO`, `KKE_INTRO_AT`, `KKE_HIDE_UI`, `KKE_USE_EVERYTHING`,
  `KKE_SCRIPTS_DIR`.

Compiled out rather than switched off, because a switch is a byte in a
file or in memory, and flipping bytes is what cheat tools do. Game code
uses the same pieces:

```cpp
#include "kke/DevTools.h"

if constexpr (kke::dev::kEnabled) { /* god mode key, level skip, ... */ }
if (kke::dev::flag("MYGAME_START_AT_BOSS")) { ... } // nullptr in shipping
```

Settings players may legitimately change (VRAM budget, physics threads)
keep using `std::getenv`. The engine's demo downloads (docs/RELEASES.md)
stay developer builds on purpose: they are there to show the tools.

## Server authority

The first rule: the server never takes a client's word for anything that
matters. How strict that is depends on the game, so it is a policy per
player **role**:

| Authority | Meaning | Good for |
|---|---|---|
| `Client` | The client decides; the server relays it | Things nobody gains by faking (emotes, a builder role in co-op) |
| `Checked` | The client proposes; the server checks and may refuse | Movement (speed limits + `checkMove`, i.e. no walls, no flying); events through `checkEvent` |
| `Server` | Only the server decides; the client's messages for it are dropped | Spectators; damage, score and loot in competitive games |

```cpp
server.authority = kke::net::AuthorityPolicy::competitive();
server.authority.setRole("builder", { Authority::Checked, Authority::Client });
server.authority.assign(playerId, "spectator");

// Checked events: the game says what's allowed.
server.checkEvent = [&](uint8_t id, const GameEventMsg& e) {
    if (e.kind == kShoot) return weapons.canFire(id, now); // cooldown, ammo, alive
    return e.kind != kDamage;                               // damage only from the server
};
```

`coop()` trusts players' events and checks their moves; `competitive()`
checks both; both have a `spectator` role that can do neither. The
default (no preset) is what the engine always did, so existing games
behave the same. Refusals are counted (`refusedMoves()`,
`refusedEvents()`), a refused move sends the player back to their last
legitimate position, and roles are forgotten when a player leaves.

The rules of thumb for a competitive game built on this:

1. The server applies every event: a client sends "I fired", the server
   works out whether it hit. A client never sends "I hit".
2. Every number from a client is range-checked on the server (is that
   item theirs, is that spot reachable, is it too often).
3. Send clients only what they may know (see the fog-of-war issue).

## Fog of war

The server knows everything; a client only needs to know what its player
could see or hear. So each snapshot leaves out the players a client
couldn't perceive yet, and a wallhack has nothing to draw (Valorant and
CS2 do the same).

```cpp
net->fogOfWar = true;                        // NetModule, host side
net->visibilitySettings.hearingRadius = 20;  // heard through walls within 20 m
```

`kke::net::Visibility` decides, for every pair of players, from rays
through the host's static level (crates and players are no cover). It
costs players no input delay: the server decides while it builds
snapshots, which it does anyway. The costs it does have, and how they're
kept small:

- **Server CPU.** Pairs further apart than `maxDistance` are never sent
  and cast no rays; pairs within `hearingRadius` are always sent (you'd
  hear their footsteps); the rest are re-traced every 100 ms, not every
  snapshot, with at most 24 rays a pair and usually one.
- **Pop-in at corners.** Rays go from where the viewer's eye will be to
  where the subject will be `lead` seconds from now (latency plus a
  frame), and to the edges of a padded body, so someone running round a
  corner is sent just before they appear, never after.
- **Flicker.** A player stays sent for `keepVisible` (0.5 s) after
  leaving sight.

A client drops a player the moment the server stops sending it (it isn't
left frozen where it was last seen) and starts it fresh when it's back.
Grenades, projectiles and sounds by hearing range are next (#49).

## Game rules

The developer knows what's impossible in their game: nobody flies
without a plane, a plane doesn't fly without fuel, no hit beats the best
buffed attack. `kke::GameRules` lets them say so as plain facts, and the
server checks every claim against them:

```cpp
kke::GameRules rules;
rules.rule("flying needs a plane with fuel", { "flying == 1" }, { "in_plane == 1", "fuel > 0" });
rules.rule("no hit above the best buffed attack", {}, { "damage <= max_damage" });
rules.limit("gold in range", "gold", 0, 1'000'000);

std::vector<kke::RuleViolation> broken;
if (!rules.check({ { "flying", 1 }, { "in_plane", 1 }, { "fuel", 0 } }, &broken))
    log("{}: {}", broken[0].rule, broken[0].detail); // "needs fuel > 0 (fuel = 0)"
```

A fact is a number the game fills in for the moment it checks (missing
facts are 0, true and false are 1 and 0, and NaN never passes). A
condition compares a fact with a number or with another fact, so rare
multipliers work: the game puts the real ceiling for that hit in
`max_damage`. Rules are data (`toJson` / `loadJson`), so the Lua,
node-graph and drag-and-drop tiers can make the same rules (#59). A
broken rule is evidence, counted per rule; what happens next (refuse the
event, correct the player, flag them for review) is the game's call.

## Rigged controllers and macros

A rapid-fire mod or a Cronus-style adapter doesn't need anything
installed that we could look for: it arrives as an ordinary controller.
What gives it away is timing. People are noisy: a finger mashing a button
drifts 10 to 30 ms between presses; a thumb holding a stick half-way
jitters. Machines repeat themselves to the millisecond.

`kke::InputSanity` watches every local key, mouse button, pad button and
stick (InputModule feeds it with the operating system's own event
timestamps; frame times would make every human look regular) and flags:

| Finding | What it means | Default threshold |
|---|---|---|
| Impossible rate | One button faster than people can press, sustained | 20+ presses/s over 24 presses |
| Turbo | A fast button with almost no timing jitter | 8+ presses/s, under 2 ms of jitter |
| Macro | The same 4+ step sequence replayed 3 times, every gap within 3 ms | |
| Steady axis | A stick held part-way with no noise at all | 2 s |
| Inhuman reaction | Most reactions faster than people react | 8 of the last 10 under 100 ms |
| Bad value | Out-of-range or non-finite values, time going backwards | |

Findings are logged, counted and summed into a `suspicion()` from 0 to 1.
Games read them from `InputModule::sanity()` (or `sanity().onFinding`)
to send to their server, show to moderators, or weigh with the server's
own checks. They never ban on their own: accessibility controllers,
rhythm-game experts and odd drivers can trip one. Reaction times need the
game's help: call `sanity().reaction(seconds, now)` when a player answers
a cue (an enemy appearing, a light turning green).

Honest limits: a macro with random jitter added looks human to this, and
a client that is itself hacked can simply not report findings. That's
why the same checks should also run on the server over the inputs it
receives (#51), and why authority above comes first.

## Sealed game data

```bash
kke_seal keygen ~/.kke/mygame.key          # once; prints the public key
kke_seal sign games/mygame/data ~/.kke/mygame.key   # at every release (CI: KKE_SEAL_KEY secret)
kke_seal verify games/mygame/data <public-key-hex>
kke_seal digest games/mygame/data          # one hash for the whole data set
```

A seal (`kke.seal`, JSON) lists every file in the folder with its
BLAKE2b-256 hash, signed with the developer's EdDSA key (Monocypher,
BSD/CC0). The game has the public key compiled in and checks with
`kke::seal::verify(folder, publicKey)`, which reports modified, missing
and added files and whether the signature holds. Rewriting the seal to
match edited files doesn't work without the private key, which never
leaves the developer's machine or CI.

What it is and isn't: on a player's own PC, someone determined can patch
the check out of the executable. Its weight is:

1. **Joining a server**: a client sends the data digest; the server
   turns away clients whose data differs from its own (#52).
2. **Broken downloads and stale mods** show up as a clear message instead
   of a strange crash.
3. **Marketplace content**: a creator's seal says the pack really is
   theirs and unchanged.

## What's next

Tracked in #48, with one issue each (label `area: anti-cheat`):

- #49 Fog of war for grenades, projectiles and sounds, and in kke_server (players done).
- #59 Game rules in Lua, the node graph and drag-and-drop, checked on the server.
- #58 Kreative DRM's activation service (see [DRM.md](DRM.md)).
- #50 Server-authoritative movement from inputs for competitive games (with #28).
- #51 Input sanity on the server, and clients reporting findings.
- #52 Data digest in the join handshake; startup seal check in shipping builds.
- #53 Reports, replays and a review queue (the Overwatch idea), with
  delayed, human-confirmed bans on self-hosted servers.
- #54 Aim analysis on the server: snaps, tracking and hit rates.
- #55 Authority, rate limits and range checks for Lua `net` calls; the
  remaining debug switches moved to `kke::dev::env`.
