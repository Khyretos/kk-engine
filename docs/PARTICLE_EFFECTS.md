# Particle effects: smoke and sparks

`kke::ParticleEffects` (`kke/ParticleEffects.h`) is for the smoke and
sparks of action: tyre smoke from a burnout or a slide, a cloud from a
wrecked engine, dust, sparks off metal scraping a wall. A few thousand
particles at most, simulated on the CPU (position, velocity, drag, rise,
growth, fade) and drawn as camera-facing quads:

- **Smoke** is soft, lit by the sun (with its shadow) and the sky, and
  sorted back to front in every view, so thick smoke reads as a volume
  and darkens where the sun can't reach it. It thins out within a few
  metres of the camera, so a camera passing through a puff (a chase
  camera behind its own smoke trail) still sees.
- **Sparks** are additive streaks along their velocity, in linear HDR
  colour, so they bloom.

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

## Cost

The simulation and the quads are rebuilt on the CPU every frame, so keep
the count in the thousands. On the GPU the cost that matters is
overdraw: large puffs close to the camera are what to watch. Smoke is
drawn at full resolution with no extra passes. The racing demo allows
8000 alive; a 16-car oval race stays well under that (it logs the count
with `KKE_RACE_QUIT`).

## Where it's used

games/racing: tyre smoke from wheelspin and slides, burnouts on the
grid, engine smoke and fire below 45% health, sparks from hits and from
scraping walls and other cars.
