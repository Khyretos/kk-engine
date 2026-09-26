# Climb Race

A speed-climbing race up a generated mountain face. You start on the ground
in front of your wall and a rival starts in front of the identical wall
next to it. The first to mantle over the summit wins. It shows off the
climbing blocks (`kke::ClimbWall`, `kke::Climber`, `kke::ClimbBot`), two-bone
IK on all four limbs, Jolt collision (the rock, the ledges, and loose holds
that break off and fall), and split screen.

## Controls

On foot it plays like any third-person game: left stick / WASD to walk,
right stick / mouse to look, A / Space to jump. Walk up to the rock and grab.

| On the rock | Controller | Mouse and keyboard |
|---|---|---|
| Choose where a hand goes | left stick aims (the hold lights up cyan / magenta) | the crosshair picks a hold (red = out of reach), or WASD aims |
| Precise reach: slow, cheap | LB / RB | Q / E |
| Lunge: hold, then let go (the longer, the further) | LT / RT | left / right mouse button |
| Quick snatch: fast, costs more | hold a trigger + press its bumper | hold a mouse button + Q / E |
| Over an edge (both hands on it) | A | Space |
| Let go | B | C |
| Race again / new mountain | Start / Back | R / N |
| Split screen (second controller plays the right wall) | | F2 |

Stamina is the only thing on the HUD that you manage. It drains faster on
one hand, on crimps and slopers, on overhangs, and with your feet off. It
comes back on two jugs (green) with your feet on, and fast when you stand
on a ledge. At zero you fall. Holds: green = jug, orange = crimp, blue =
sloper, the ledge lips are edges. Some holds are loose and break under a
lunge.

## Switches (headless / demos)

| Variable | Effect |
|---|---|
| `KKE_CLIMB_SEED=<n>` | Which mountain (default 7). Same seed, same mountain. |
| `KKE_CLIMB_AUTOPILOT=1` | You climb by yourself too (`kke::ClimbBot`). |
| `KKE_CLIMB_SPLIT=1` | Start in split screen. |
| `KKE_CLIMB_BOT_PAUSE=<s>` | The rival's breath between moves (default 0.45). |
| `KKE_CLIMB_ROCKFALL=1` | Every loose hold on your face comes off two seconds in; where they land is logged. |
| `KKE_CLIMB_QUIT=<s>` | Quit after that long, logging both heights every 5 s. |

`KKE_SKIP_INTRO=1 KKE_CLIMB_AUTOPILOT=1 KKE_CLIMB_QUIT=110 ./climb_race`
races both bots to the top (seed 7: about 55 s).

## Assets

No Synty packs. The climbers are the CC0 UAL mannequin
(`assets/animations/UAL1_Standard.fbx`, Quaternius); the rock, the holds
and the ledges are generated meshes. Without the FBX the climbers are blocks.
