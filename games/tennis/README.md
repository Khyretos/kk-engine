# Tennis

Singles and doubles on a hard court, in a sport center of ten courts. You
play the CPU, or up to four people play on one screen, split in two or
four, or online with people on other PCs (and their friends on the same
screen as them). The ball is a FEMFX soft body: it flattens on the strings and on the
court and wobbles back. You press a shot button early, hold it to hit
harder, and aim with the stick; when the ball reaches you the swing
happens, and how well you timed it and how well you stood decides where
it goes. The score is real tennis: deuce and advantage, tiebreaks, lets,
double faults, changing ends.

The demo teaches how to put a FEMFX object into a game that needs exact
numbers (the ball's flight is the game's own maths; FEMFX gives it its
body), a CPU player whose footwork is pure logic you can unit-test, rules
as a small state machine the whole game listens to, two-bone IK swings
with no animation clips, the start menu with local players and CPU
players, and online play where every machine flies the ball from the same
hit. Start here for a sports game, any game with a ball, or any game
where a soft body has to go exactly where the rules say.

![The TV view of a CPU match: two players, the net, the fence, the next court](../../website/static/media/tennis.webp)

Or pick **Sport center** in the menu: walk about all ten courts, step up
to a free court's gate to play (someone else at the gate is your
opponent, or play the CPU), watch the CPU players' matches with a crowd
of up to 100 people who sit, stand and cheer, and see who has won most
on the board. Still to come: the sport center online (today online is
one match).

## Run it

The executable is `tennis` (`kke_add_game(tennis ...)` in
[CMakeLists.txt](CMakeLists.txt)). The root `CMakeLists.txt` adds it when
`KKE_ENABLE_JOLT`, `KKE_ENABLE_FEMFX` and `KKE_ENABLE_NET` are all on: the
`everything` presets. FEMFX has no ARM port yet, so there is no Android
build.

```bash
cmake --build build --target tennis
cd build/bin
./tennis
KKE_SKIP_INTRO=1 ./tennis                            # skip the logo intro
KKE_TENNIS_BOTS=1 KKE_TENNIS_QUIT=120 ./tennis       # watch two CPU players, with a log
KKE_TENNIS_BALLTEST=1 ./tennis                       # the ball's test shots, logged
KKE_TENNIS_CENTER=1 KKE_TENNIS_BOTS=1 KKE_TENNIS_CROWD=100 ./tennis  # the whole sport center, CPU players only
KKE_NET=host KKE_NET_NAME=Pip ./tennis                # host an online match...
KKE_NET=join:127.0.0.1 KKE_NET_NAME=Rook ./tennis     # ...and join it from a second terminal
```

| Variable | Effect |
|---|---|
| `KKE_TENNIS_BOTS=1` | CPU players only, no menu (a demo that plays itself) |
| `KKE_TENNIS_DOUBLES=1` | four players, two a side |
| `KKE_TENNIS_LEVEL=0..3` | the CPU players' level when there is no menu: Easy, Normal (default), Hard, Expert |
| `KKE_TENNIS_GAMES=<n>` | games to win the set, over the menu's choice |
| `KKE_TENNIS_LOBBY=0` | skip the menu: you against one CPU player |
| `KKE_TENNIS_QUIT=<s>` | quit after that many seconds of play, logging the score every 10 s and every call |
| `KKE_TENNIS_SEED=<n>` | the CPU players' dice (default 1) |
| `KKE_NET=host` / `KKE_NET=join:ADDRESS` | host, or join a host, at startup ([docs/NETWORKING.md](../../docs/NETWORKING.md)); `KKE_NET_NAME=<name>` is player 1's name |
| `KKE_TENNIS_CENTER=1` | the sport center instead of one match (the menu's Play row) |
| `KKE_TENNIS_CROWD=<n>` | how many CPU people walk about the sport center and watch (the menu's Crowd row: 0, 20, 40 or 100) |
| `KKE_TENNIS_EMPTY=1` | the sport center's free courts stay empty (no CPU matches) |
| `KKE_TENNIS_WAIT=<n>` | hosting: start the match by itself once `n` people have joined (tests) |
| `KKE_TENNIS_AUTOPLAY=1` | the players at this screen are played by the CPU (tests: two copies that play each other online; in the sport center, walk to a free gate and play the CPU) |
| `KKE_TENNIS_BALLTEST=1` | no match: seven scripted shots (a drop, a groundstroke, into the net, a serve, both fences, the roof), every contact logged with how far the FEMFX body is from the flight |
| `KKE_ANIMATIONS_DIR` | where to look for `UAL1_Standard.fbx` and `UAL2.fbx` |
| `KKE_ASSETS_DIR` | also searched for `Universal Animation Library 2/Unity/UAL2.fbx` |

Rebindings are saved to `tennis_input.json` and the menu's choices to
`tennis_lobby.json` (the names passed in [main.cpp](main.cpp)).

## Controls

All bindings are made in `TennisModule::defineControls`
([TennisModule.cpp](TennisModule.cpp)), the same for players 1 to 4, and
are rebindable actions in the "Shots" and "Game" groups.

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Run (`move`) | WASD | left stick |
| Topspin, the all-round shot; also the toss and the serve (`tennis.topspin`) | Space / left mouse | A (south) |
| Flat: hard and fast (`tennis.flat`) | J | X (west) |
| Slice: low and slow (`tennis.slice`) | K / right mouse | B (east) |
| Lob: over their head (`tennis.lob`) | L | Y (north) |
| Back to the menu (`tennis.menu`; online, a client leaves the game; in the sport center, leave the court, the other side winning) | Esc | Back |
| Developer panels (`panels`) | F1 | none |

- **A shot.** Press the button while the ball is coming: the shot is
  armed for up to 1.3 s and swings when the ball reaches your hitting
  spot. Hold it to hit harder (you run slower while you hold). Pressed
  0.08 to 0.7 s before the ball arrives is perfect timing; too late is a
  swing at the ball already past, too early is a swing at nothing.
- **Aim** with the stick (or WASD) as you hit: left and right is across
  the court, forward is deeper, back is shorter.
- **The serve.** Stand still: the first press tosses the ball, the second
  hits it; hit it near the top of the toss (about 2.75 m) for the best
  serve. Let it drop and you catch it and toss again.
- The HUD's hints show the right glyph for the device each player last
  touched (`InputModule::promptText`).
- The engine's character actions this game doesn't use (jump, sprint,
  fire, look and so on, and `voice.talk`) have their bindings cleared.

## How it plays

- **The menu.** Controllers press A to join, up to four players, each
  with a name and a colour. Player 1 picks one match or the sport center
  (with its crowd and whether CPU players take the free courts), the CPU players (0 to 3, Easy
  to Expert), the match length (a short set to 4 games, a set to 6, best
  of three short sets, or quick: to 2), the teams (across the net, or all
  the people at this screen on one side against the CPU) and the helping
  hand. Two players make singles; three or four make doubles (filled up
  with CPU players).
- **The sport center** (the menu's Play row). Everyone at this screen
  walks the promenade between the courts (split screen for two or more,
  the camera behind each). A court's gate is at its end by the
  promenade: stand there and press Topspin to play on it. A second person
  at the gate is your opponent (a few seconds' countdown lets a third and
  fourth join for doubles); Lob plays the CPU now, Slice leaves. CPU
  players take any court nobody wants and hand it over at the end of a
  point when someone waits (the two middle courts stay free while anyone
  here plays). The crowd (the Crowd row) strolls, picks a court with a
  match on, sits on a bench or stands beside it, and cheers or groans at
  every point. After a match its players walk off at the gate, and every
  match won goes on the board top right, "Matches won here".
- **Scoring.** Real tennis ([Rules.cpp](Rules.cpp)): 15, 30, 40, game;
  deuce and advantage (or no-ad); a tiebreak to 7, two clear, at 6 games
  all (or 4 all, or 2 all); the serve changes every game and every two
  points of a tiebreak; ends change after every odd game and every 6
  tiebreak points; in doubles the partners take turns serving.
- **The umpire.** A serve must land in the box diagonally across; out is
  a fault, two are a double fault; one that clips the net and lands in is
  a let. The return waits for the serve's bounce. In a rally the ball may
  be taken out of the air or after one bounce, never after two, and
  nobody hits it twice. Out, the fence or the roof before it bounces: the
  hitter loses the point. A ball that stops dead counts as bounced twice
  where it lies.
- **The helping hand** (on by default): when you let go of the stick
  while the ball is coming to you, you walk to meet it, your reach is
  0.45 m longer and a hit is never worse than fairly good. Off, it's all
  you.
- **The CPU players** read your shot after a reaction time, run to where
  the ball can be met, pick a shot (lobs when you're at the net, drop
  shots when brave, slices, flats, topspin) and aim away from you. Their
  levels differ in speed (3.6 to 6.2 m/s), reaction time (0.45 to 0.1 s),
  how straight they aim, how hard they hit and how close to the lines
  they go ([Bot.cpp](Bot.cpp) `BotSkill::forLevel`).

## How it works

### Startup and the frame

[main.cpp](main.cpp) sets the mood `clear_day` and adds the modules in
this order: Settings, Input, RigidBody (Jolt), Physics (FEMFX, metre
scale, no demo objects, its own ground drawing off), Model, Ui, Audio,
Lobby, Net, DemoPanel, Tennis, Stats. The order matters once: fixed
updates run in add order, so FEMFX has stepped the ball before
`TennisModule::fixedUpdate` reads it.

Each fixed step (`TennisModule::fixedUpdate`, `stepMatch` in
[Play.cpp](Play.cpp)): the ball moves and reports what it touched, the
umpire hears it, every player thinks (CPU) or reads the controls (people),
runs and maybe hits. Each frame (`update`): the menu, the controls, the
bodies' animation and IK, the cameras, the HUD.

### The ball: our flight, FEMFX's body

[Ball.cpp](Ball.cpp). The first version let FEMFX fly the ball, and the
ball test measured why that can't work for tennis: FEMFX's implicit
solver is made for soft, heavy things, and on a 57 g ball it lost most of
gravity (a 2 m drop reached the court at 1.2 m/s instead of 6.2) and
nearly all of the bounce, and a 30 m/s shot passed through a thin wall
between two steps. So the ball is two parts:

- **The flight** is worked out in `Ball::step` each fixed step, in small
  steps (2 cm at most, so a 60 m/s serve can't jump the net tape): gravity
  plus the spin's pull, air drag, the court's bounce, the net (the tape
  lets it roll over slowly; the mesh stops it and drops it), the fence
  and the invisible roof (they take most of its speed). Every rule, the
  CPU and, later, the network read this one.
- **The body** is a FEMFX tet-mesh sphere (`buildSphere(4, 0.045)`,
  rubber of 400 kg/m³ and a stiffness of 1e4) that `steerBody` pushes
  along the flight each step: every vertex gets the same push, so only
  the whole ball moves and the squash and the wobble stay FEMFX's. A hit
  (`Ball::strike`) gives every vertex the new velocity plus a turn about
  the centre (spin) and a squeeze along the hit that grows with the
  distance from the centre, so the strings flatten it and FEMFX springs
  it back. The body stays within about 5 cm of the flight (10 cm at a
  hard hit), and FEMFX's own ground plane and the court's boxes squash it
  where it lands.

The engine calls it uses are new and general
([PhysicsModule.h](../../engine/include/kke/modules/PhysicsModule.h)
"Steering a whole object"): `objectMotion`, `changeVertexVelocities`,
`translateObject`, `resetObject`.

Every court also gives FEMFX boxes for its fence walls (2 m thick),
its roof at 14 m and its net (`Ball::courtBoxes`, one
`setExternalBoxes` call for all ten courts): the flight never leaves the
court, and the body can't either.

### The maths everyone agrees on

[Shot.cpp](Shot.cpp) is pure maths, tested in
[tests/test_tennis.cpp](../../tests/test_tennis.cpp):

- `Flight`: position and velocity at any time, in closed form, with
  gravity, a spin's extra pull and linear air drag (0.4 per second: a
  25 m/s drive loses about a third by the far baseline). When it comes
  down to a height, when it crosses the net.
- `bounce`: 75% of the vertical speed back (the ITF's 2.54 m drop comes
  back to 1.35-1.47 m), 62% of the speed along the court (a medium-paced
  hard court); 40% of the spin's pull is left after.
- `planShot`: the launch velocity that lands a ball exactly on a target
  at about a speed, raising the arc until it clears the net by a margin.
- `meetPoint`, `meetOnArc`: where a player takes a ball after its bounce,
  waist high on the way down, or earlier when it would carry them too
  deep.

### Hitting

`tryHit` and `hitBall` in [Play.cpp](Play.cpp). An armed shot waits until
the ball is within reach (0.2 to 1.5 m) and reaches the hitting spot a
little in front, or starts going away. Quality is spacing (the ball an arm
and a racket to the side, between knee and shoulder, beside or just in
front) times timing (how long before the ball the button went down; a CPU
player's by its level). The target is the aim plus a wobble that grows as
quality drops; hitting early pulls it across the body, late pushes it
wide. `planShot` gives the velocity; the spin kind gives the pull and the
spin; the harder the hit, the more the strings squash the ball.

### The CPU player

[Bot.cpp](Bot.cpp) has no engine in it: it is told where the ball is
going (a `Flight`), where it stands, where the opponent stands and what
the rules allow, and answers where to run, how urgently, and whether to
arm a shot, which, aimed where. The test drives a Hard bot at its own
speed toward what it says against 60 random shots and checks it reaches
at least 90%.

### Bodies and swings

[Body.cpp](Body.cpp). The players are the UAL mannequin with its idle,
walk, jog and sprint clips blended by speed, UAL2's side steps when that
pack is there, and sitting, cheering and groaning. There are no swing
clips: every swing is two-bone IK on the right arm along a path (back,
contact, follow-through) timed so the racket meets the ball; the left arm
tosses the serve. The racket is a mesh built in code (grip, shaft, oval
head), held in the right hand.

### The sport center

[Court.cpp](Court.cpp), [Scene.cpp](Scene.cpp). Ten courts to ITF size in
two rows of five, one scene, with a green surround, lines, net and posts,
fences with windscreens (see-through: `drawTranslucent`), an umpire's
chair, floodlights, benches along the promenade between the rows, and
spectators' seats beside every court and a standing row behind the
promenade end. The fences and floodlight poles are Jolt walls, so people
can't walk onto another court or through a pole.

[Center.cpp](Center.cpp) runs it. Everyone out of a match is a `Walker`
(a Jolt character and a `Body`): a person at this screen, moved by the
stick relative to their camera, or one of the crowd. Each court has a
`Gate` (who waits, the countdown); `startCourt` turns the people waiting
into a match (hiding their walkers and removing their capsules),
`endCenterMatch` counts the win and puts them back at the gate. Matches
come and go, so player slots are reused (`Player::alive`).

The crowd is three states: stroll to a spot on the promenade, go to a
free seat by a court with a match on (seats beside a court are in the
gap between two fences, so they walk to the gap's mouth first and back
out the same way), and watch for 25 to 75 s, facing the court. A point
on the court they watch sets them cheering (three in four) or groaning.
Each seat is held by one walker (`m_seatTaken`), so nobody stands in
anybody. The walkers' camera pulls in in front of a pole or a fence
(a Jolt raycast from the head), so it never sits inside one.

Cost, 100 in the crowd and ten matches: each person is a skinned
mannequin (skinned on the CPU by `ModelModule`) and a Jolt character;
the ten balls are ten FEMFX bodies. On the software renderer the CI uses
it runs at 3 to 4 frames a second, all of it waiting on the renderer;
a real GPU is the test that counts.

### Online

Player 1 sets **Online** in the menu to **Host** or **Join** (the games
found on the network and on this PC are listed; pick one and press Join).
Everyone at each screen plays: a second person at a screen is a
`NetModule` local player, like the menu's seats. [Net.cpp](Net.cpp) has
the game's side, [NetTennis.h](NetTennis.h) the messages (event kinds
from `0x5400`).

- **Who plays.** The host's Start builds the match: its own seats, then
  everyone online, then the menu's CPU players, then spare CPU players if
  a side is short (the host keeps three ready as network players, so
  everyone sees them move). More than four people: the first four play.
  Its **Setup** says who plays where; each client builds the same match,
  its own seats played from its own controllers.
- **Each machine runs its own players** and sends where they are, where
  they face and how far into which swing (`toState`, a `NetPlayerState`
  with the swing's contact packed into its extra bytes). Everyone else's
  players are kinematic capsules put where their machine says.
- **The host is the umpire.** Its **Serve** starts each point (with a
  number, so a hit that arrives after the call is dropped) and its
  **Point** ends it with the result and the call; every machine applies
  that result to the same score. A client never calls a point from its
  own ball.
- **A hit is all the ball needs.** The hitter's machine plans the shot and
  sends **Hit** (where, the velocity, spin, pull); everyone, the hitter
  too, flies the ball from the numbers as they come off the wire, so the
  flights match to the bit (a test checks the bytes). The host checks a
  client's hit could happen (their turn, this point, this shot) before it
  passes it on. A serve's **Toss** goes the same way.
- **Drift is put right.** Five times a second the host sends its ball;
  a client more than 25 cm or 1 m/s off takes it, unless a hit of its own
  is on the way to the host.
- **Someone leaves:** the host ends the match for everyone (**End**) and
  all go back to the menu; a client's menu button leaves the game.

Tested with two copies on one PC, both played by the CPU
(`KKE_TENNIS_AUTOPLAY=1`, the commands in "Run it" plus
`KKE_TENNIS_WAIT=1 KKE_TENNIS_LOBBY=0 KKE_TENNIS_QUIT=70`): both logs make
the same calls after the same number of shots, rallies of 6 to 15 shots.

### Cameras and the HUD

With people playing, each gets a camera behind their own baseline, high
enough to see over the net, that swings round when ends change; two
people split the screen, three or four split it in four. With only CPU
players (and in the menu) it's the TV view: high behind one end, both
baselines in sight. The HUD is RmlUi ([ui/tennis_hud.rml](ui/tennis_hud.rml),
[Hud.cpp](Hud.cpp)): the scoreboard with who serves, the umpire's call,
"Second serve" and "Tiebreak", and the controls for whoever is at this
screen.

## Design decisions

- **The flight is ours, the body is FEMFX's.** Measured above: FEMFX on
  its own can't give a tennis ball's numbers, and the rules, the CPU and
  a network all need one flight they agree on to the millimetre.
- **Linear drag.** Real drag grows with the square of the speed; linear
  drag is close over a rally's speeds and keeps every sum closed-form, so
  `planShot` lands exactly where it aims.
- **Press early, swing on arrival.** Timing a button to the exact frame
  is not fun on a TV; arming the shot and grading how early you pressed
  keeps timing a skill without being a lottery.
- **Procedural swings.** Kees was asked which swing animations to use;
  until there is an answer the swings are IK, which needs no clips and
  always reaches the ball.
- **Hits, not a streamed ball, online.** Sending the hit and letting every
  machine fly the same maths costs one small message per shot and looks
  smooth at any latency; the host's ball a few times a second only mops
  up. Streaming the ball's position would lag it and jitter the squash.
- **No Synty art yet.** The Shopping Mall pack Kees mentioned is not on
  the share; the courts are built from boxes.

## Tuning

| What | Where | Value |
|---|---|---|
| Bounce: vertical kept, along kept | `BounceModel` in [Shot.h](Shot.h) | 0.75, 0.62 |
| Air drag | `kDrag` in [Shot.h](Shot.h) | 0.4 /s |
| Spin pull (topspin, slice, lob, drop, serve) | `spinPull` in [Shot.cpp](Shot.cpp) | 6, -2.5, 1, -1.5, 2 m/s² |
| Shot speeds | `speedFor` in [Play.cpp](Play.cpp) | flat 25-35, topspin 20-28, slice 15-20, lob 9-10.5, serve 30-46 m/s |
| Run speed, while charging | `kRunSpeed`, `kChargeSpeed` in [Play.cpp](Play.cpp) | 5.8, 3.6 m/s |
| Reach, helping hand's extra | `kReachMin`, `kReachMax`, `kReachHelp` | 0.2, 1.5, 0.45 m |
| How long an armed shot waits | `kArmedFor` | 1.3 s |
| CPU levels | `BotSkill::forLevel` in [Bot.cpp](Bot.cpp) | speed, reaction, aim, power, risk |
| A CPU's aim wandering in a long rally | `tired` in `hitBall` ([Play.cpp](Play.cpp)) | +10 cm a shot after the 3rd |
| Sport center: walking, the crowd's stroll | `kWalkSpeed`, `kCrowdSpeed` in [Center.cpp](Center.cpp) | 4.2, 1.5 m/s |
| Sport center: gate reach, countdown, a court's rest before CPU players take it | `kGateReach`, `kGateCountdown`, `kCourtRest` | 3 m, 4 s, 6 s |
| The ball's body | `ballMaterial`, `buildSphere(4, ...)` in [Ball.cpp](Ball.cpp) | 400 kg/m³, 1e4 stiffness |
| How hard the body is steered | `steerBody` in [Ball.cpp](Ball.cpp) | 25 /s blend, 20 /s pull onto the path |

## Engine features it uses

- FEMFX soft bodies and the steering calls (`PhysicsModule`, see the
  [physics demo](../physics_demo/README.md))
- Jolt characters and walls (`RigidBodyModule`)
- The start menu (`LobbyModule`, [docs/LOBBY.md](../../docs/LOBBY.md))
- Input with four players, rebinding and button glyphs
  ([docs/INPUT.md](../../docs/INPUT.md))
- Skinned models, the animator and two-bone IK (`ModelModule`,
  [docs/PROCEDURAL_ANIMATION.md](../../docs/PROCEDURAL_ANIMATION.md))
- Split screen (`kke::splitScreen`)
- RmlUi for the HUD, the demo panel (`DemoPanelModule`,
  [docs/DEMO_PANEL.md](../../docs/DEMO_PANEL.md))
- Impact sounds (`AudioModule::playImpact`)
- Moods ([docs/MOODS.md](../../docs/MOODS.md))
- Online play with local players (`NetModule`: `sendEvent`, `relayEvent`,
  `addLocalPlayer`, `setLocalPlayer`, LAN search;
  [docs/NETWORKING.md](../../docs/NETWORKING.md))

## Assets

- The mannequin and its clips: `assets/animations/UAL1_Standard.fbx`
  (Quaternius' Universal Animation Library, CC0), in the repository.
- Side steps: `UAL2.fbx` (Universal Animation Library 2, CC0) from
  `assets/animations/` or `KKE_ASSETS_DIR`. Without it the log says so
  once at info level and players turn to run sideways.
- The ball's felt and seam: [textures/tennis_ball.png](textures/tennis_ball.png),
  made by [textures/make_ball_texture.py](textures/make_ball_texture.py).
- Everything else (the courts, the racket) is built in code. No Synty
  pack is used yet.

## Make a game like this

1. **Build with FEMFX** (`cmake --workflow --preset everything`) and copy
   the folder: `cp -r games/tennis games/my_sport`; rename the target,
   the namespace and the input and menu files.
2. **Keep the rules pure.** A `Rules.cpp` with no engine in it that hears
   "hit", "bounce", "net", "out" and answers who won the point is easy to
   test and easy to change (badminton, squash, volleyball).
3. **Own the flight, give FEMFX the body.** For any ball that must go
   where the rules say, fly it with your own maths (`Flight`, or your own)
   and steer the FEMFX body along with `changeVertexVelocities`; kick it
   with `strike`-like velocity fields for the squash.
4. **Write the CPU as pure logic** that gets a view and returns a
   decision, then test its footwork without a window.
5. **Online, send what happened, not where things are.** A hit, a toss,
   the umpire's call: small events every machine applies the same way
   ([Net.cpp](Net.cpp)); only people's poses stream.
6. **Read next:** [the physics demo](../physics_demo/README.md),
   [Climb Race](../climb_race/README.md) for more online play with local
   guests, [docs/LOBBY.md](../../docs/LOBBY.md).

## Files

| File | What's in it |
|---|---|
| [main.cpp](main.cpp) | The modules and the mood |
| [TennisModule.h](TennisModule.h), [TennisModule.cpp](TennisModule.cpp) | Players and matches, startup, controls, the frame, the ball test |
| [Play.cpp](Play.cpp) | A match's step: points, the umpire, people's controls, CPU players, serving, hitting |
| [Scene.cpp](Scene.cpp) | The sport center's meshes and walls, bodies, cameras |
| [Hud.cpp](Hud.cpp), [ui/tennis_hud.rml](ui/tennis_hud.rml) | The scoreboard, the calls, the hints |
| [Lobby.cpp](Lobby.cpp) | The start menu's fields and options |
| [Center.cpp](Center.cpp) | The sport center: walkers, gates, the crowd, CPU matches on free courts, the win board |
| [Net.cpp](Net.cpp) | Online: Host / Join rows, who plays, events in and out, other machines' players |
| [NetTennis.h](NetTennis.h), [NetTennis.cpp](NetTennis.cpp) | The online messages and how they are packed (tested) |
| [Rules.h](Rules.h), [Rules.cpp](Rules.cpp) | The score and the umpire (pure) |
| [Shot.h](Shot.h), [Shot.cpp](Shot.cpp) | Flights, bounces, planning a shot, where to meet a ball (pure) |
| [Court.h](Court.h), [Court.cpp](Court.cpp) | Court sizes, boxes, positions, the ten-court layout, seats |
| [Ball.h](Ball.h), [Ball.cpp](Ball.cpp) | The ball: its flight and its FEMFX body |
| [Bot.h](Bot.h), [Bot.cpp](Bot.cpp) | The CPU player (pure) |
| [Body.h](Body.h), [Body.cpp](Body.cpp) | The mannequin, its clips, the IK swings, the racket |
| [textures/](textures/) | The ball's texture and the script that makes it |
