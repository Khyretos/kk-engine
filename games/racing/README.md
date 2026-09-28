# Racing

Three kinds of race on Jolt's vehicle physics, from a start menu where
players join locally or online:

- **Oval**: laps of a banked speedway with a big field (up to 24 cars),
  two abreast, bump drafting and a pit lane. Every hit hurts: cars dent
  where they are struck, lose power and grip, and can be totalled, so you
  drive carefully to finish.
- **Drift**: a twisty loop round the docks. Cars leave the line one after
  another; sliding through the corners scores points, longer and faster
  slides build a combo, and hitting anything loses it.
- **Drag**: a straight quarter mile, eight lanes. Hold the brake and the
  throttle for a burnout while the ambers count down, launch on green
  (early is a red light) and change gear yourself at the top of the revs.

Tyre smoke from burnouts and slides, sparks off metal, skid marks that
stay on the road, bits of bodywork that fly off, smoke and fire from a
wrecked engine. With the Synty **POLYGON Street Racer** pack the cars,
paint jobs and trackside props are Synty's; without it every car is a
coloured block of the right size, and the game plays the same.

![The grid in the start menu](../../website/static/media/racing-lobby.webp)

## Run it

```
cmake --build build --target racing
./build/bin/racing
```

The start menu opens on the grid. Press **A** (or Enter) to join, pick a
name, car, body kit and paint, and start. Player 1 picks the track (which
picks the event), how many cars, laps, how good the CPU drivers are and
how much damage hurts.

For demos and tests:

| Variable | What it does |
|---|---|
| `KKE_RACE_AUTOPILOT=1` | the CPU drives player 1; races start by themselves and restart 10 s after the finish |
| `KKE_RACE_TRACK=speedway` | this track (`speedway`, `harbour_drift`, `quarter_mile`, or any file in `tracks/`) |
| `KKE_RACE_CARS=16`, `KKE_RACE_LAPS=3` | the field and the laps |
| `KKE_RACE_DAMAGE=0/1/2` | damage off, normal, brutal |
| `KKE_RACE_CRASH=1` | the damage test: CPU drivers aim at the car in front |
| `KKE_RACE_CAMERA=0..3` | chase, far chase, bonnet, TV |
| `KKE_RACE_LOBBY=0` | straight into a race, no start menu |
| `KKE_RACE_QUIT=60` | quit after 60 s, printing where every car is every 5 s |
| `KKE_ASSETS_DIR=...` | where the Synty packs are (docs/SCENES.md) |

## Controls

Up to four players on one screen (split screen), each with a gamepad or
the keyboard.

| | Gamepad | Keyboard |
|---|---|---|
| Steer | left stick | A / D or the arrows |
| Gas / brake | right / left trigger | W / S (S reverses once stopped) |
| Handbrake (start a slide) | A | Space |
| Gear up / down (drag strip) | B / X | E / Q |
| Look back | LB | B |
| Back on the track | D-pad up | T |
| Camera | Y | C |
| Race again / next track | Start / D-pad right | R / N |
| Start menu | Back | M |
| How to play | D-pad down | H |
| Developer panels | | F1 |

On a touch screen: the left half of the screen steers (where your finger
is, left or right of the middle of that half), the right quarter is the
gas and the strip left of it the brake.

The hint bar at the bottom always shows the buttons of the device you
last used (Xelu's prompts, docs/INPUT.md).

## How it plays

### The start menu

`kke::LobbyModule` (docs/LOBBY.md): each seat picks a name, one of six
cars (stock car, sports, exotic, sedan, hatch, ute: each with its own
power, weight, drive and grip), one of four body kits and a paint job.
Player 1's options: the track, how many cars (2 to 24; 8 on the strip),
laps, the CPU drivers (Easy to Expert), damage (Off, Normal, Brutal) and
Online. Behind the menu the grid waits on the chosen track with every car
as it will race, so changing a paint job shows it at once.

### The oval

A rolling grid two abreast behind the line; the lights count down and
everyone is held on the brake until green. The CPU drivers pick a lane,
change lanes only when nobody is beside them, go round a slower car when
there is room and sit behind it when there isn't, brake for the tightest
corner within braking distance (grip, curvature and banking) and pit
when hurt. The pit lane on the front straight has a 50 km/h limiter and
repairs a stopped car at 30% a second.

After the leader finishes everyone else finishes as they next cross the
line; the race ends when every car is home or out, or a minute after the
winner.

### Damage

Every hit is measured by how fast the two surfaces met (not how fast you
were going: a tap in a pack costs little, a head-on everything). It takes
health, dents the body where it landed (the mesh itself moves in, deeper
for harder hits), may bend a wheel (the car pulls to that side and that
tyre grips less), throws sparks and, above 40 km/h, bits of bodywork.
Health sets the engine's power (half at zero); below 45% the engine
smokes, and a totalled car stops where it is. Scraping along a wall or
another car wears it down slowly with a trail of sparks.

### Drift

Cars leave the line three seconds apart, so every run has room. A slide
counts when three wheels are on the road, the car is at 12 to 80 degrees
to where it is going and faster than 7 m/s. Points come from the angle
times the speed, times a combo that grows every two seconds held (up to
x5); a second out of the slide banks the chain. Hitting anything loses it.
Most points wins.

The drift cars have looser rear tyres, and every car's tyres follow a
friction circle: a wheel that spins or locks has less to give sideways,
so a handbrake pull or a burnout steps the rear out and the throttle
holds it there.

### Drag

Eight lanes, the lights count down four seconds; the ambers come on for
the last one and a half, and from then on the brake is yours: moving
0.4 m before green is a false start (a red light, last place). The
gearbox is manual; "Perfect shift" at the top of the revs. Your reaction
time and your speed through the line are on the results.

### Online

Player 1 sets Online to Host or Join in the start menu (or
`KKE_NET=host`, `KKE_NET=join:ADDRESS`); games on the local network show
up by themselves, and the join codes and relay in docs/SERVER_HOSTING.md
work too. Everyone on each screen drives. The host picks the track and
starts every set of lights.

## How it works

### Startup and the frame

`main.cpp` adds the modules (input, Jolt, models, RmlUi, audio, the
start menu, networking) and `RacingModule`. In `init()` it reads the
tracks, scans the asset folder for the Street Racer pack, sets up the
start menu and networking and builds the first grid.

Every physics step (`fixedUpdate`, after Jolt's step) each car here is
read (position, speed, wheels, where it is on the track), driven (a
player's input or the CPU's) and the race clock moves on. Every frame
(`update`) handles contacts and damage, effects, the cameras and the
HUD. Drawing interpolates each car between the last two physics steps,
so a car at 200 km/h is smooth at any frame rate.

### Tracks (Track.h, Track.cpp)

A track is a YAML (or JSON, kke::datafile) file in `tracks/`: an oval, a
loop through points or a straight strip, its width, banking, walls and
laps (every key is in `Track.h`). It becomes a centre line sampled every
2 m, each sample with its own frame (forward, left across the banked
road, up) and curvature. From that come the road and walls (triangles
drawn with `DynamicMeshRenderer` and given to Jolt as static meshes),
the lane markings, the grid slots, and `locate()`: where any point is on
the track, as distance along it (`s`) and across it (`u`). Laps,
positions, the CPU drivers and the "back on the track" reset all use
`s` and `u`.

### Cars (Cars.h, Cars.cpp)

`Garage` makes a car's art: the Street Racer car (`SK_Veh_Preset_<type>_0<kit>`),
split into the body (one mesh part per material) and a left and right
wheel, with where the four wheels sit and a convex hull for the chassis.
A paint job is a whole texture of the pack (the colour up top, badges,
plates and lights below), so both the body and livery materials get it.
Without the pack a block car of the type's size stands in.

`RacingModule::buildCar` turns it into a Jolt vehicle
(`RigidWorld::addVehicle`, docs/VEHICLES.md): the hull, the mass and
torque of its type, rear, front or all-wheel drive, suspension, brakes
biased to the front (so brake and throttle together spin the rears: a
burnout) and a handbrake on the rears. On the drag strip the gearbox is
manual. Cars from other machines are kinematic hulls that move where
their owner says.

### Driving (Driving.cpp)

`readPlayer`: steering that ramps in (a keyboard's full lock over a
fifth of a second) and gets less at speed, so a key press doesn't spin a
car at 150 km/h. `readCpu`: the lane, overtaking or following, the
corner speed, pitting, the drift technique (a flick of the handbrake
into a bend, then held sideways on the throttle with the front wheels
along the slide and aimed down the road) and the drag launch and
shifts. `driveCar` applies the rules on top: held on the grid, the pit
limiter and repairs, a bent wheel's pull, a totalled car's stop.

### Damage (Damage.cpp)

Jolt reports new contacts (`RigidBodyModule::frameContacts()`); a car's
contacts with another car or the walls become `hitCar`. The dent moves
the body's vertices near the hit inwards along the hit's direction,
fading with distance and never past the car's middle, then recomputes
normals; `ModelModule::setDeformedVertices` draws the dented copy. Debris
are small boxes on Jolt that fade after 14 s. Online, a dent is an event
so everyone sees the same car.

### Effects

`kke::ParticleEffects` (docs/PARTICLE_EFFECTS.md): tyre smoke where a
wheel spins or slides, engine smoke and fire, sparks off hits and
scrapes. Skid marks are one growing `DynamicMeshRenderer` mesh of quads
laid under sliding tyres (up to 3000). Contact sounds come from the
audio module's impact synthesis (docs/AUDIO.md), from the bodies'
materials.

### Split screen and cameras

One view per player (`kke/Viewports.h`): stacked for two, quarters for
three and four; with three players the fourth quarter is a TV camera
that follows the leader. Each player's camera is a chase, far chase,
bonnet or TV camera; the chase camera widens its field of view with
speed.

### The HUD (Hud.cpp, ui/racing_hud.rml)

RmlUi with a data model: each player's panel in their view's corner
(place, lap, times, drift points and notes such as "Perfect shift") and
dash (speed, gear, revs, the car's health), the standings, the lights,
banners and the results table. Menus and HUD keep to the screen's safe
area (docs/INPUT.md "Phone, PC and console").

### Networking (Net.cpp, NetRace.h, NetRace.cpp)

Modelled on Climb Race. The host sends a Setup event (the track, laps,
damage and every seat: who drives which car), a Go event to start the
lights, and its CPU cars 15 times a second. Each machine sends its own
cars in `NetPlayerState` (position, velocity) with the rest packed into
its 32 extra bytes (rotation, speed, steering, revs, health, lap,
progress, which tyres smoke). Finishes and dents are events. Remote cars
are dead-reckoned from their last pose.

## Design decisions

- **Jolt's vehicles, not our own.** Jolt's `WheeledVehicleController`
  has suspension, tyre friction curves, an engine, a gearbox, limited
  slip differentials and anti-roll bars, tested in many games. The
  engine adds only what it lacks: a friction circle (wheelspin and
  locked wheels lose sideways grip), per-wheel grip for damage and a
  power scale.
- **Damage from the closing speed.** Racing in a pack means constant
  touches; measuring the speed the surfaces met at makes bumping cheap
  and crashing expensive, like the real thing.
- **Dents in the mesh, not swapped parts.** Works on any car, block or
  Synty, and shows exactly where the hit was.
- **Drift runs one after another.** Eight cars sliding into the first
  corner together is a pile-up, not a drift contest.
- **The race runs on the physics clock.** Lap times and the countdown
  use the same steps as the cars, so a slow frame never changes a result.

## Tuning

| What | Where |
|---|---|
| A car type's weight, power, drive, grip, lock | `carTypes()` in `Cars.cpp` |
| Suspension, brakes, tyres | `buildCar` in `Driving.cpp` / `Cars.cpp` |
| How much a hit hurts, dent depth | `hitCar` in `Damage.cpp` |
| Drift scoring | `updateDrift` in `Damage.cpp` |
| CPU pace, lanes, drifting | `readCpu` in `Driving.cpp`; skill in `resetRace` (`Race.cpp`) |
| Pit limiter and repair rate | `kPitSpeed`, `driveCar` in `Driving.cpp` |
| A track | a file in `tracks/` |

## Engine features it uses

Jolt vehicles (`kke/Vehicle.h`, docs/VEHICLES.md), `kke::ParticleEffects`
(docs/PARTICLE_EFFECTS.md), model deformation (`ModelModule::setDeformedVertices`),
`DynamicMeshRenderer`, the start menu (docs/LOBBY.md), networking
(docs/NETWORKING.md), split screen and button prompts (docs/INPUT.md),
moods (docs/MOODS.md), impact sounds (docs/AUDIO.md), data files
(docs/DATA_FILES.md).

## Assets

The Synty **POLYGON Street Racer** pack, from `KKE_ASSETS_DIR` (never in
the repository); docs/SCENES.md lists every asset used. Without it the
cars are blocks, the tracks have no props, a line in the log (info) and
on screen says so, and everything else is the same.

## Make a game like this

- **Another track**: copy a file in `tracks/`, change the numbers or the
  points; it's in the start menu next time.
- **Another event**: a new `Event` in `Track.h`; its rules in
  `updateRace`/`crossLine` (`Race.cpp`) and its HUD in `Hud.cpp`.
- **A car game of your own**: `RigidWorld::addVehicle` with a
  `VehicleDesc` is all the physics (docs/VEHICLES.md has a complete
  example); `ParticleEffects` for smoke.

## Files

| File | What |
|---|---|
| `main.cpp` | the modules |
| `RacingModule.h`, `RacingModule.cpp` | the game: state, the frame, drawing |
| `Track.h`, `Track.cpp` | tracks from `tracks/*.yaml`: the road, walls, where a car is |
| `Cars.h`, `Cars.cpp` | car types, paint jobs, the car art (Synty or blocks) |
| `Race.cpp` | the field, the grid, the race rules, laps and results, scenery |
| `Driving.cpp` | players, CPU drivers, touch, the rules on top, cameras |
| `Damage.cpp` | hits, dents, debris, smoke, sparks, skid marks, drift points |
| `Lobby.cpp` | the start menu |
| `Net.cpp`, `NetRace.h`, `NetRace.cpp` | online races |
| `Hud.cpp`, `ui/racing_hud.rml` | the HUD |
| `tracks/` | Kompas Speedway, Harbour Run, The Quarter Mile |
