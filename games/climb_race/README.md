# Climb Race

A speed-climbing race up a generated mountain face. Every climber starts on
the ground in front of their own copy of the wall, side by side. The first
to mantle over the summit wins. It shows off the climbing blocks
(`kke::ClimbWall`, `kke::Climber`, `kke::ClimbBot`), two-bone IK on all four
limbs, Jolt collision (the rock, the ledges, and loose holds that break off
and fall), the start menu (`kke::LobbyModule`, [docs/LOBBY.md](../../docs/LOBBY.md))
split screen, and online play (several players per screen, below).

## The start menu

The climbers line up in front of the mountain, each above their player's
card. Every controller that presses A joins (up to four players, split
screen: side by side for two, quarters for three and four, and with three
the fourth quarter watches the whole mountain). Each player picks a name
and a colour. Player 1 also sets the CPU climbers: how many (0 to 5) and
how good each one is:

| Difficulty | The CPU climber |
|---|---|
| Easy | slow, never lunges, rests early and long |
| Normal | steady, lunges when fresh |
| Hard | quick |
| Expert | quicker, and pushes on tired |

Player 1 also picks the **Mountain** (below). Start (or the Start row)
begins the race. A controller plugged in during a
race gets "press A to join" and is in from the next race. The choices are
saved in `climb_race_lobby.json` (or `.yml`) next to the game.

## Mountains

Six mountains, easiest first, each with its own sky and light. At first
only Pebble Hill (and Random) is open: finish a mountain, at any speed,
and the next one opens. Each mountain has three medal times; your best
time and best medal on each are saved in `climb_race_progress.json` next
to the game and shown in the menu ("Pebble Hill (best 0:24.10, silver)").
Medals are for players, not the CPU climbers; in split screen every
player's finish counts. `KKE_CLIMB_ALL=1` opens every mountain (testing).

| Mountain | Height | What it's about |
|---|---|---|
| Pebble Hill | 12 m | A gentle slab covered in big green holds. Start here. |
| Meadow Crag | 20 m | Two ledges to rest on and a first steep wall. |
| Granite Tower | 36 m | The classic face: slab, wall, then a big overhang. |
| Overhang Cove | 26 m | Steep all the way. Big holds, but your arms burn. |
| Crumble Peak | 30 m | Loose rock everywhere. Lunge and the hold may come with you. |
| Cloud Spire | 46 m | The long one. Small holds, four ledges, pace yourself. |

**Random** is a new mountain every time. After a race, Y / N goes to the
next mountain (on Random: another random one).

Each mountain is a small file in `mountains/` next to the game (YAML or
JSON). To make your own, copy one (say `pebble_hill.yaml` to
`my_hill.yaml`), change its `name:` and a few numbers, and start the game:
it's in the Mountain row. Every key is explained in
[Mountains.h](Mountains.h); a mistake (a typo in a key, a number out of
range) is named in the log and the rest still works. Online, the host's
mountain goes to everyone, even one only the host has.

## Party modes

Player 1's **Mode** row (the host's, online):

| Mode | Rules |
|---|---|
| Race | First over the summit wins. |
| Rockfall | Rocks tumble down every face at its climber. A hit costs a third of your stamina ("hit by a rock!"): rest when you can. The higher the leader, the more rocks come. |
| Elimination | Every 30 seconds the lowest climber still in is out: they let go and watch. The last one climbing, or the first to the top, wins. |
| Time trial | Race the ghost of the best run on this mountain: a pale climber doing exactly what that run did, through player 1's face. Beat it and yours is the ghost next time. Offline only (online it's a Race). |

Medals and best times count in every mode. Every best run on a tour
mountain is kept as its ghost, in `climb_race_ghosts/` next to the tour
file.

## Online

Race friends on another PC, or in a second window on the same PC. Each
machine can have up to four players on its own couch (split screen, like
Halo online), and everyone on every screen races everyone else.

**Host a race**

1. Start Climb Race. In player 1's card, go down to **Online** and press
   right twice, to **Host**. The line under the title says "Hosting on
   port 27960: 0 players online".
2. Wait for the others to join (a line pops up for each), then press
   Start. The race goes to every screen.

**Join a race**

1. Start Climb Race on the other PC (or a second window). If it's the same
   PC, pick a different Name and Colour, so you can tell yourselves apart.
2. Go down to **Online** and press right once, to **Join**. The **Game**
   row lists every Climb Race hosted on this network and on this PC, with
   its player count ("Pip (2/16)"); left / right picks one.
3. Go down to **Join Pip** and press A (Enter). The line under the title
   says "Online, connected as player 2: the host starts the race".

**More players on a screen:** on either machine, another controller
presses A to join the card row, as offline, before or after going
online. Each is its own climber for everyone.

- The host decides the race: the mountain (whatever the joiners picked), who climbs which face, the CPU
  climbers (the host's, which everyone sees), when it starts, race again
  and next mountain. A joiner's CPU climbers stay off; its Start does nothing.
- The countdown waits until every machine has built the race (a few
  seconds at most), so everyone starts together.
- Each machine climbs its own players and sends where they are and where
  their hands and feet are; the others see them on the same holds. A
  loose hold that breaks and each finish go to everyone.
- Someone joining during a race is in from the next one (the host presses race again).
  The host leaving ends the race for everyone ("the host ended the game");
  a joiner leaving takes only its climbers.
- Across PCs: allow UDP ports 27960 to 27975 in the firewall on the host.
  Not on the same network? Use a join code (docs/SERVER_HOSTING.md "Join
  codes").

From a terminal (no menu), two windows on one PC:

```sh
KKE_NET=host KKE_NET_NAME=Pip ./climb_race
KKE_NET=join:127.0.0.1 KKE_NET_NAME=Rook ./climb_race    # a second terminal
```

The joiner's card says it's online; the host presses Start. Everything
from docs/NETWORKING.md "Try it" works here too (`KKE_NET_LAG`,
`KKE_NET_LOSS`, join codes). F1 opens the network panel (round trip, loss,
who's on which screen).

## Controls

On foot it plays like any third-person game: left stick / WASD to walk,
right stick / mouse to look, A / Space to jump. Walk up to the rock and grab.
A how-to-play screen opens before the first race (A / Space starts); X / H
shows it again at any time. The hint line at the bottom always says what to
press next, with the button glyphs of the pad you are using.

| On the rock | Controller | Mouse and keyboard |
|---|---|---|
| Choose where a hand goes | left stick aims (the hold lights up cyan / magenta) | the crosshair picks a hold (red = out of reach), or WASD aims |
| Precise reach: slow, cheap | LB / RB | Q / E |
| Lunge: hold, then let go (the longer, the further) | LT / RT | left / right mouse button |
| Quick snatch: fast, costs more | hold a trigger + press its bumper | hold a mouse button + Q / E |
| Over an edge (both hands on it) | A | Space |
| Let go | B | C |
| How to play | X | H |
| Race again / next mountain | Start / Y | R / N |
| Back to the menu (players, CPU climbers) | Back | M |

Stamina is the only thing on the HUD that you manage. It drains faster on
one hand, on crimps and slopers, on overhangs, and with your feet off. It
comes back on two jugs (green) with your feet on, and fast when you stand
on a ledge. At zero you fall. Holds: green = jug, orange = crimp, blue =
sloper, the ledge lips are edges. Some holds are loose and break under a
lunge.

Both hands can share one hold (match on it), which is how you swap hands on
a big jug. The arms have a real length: the hands only go where the body can
hang between them, and if you pull one hand so far that the other arm can't
stay on, the lower hand cuts loose. Grab again quickly.

## Switches (headless / demos)

| Variable | Effect |
|---|---|
| `KKE_CLIMB_MOUNTAIN=<name>` | Which mountain: its file name (`crumble_peak`) or name, or `random`. Default: the menu's. |
| `KKE_CLIMB_MODE=<mode>` | `race`, `rockfall`, `elimination` or `time trial`, whatever the menu says. |
| `KKE_CLIMB_ALL=1` | Every mountain open, whatever the saved tour says (not saved). |
| `KKE_CLIMB_PROGRESS=<file>` | Where the tour is saved (default `climb_race_progress.json`). |
| `KKE_CLIMB_SEED=<n>` | Random, on that seed (default 7, Granite Tower's rock). Same seed, same mountain. |
| `KKE_CLIMB_LOBBY=0` | No menu: straight into a race, you and `KKE_CLIMB_CPUS=<n>` CPU climbers (default 1). |
| `KKE_CLIMB_AUTOPILOT=1` | No menu, and you climb by yourself too (`kke::ClimbBot`). |
| `KKE_CLIMB_BOT_PAUSE=<s>` | Every CPU climber's breath between moves (default: from its difficulty). |
| `KKE_CLIMB_ROCKFALL=1` | No menu; every loose hold on your face comes off two seconds in; where they land is logged. |
| `KKE_CLIMB_QUIT=<s>` | Quit after that long, logging every climber's height every 5 s and, at the end, how far the hands sat from their holds. |
| `KKE_CLIMB_INTRO=0/1` | Force the how-to-play screen off or on (it is off in headless runs). |
| `KKE_CLIMB_CLOSEUP=<m>` | How far behind you the camera sits on the rock (default 4.6; 1.5 to check the grip). |
| `KKE_NET=host` / `KKE_NET=join:ADDRESS` | Host, or join a host, at startup (see "Online"). `KKE_NET_NAME=<name>` is player 1's name. |
| `KKE_CLIMB_WAIT=<n>` | Hosting: start by itself once n players have joined online (for tests with no one at the keyboard). |
| `KKE_LOBBY_JOIN=<n>` | With `KKE_VIRTUAL_INPUT=pad,pad`: n controllers join the menu at startup (see docs/LOBBY.md). |

`KKE_SKIP_INTRO=1 KKE_CLIMB_MOUNTAIN=granite_tower KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_QUIT=110 ./climb_race`
races you (on autopilot) and a Hard rival to the top (about a minute).

## Assets

No Synty packs. The climbers are the CC0 UAL mannequin
(`assets/animations/UAL1_Standard.fbx`, Quaternius); the rock, the holds
and the ledges are generated meshes. Without the FBX the climbers are blocks.
