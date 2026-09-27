# Climb Race

A speed-climbing race up a generated mountain face. Every climber starts on
the ground in front of their own copy of the wall, side by side. The first
to mantle over the summit wins. It shows off the climbing blocks
(`kke::ClimbWall`, `kke::Climber`, `kke::ClimbBot`), two-bone IK on all four
limbs, Jolt collision (the rock, the ledges, and loose holds that break off
and fall), the start menu (`kke::LobbyModule`, [docs/LOBBY.md](../../docs/LOBBY.md))
and split screen.

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

Start (or the Start row) begins the race. A controller plugged in during a
race gets "press A to join" and is in from the next race. The choices are
saved in `climb_race_lobby.json` (or `.yml`) next to the game.

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
| Race again / new mountain | Start / Y | R / N |
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
| `KKE_CLIMB_SEED=<n>` | Which mountain (default 7). Same seed, same mountain. |
| `KKE_CLIMB_LOBBY=0` | No menu: straight into a race, you and `KKE_CLIMB_CPUS=<n>` CPU climbers (default 1). |
| `KKE_CLIMB_AUTOPILOT=1` | No menu, and you climb by yourself too (`kke::ClimbBot`). |
| `KKE_CLIMB_BOT_PAUSE=<s>` | Every CPU climber's breath between moves (default: from its difficulty). |
| `KKE_CLIMB_ROCKFALL=1` | No menu; every loose hold on your face comes off two seconds in; where they land is logged. |
| `KKE_CLIMB_QUIT=<s>` | Quit after that long, logging every climber's height every 5 s and, at the end, how far the hands sat from their holds. |
| `KKE_CLIMB_INTRO=0/1` | Force the how-to-play screen off or on (it is off in headless runs). |
| `KKE_CLIMB_CLOSEUP=<m>` | How far behind you the camera sits on the rock (default 4.6; 1.5 to check the grip). |
| `KKE_LOBBY_JOIN=<n>` | With `KKE_VIRTUAL_INPUT=pad,pad`: n controllers join the menu at startup (see docs/LOBBY.md). |

`KKE_SKIP_INTRO=1 KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_QUIT=110 ./climb_race`
races you (on autopilot) and a Hard rival to the top (seed 7: about a minute).

## Assets

No Synty packs. The climbers are the CC0 UAL mannequin
(`assets/animations/UAL1_Standard.fbx`, Quaternius); the rock, the holds
and the ledges are generated meshes. Without the FBX the climbers are blocks.
