# Particle effects: smoke, sparks, fire and more

`kke::ParticleEffects` (`kke/ParticleEffects.h`) is for the smoke and
sparks of action: tyre smoke from a burnout or a slide, a cloud from a
wrecked engine, dust, sparks off metal scraping a wall. A few thousand
particles at most, simulated on the CPU (position, velocity, drag, rise,
growth, fade) and drawn as camera-facing quads:

- **Smoke** is soft, lit by the sun (with its shadow) and the sky, and
  sorted back to front in every view, so thick smoke reads as a volume
  and darkens where the sun can't reach it.
- **Sparks** are additive streaks along their velocity, in linear HDR
  colour, so they bloom.
- **Glows** are soft round light, added: flames, embers, magic, a muzzle
  flash. Overlapping glows add up and bloom.
- **Flakes** are small solid cards lit by the sun and sky: confetti,
  snow, leaves, wood chips, grit, glass slivers, coins. They tumble
  (`flip`), sway (`flutter`), and come to rest on their `floor`.
- **Rings** are expanding bands of light, facing the camera or lying
  flat on the ground: a shockwave, a splash's ripple, a portal's pulse.

`kke::ParticleLibrary` (`kke/ParticleLibrary.h`) plays ready-made effects
by name ("explosion", "campfire", "confetti", below), each written as a
few lines of YAML or JSON you can copy and change. `games/particles_demo`
shows every one of them.

For a fountain of tens of thousands of GPU particles, see
`ParticleModule` (compute shaders) instead; for liquids,
`ParticleFluid`.

## Use it

```cpp
#include "kke/ParticleEffects.h"

// init():
m_fx = std::make_unique<kke::ParticleEffects>(*m_app, 8000); // at most 8000 alive

// update(dt), wherever something smokes or scrapes:
m_fx->smoke(wheelPosition, carVelocity * 0.3f, { 0.85f, 0.85f, 0.85f });      // tyre smoke
m_fx->smoke(bonnet, glm::vec3(0, 1, 0), { 0.05f, 0.05f, 0.05f }, 0.5f, 3.0f); // a dying engine
m_fx->sparks(hit.point, hit.normal, 24, 7.0f, carVelocity);                 // metal on a wall
m_fx->update(dt, wind);                                                      // moves and ages all

// renderTranslucent(ctx): once per view in split screen
m_fx->draw(ctx);

// shutdown(), or a new level:
m_fx->clear();
```

## The calls

| Call | What |
|---|---|
| `smoke(position, velocity, color, radius = 0.35, life = 2.2, opacity = 0.55)` | one puff; `color` is an sRGB albedo: grey-white for tyres, near-black for an engine fire, brown for dust |
| `sparks(position, normal, count, speed, along = 0)` | a burst leaving the surface (`normal` points away from it) at about `speed` m/s, carried by `along` (the moving car's velocity) |
| `emit(Particle)` | any particle, every field yours (below) |
| `update(dt, wind)` | integrates everything; smoke drifts with `wind` (m/s) |
| `draw(ctx)` | from `renderTranslucent`, after everything opaque |
| `clear()`, `count()`, `capacity()` | |

When full, a new particle replaces the oldest.

`Particle`'s fields: `kind` (Smoke or Spark), `position`, `velocity`,
`color` (smoke: sRGB albedo; spark: linear light such as (6, 3, 1)),
`radius` (m at birth), `growth` (m/s the radius grows), `opacity` (at
birth, fading to 0 over its life), `life` (s), `drag` (1/s), `rise`
(m/s² up for hot smoke; sparks fall with gravity instead), `spin`
(rad/s), `stretch` (a spark's streak, in seconds of travel), `seed`
(0..1, the puff's shape).

`Particle`'s newer fields: `colorEnd` (the colour it turns to over its
life; negative keeps `color`), `flip` (rad/s a flake turns over),
`flutter` (m/s of sway while a flake falls), `thickness` (a ring's band,
as a fraction of its radius), `floor` (y of the ground: sparks bounce
off it, flakes rest on it), `shape` (a flake's outline: Square, Disc or
Shard), `flat` (a ring lies on the ground).

## Ready-made effects: kke::ParticleLibrary

```cpp
#include "kke/ParticleLibrary.h"

// init():
m_fx = std::make_unique<kke::ParticleEffects>(*m_app);
m_library = std::make_unique<kke::ParticleLibrary>(*m_fx);   // the built-in effects
m_library->load("assets/effects/my_effects.yaml");             // yours (adds or replaces)

// A burst, once, where something happened:
m_library->play("explosion", { hit.point, hit.normal });
m_library->play("wood_chips", { crate.position, glm::vec3(0, 1, 0), glm::vec3(0), /*scale=*/1.5f });

// Something that keeps going until you stop it:
auto fire = m_library->start("campfire", { glm::vec3(2, 0, 0) });
m_library->move(fire, { torch.position, {0, 1, 0}, torch.velocity }); // follow something
m_library->stop(fire);                                               // what's in the air finishes

// update(dt): emits for everything running, then moves every particle
m_library->update(dt, wind);
// renderTranslucent(ctx):
m_fx->draw(ctx);
```

A `Spot` is where it plays: `position`, `normal` (away from the surface;
bursts spray around it), `velocity` (of what it came from), `scale` (x
speed, size and area), `floor` (y of the ground). `play()` runs an effect
once; `start()` runs it until `stop()`. `looping(name)` says whether an
effect has a steady stream (start it) or only bursts (play it).

### The built-in effects

| Name | What |
|---|---|
| `fire` | flames licking up, smoke above them (1.5 s) |
| `campfire` | a fire that keeps burning, with embers and smoke |
| `torch` | a flame on a stick: `move()` it with the torch |
| `explosion` | a fireball, a shockwave, sparks, debris and a column of smoke |
| `smoke_column` | thick black smoke rising from something burning |
| `steam` | white steam puffing up |
| `dust` | a puff of dust: a landing, a footstep, a falling crate |
| `impact_dirt` | earth and stones kicked up by a hit |
| `sparks` | a shower of sparks off metal |
| `welding` | steady sparks and a blue-white glow |
| `muzzle_flash` | a gun's flash, sparks and a wisp of smoke, along the normal |
| `wood_chips` | splinters and dust from wood breaking |
| `stone_chips` | grit and grey dust from stone breaking |
| `glass_shatter` | glinting slivers of glass |
| `splash` | droplets and a ripple on the surface |
| `rain` | rain over a 20 m square: `move()` it with the camera |
| `snow` | snow drifting down over a 20 m square |
| `leaves` | autumn leaves tumbling down |
| `confetti` | a burst of party confetti |
| `fireworks` | a firework bursting into coloured stars |
| `magic` | sparkles swirling up and a soft pulsing ring |
| `heal` | green sparkles rising round a character |
| `shockwave` | a ring of force across the ground, kicking up dust |
| `coins` | gold coins bursting out and spinning to the ground |

`ParticleLibrary::builtInYaml()` is the whole set as YAML: the best
place to start your own.

### Writing an effect

A file is a map of effect name to effect; an effect is a `description`
and a list of `layers`. Each layer is one kind of particle, either a
burst (`count`) or a steady stream (`rate` a second):

```yaml
lantern:
  description: A small flame with a soft glow round it
  layers:
    - kind: glow                     # smoke, spark, glow, flake or ring
      rate: 25                       # a stream; `count: 30` would be a burst
      speed: [0.2, 0.5]              # m/s, a random value between the two (or one number)
      spread: 15                     # degrees around the normal (180: every way)
      up: true                       # around world up instead of the normal
      jitter: 0.03                   # born up to this far from the spot (m)
      colors: [[6, 2.8, 0.8]]        # one picked at random: light for glow/spark/ring, sRGB for smoke/flake
      colorEnd: [1.5, 0.2, 0.03]     # what it turns to over its life
      radius: [0.05, 0.08]           # m
      growth: -0.05                  # m/s the radius grows
      life: [0.3, 0.5]               # s
      drag: 3                        # 1/s
      rise: 1.5                      # m/s² up (negative falls: -9.81 for chips and drops)
```

Every layer field: `kind`, `count`, `rate`, `duration` (a stream's
length when played once; a second if not given), `delay`, `speed`,
`spread`, `up`, `inherit` (x the spot's velocity), `jitter`, `box` (born
anywhere in this box around the spot, half sizes: rain and snow),
`colors` (or one `color`), `colorEnd`, `brightness` (+- random
brightness, 0..1), `radius`, `growth`, `opacity`, `life`, `drag`, `rise`,
`spin`, `flip`, `flutter`, `stretch`, `thickness`, `shape` (square, disc,
shard), `flat`. Ranges (`speed`, `radius`, `life`, `spin`, `flip`) take
one number or `[min, max]`.

A mistake names the effect and what's wrong ("effect 'puff': unknown
kind 'plasma'"), and nothing from that file is added.
`ParticleLibrary::validate()` checks a file without adding it (for a
mod loader or a tool).

### Learning from the Synty particle pack

The library's set was drawn up from the effects in Synty's POLYGON
Particle FX pack (fire, smoke, explosions, dust, debris, sparks, magic,
weather, confetti, coins, hearts, portals). The pack's Unity effect
setups aren't in its source files, so the effects are our own; its
meshes (shards, chips, coins) and textures can't be shipped in the
public repo anyway (docs/ASSETS.md), and flakes draw those shapes
themselves.

## Cost

The simulation and the quads are rebuilt on the CPU every frame, so keep
the count in the thousands. On the GPU the cost that matters is
overdraw: large puffs close to the camera are what to watch. Smoke is
drawn at full resolution with no extra passes. Glows, flakes and rings
cost the same as smoke and sparks; each kind's pipeline is made only the
first time that kind is drawn. The racing demo allows 8000 alive; a
16-car oval race stays well under that (it logs the count with
`KKE_RACE_QUIT`). The particles demo with every effect at once runs
about 1000 to 2500.

## Where it's used

games/particles_demo: every effect in the library, one at a time or all
at once round a circle, with size and wind sliders (README.md there).

games/racing: tyre smoke from wheelspin and slides, burnouts on the
grid, engine smoke and fire below 45% health, sparks from hits and from
scraping walls and other cars.
