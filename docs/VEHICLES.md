# Vehicles

Cars, trucks, karts: wheeled vehicles on Jolt's vehicle physics
(`JPH::VehicleConstraint` with a `WheeledVehicleController`), made with
one call on `RigidWorld` (`kke/Vehicle.h`). Jolt does the suspension,
the tyres, the engine, the gearbox and the differentials; the engine adds
a friction circle, per-wheel grip and a power scale so a game can model
damage and drifting. The racing demo (games/racing/README.md) is built
on it.

## A car in a few lines

```cpp
#include "kke/RigidWorld.h"   // kke/Vehicle.h comes with it

kke::VehicleDesc d;
d.halfExtents = { 0.9f, 0.4f, 2.1f };     // the chassis box (or d.hull = convex hull points)
d.position = { 0.0f, 1.0f, 0.0f };
d.mass = 1300.0f;
d.maxTorque = 500.0f;                     // Nm
for (int i = 0; i < 4; ++i) {             // front-left, front-right, rear-left, rear-right
    kke::VehicleWheelDesc w;
    const bool front = i < 2;
    w.position = { i % 2 == 0 ? 0.8f : -0.8f, -0.1f, front ? 1.3f : -1.3f }; // +X is the car's left
    w.maxSteerDegrees = front ? 32.0f : 0.0f;
    w.maxHandBrakeTorque = front ? 0.0f : 4000.0f;
    d.wheels.push_back(w);
}
d.drivenAxles = { 1 };                    // rear-wheel drive ({0} front, {0, 1} all)

kke::RigidWorld& world = rigidBodyModule->world();
kke::RigidWorld::VehicleId car = world.addVehicle(d);

// Every physics step (Module::fixedUpdate):
kke::VehicleInput in;
in.throttle = 1.0f;       // 0..1 (automatic: below 0 reverses once stopped)
in.brake = 0.0f;          // 0..1
in.steer = -0.3f;         // -1 left .. 1 right
in.handBrake = 0.0f;
world.setVehicleInput(car, in);

kke::VehicleState s;
world.vehicleState(car, s);   // speed, rpm, gear, and every wheel's transform, contact and slip
```

Draw the body at `world.transform(world.vehicleBody(car))` and each wheel
at `s.wheels[i].transform` (the wheel's mesh modelled centred on the
wheel, in the car's axes: it comes with the spin, the steering and the
suspension travel).

## Axes

Body space: **+Z forward, +Y up, +X the car's left** (right-handed: a car
seen from behind has +X on its left). Wheels come in axle pairs, the left
wheel first. `VehicleInput::steer` is positive to the right;
`VehicleWheelState::steerDegrees` is positive to the left (Jolt's).

## What the numbers do

| Field | What | Typical |
|---|---|---|
| `VehicleDesc::mass` | kg | 1000 (hatch) to 1800 (ute) |
| `centerOfMassOffset` | added to the shape's centre of mass; keep it low or the car rolls over in every corner | (0, -0.35, 0) |
| `maxTorque`, `minRpm`, `maxRpm` | the engine | 350-700 Nm, 1000, 6500-8200 |
| `gearRatios`, `reverseRatio`, `differentialRatio` | the gearbox | 5 gears, 2.66 .. 0.74 |
| `manualGearbox` | `VehicleInput::gear` picks the gear (-1 reverse, 0 neutral) | drag racing |
| `shiftUpRpm`, `shiftDownRpm` | the automatic gearbox | 5500, 2500 |
| `limitedSlipRatio` | how different the wheels of a driven axle may spin | 1.4 (LSD) |
| `antiRollStiffness` | per axle pair | 6000 |
| `VehicleWheelDesc::suspensionFrequency`, `suspensionDamping` | stiffness (Hz) and damping | 1.5-2.5, 0.5 |
| `suspensionMin`, `suspensionMax` | travel below the attachment point | 0.05, 0.3 m |
| `maxBrakeTorque`, `maxHandBrakeTorque` | Nm; front-biased brakes let throttle + brake spin the rears (a burnout) | 1500-2600, 4000-5000 on the rears |
| `longitudinalGrip`, `lateralGrip` | tyre grip forward and sideways (1 = a road tyre on asphalt) | 1.2, 1.0 |
| `slideGrip` | the share of the peak grip left once the tyre slides fully; low keeps a drift going | 0.7-0.85 |
| `combinedSlipLoss` | the friction circle: the share of sideways grip a spinning or locked tyre loses | 0.6 (0 = off) |

The tyres: forward grip peaks at about 6% slip and falls off (wheelspin,
locked brakes); sideways grip peaks at about 3 degrees of slip angle, is
halfway to `slideGrip` at 20 and at `slideGrip` by 90. On top of that,
past 10% of wheelspin or lock the sideways grip fades by up to
`combinedSlipLoss` (reached at 70%): pull the handbrake or light up the
rears and the back of the car steps out, like a real car.

## Tyres

Every wheel has a tyre (`kke::Tyre`, `kke/Tyre.h`) that lives on beyond
its friction curve, run by `RigidWorld` after every step and fed back
into the grip on the next. It is what the driving sims do, cheaply:

- **Heat, in two layers** (rFactor 2 and Assetto Corsa model it this
  way). The tread's surface heats in seconds from sliding: the friction's
  power, force times how fast the tread slides over the road (a burnout
  passes 150 C in a few seconds). It cools in the air, faster with speed,
  and into the road and the carcass. The carcass and the air inside warm
  in minutes from the rubber flexing as it rolls (more when it's soft).
  Grip peaks in a window (`optimalTemp` 80 C, `window` 55 C either side):
  cold tyres slide, cooked ones go greasy.
- **Pressure** follows the air's temperature (the gas law: 2.2 bar cold is
  ~2.6 at 80 C) and a puncture lets it out. Grip is best a little above
  the cold setting. A soft tyre flexes more (heat, drag).
- **Wear** from sliding, three times faster above 120 C; a worn-out tyre
  blows.
- **Load sensitivity** (BeamNG's `noLoadCoef`/`fullLoadCoef`, every sim's
  rule): twice the load gives less than twice the grip (`loadSensitivity`
  0.15: 85% of it per newton). Weight thrown onto the outside tyres in a
  corner costs grip, so a car has a balance instead of running on rails.
- **The ground** (`RigidWorld::setGroundGrip` by the body's
  `BodyDesc::material`, `kke::GroundGrip`): its grip, rolling resistance
  and the shape of the curve. On loose ground (`loose`: gravel 0.8, mud
  0.9) a sliding tyre keeps nearly all of its peak, because it digs in
  (rally drivers slide on purpose; on tarmac a slide loses grip). Presets:
  `tarmac()`, `concrete()`, `gravel()`, `dirt()`, `grass()`, `mud()`,
  `snow()`, `ice()`.
- **Damage**: `punctureVehicleTyre` (a leak; flat when a quarter is
  left), `setVehicleTyre` with `TyreCondition::Flat` (on its sidewalls:
  lower, half the grip, six times the rolling drag), `Rim` (the tyre shreds
  off after ~1.5 km flat: metal on the road, 0.3 grip) or `Detached` (the
  wheel is off and the car sits on its hub). The wheel's radius drops with
  it, so the car sits down on that corner. `replaceVehicleTyres` fits a
  fresh set; `setVehicleTyreTemperature` is the tyre warmers.

`VehicleWheelState` reports it all: `load`, `surfaceTemp`, `coreTemp`,
`pressure`, `wear`, `slidePower` (for smoke and marks), `grip` (the scale
on the friction curve right now), `radius`, `condition` and the ground's
`groundMaterial`. `TyreDesc` in `VehicleWheelDesc::tyre` sets the window,
the pressure, the load sensitivity, and scales on heat and wear (0 turns
either off).

What was left out, and why: relaxation length (slip building up over the
tyre's first half metre of rolling) needs the slip to be filtered before
Jolt's solver sees it, and speed-dependent sliding friction would change
how existing drifts hold; neither shows in a game as much as it costs in
feel. Sources: BeamNG's wheel documentation and its tyre blog posts
(<https://documentation.beamng.com/modding/vehicle/sections/wheels/>,
<https://www.beamng.com/game/news/blog/a-look-at-tire-development-in-beamng-part-2/>),
the Live for Speed tyre report (<https://www.lfs.net/report-dec2009>),
<https://en.wikipedia.org/wiki/Rolling_resistance> and
<https://en.wikipedia.org/wiki/Relaxation_length>.

The racing demo draws the rest (games/racing/README.md "Wheels"): the
tyre squashing on the road, flats, bare rims sparking, torn-off wheels
rolling away, dust on loose ground.

## Damage, drifting and resets

- `setVehiclePower(car, scale)`: scales the engine's torque (a hurt
  engine, a speed limiter, 0 for a wreck).
- `setVehicleWheelGrip(car, wheel, scale)`: scales one tyre's grip (a
  bent wheel, a puncture, a patch of oil).
- `resetVehicle(car)`: stops the wheels and the engine after you move
  the body with `setTransform` (back on the track).
- `removeVehicle(car)`: removes the constraint and its body.

Contacts come through `RigidBodyModule::frameContacts()` like any body's
(the chassis' `VehicleDesc::material` names it); the wheels are casts,
not bodies, and report their ground in `VehicleWheelState::groundBody`.

## Sound

`kke::EngineSound` (docs/AUDIO.md "Engines and tyres") turns
`VehicleState::rpm`, the throttle and the wheels' slip into an engine and
squealing tyres, with no recordings: feed it every frame and play its
samples from the car.

## Other machines' cars

A car driven on another machine doesn't need a vehicle here: a kinematic
body with the same hull, moved each frame with `moveKinematic` to where
its owner says, pushes and is pushed by the local cars. The racing demo
does this (games/racing/Net.cpp).

## Notes

- Drive and read cars in `fixedUpdate` (after the physics step), draw
  them interpolated with `Application::fixedAlpha()`.
- Stepping at 60 Hz is enough for road cars up to ~300 km/h; the
  wheels are cylinder casts (they meet kerbs with their width).
- The Jolt documentation for the vehicle system:
  <https://jrouwe.github.io/JoltPhysics/index.html#vehicles>
