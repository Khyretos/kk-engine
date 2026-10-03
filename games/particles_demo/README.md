# Particles demo

A dark stone floor at dusk with a stone in the middle, and every effect
in the engine's particle library playing on it: fire, a campfire, a
torch, explosions, smoke, steam, dust, dirt, sparks, welding, a muzzle
flash, wood and stone chips, glass, a splash, rain, snow, leaves,
confetti, fireworks, magic, healing, a shockwave and coins. Pick one and
it plays on the stone (bursts again every few seconds, streams keep
going), or turn on "All at once" to see the whole library round a
circle.

The demo teaches two engine pieces: `kke::ParticleEffects`, which
simulates and draws a few thousand particles of five kinds (smoke,
sparks, glows, flakes and rings), and `kke::ParticleLibrary`, which
plays effects by name from YAML or JSON. Start here for fire, smoke,
impacts, weather, magic or celebrations in a game.

## Run it

The executable is `particles_demo` (`kke_add_game(particles_demo ...)`
in [CMakeLists.txt](CMakeLists.txt)). It needs no optional library and
no asset pack.

```bash
cmake --build build --target particles_demo
cd build/bin
./particles_demo
KKE_PARTICLES_EFFECT=explosion ./particles_demo   # start with one effect
KKE_PARTICLES_ALL=1 ./particles_demo              # start with all at once
```

| Variable | Effect |
|---|---|
| `KKE_PARTICLES_EFFECT=<name>` | the effect to start with (a name from the table in [docs/PARTICLE_EFFECTS.md](../../docs/PARTICLE_EFFECTS.md)) |
| `KKE_PARTICLES_ALL=1` | start with every effect at once |
| `KKE_SKIP_INTRO=1` | skip the logo intro (engine-wide) |

## Controls

| Action | Keyboard / mouse | Controller |
|---|---|---|
| Previous / next effect (`particles.previous`, `particles.next`) | Left / Right | LB / RB |
| Play it again (`particles.play`) | Space | A (south) |
| All at once on / off (`particles.all`) | A | Y (north) |
| Turn the camera | left-drag | right stick |
| Zoom | wheel | d-pad |
| Settings panel | F3, or click | View |

Rebindings are saved to `particles_demo_input.json`. The panel also has
"Bursts again by themselves" (and how often), a size slider (x0.25 to
x3) and wind (-8 to 8 m/s, which carries smoke, fire and flakes).

## How it works

[ParticlesDemoModule.cpp](ParticlesDemoModule.cpp):

- `init()` makes a `kke::ParticleEffects` (12000 particles at most) and
  a `kke::ParticleLibrary` on it, which starts with the built-in
  effects, then loads every `.yaml`, `.yml` or `.json` in
  `assets/effects/` beside the game: your own effects, or new versions
  of built-in ones.
- `restart()` stops everything and plays the chosen effect on the stone
  (`play()` for a burst, `start()` for one with a steady stream, as
  `ParticleLibrary::looping()` says), or one per stone round the circle.
  Weather (rain, snow) falls from 7 m over the whole floor instead; only
  the leaves fall in "all at once", the rest would bury the gallery.
- `update()` plays bursts again every few seconds and calls
  `m_library->update(dt, wind)`, which emits for everything running and
  moves every particle.
- `renderTranslucent()` draws them: flakes and smoke sorted back to
  front, then glows, rings and sparks added as light.

## Make a game like this

Copy an effect from `ParticleLibrary::builtInYaml()` (or the table in
[docs/PARTICLE_EFFECTS.md](../../docs/PARTICLE_EFFECTS.md)) into
`assets/effects/my_effects.yaml`, rename it, change its numbers, and
play it where something happens:

```cpp
m_library->play("wood_chips", { crate.position });                // a crate breaks
m_library->play("sparks", { hit.point, hit.normal, car.velocity }); // metal scrapes
auto fire = m_library->start("campfire", { camp });                 // keeps burning
```

## Files

| File | What |
|---|---|
| [main.cpp](main.cpp) | the modules: input, UI, audio, orbit camera, the demo, its panel |
| [ParticlesDemoModule.h](ParticlesDemoModule.h) / [.cpp](ParticlesDemoModule.cpp) | the gallery |
| [game.json](game.json) | the marketplace entry |
| `engine/include/kke/ParticleEffects.h`, `ParticleLibrary.h` | the engine pieces |
| `shaders/effect*.vert/frag` | how each kind is drawn |
