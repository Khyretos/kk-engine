# Water demo

A tank of water with an open front to look in. At the start a dam
breaks: a metre of water 80 cm deep falls over, races across the floor,
climbs the far wall and sloshes back and forth until it settles. Three
wooden crates drop into it, get carried along by the wave and end up
floating about half under; a stone block sinks. A paddle at the right
wall makes waves on demand.

The demo teaches three engine pieces that work together:

- `kke::MpmWater`: water as particles on a grid, the Moving Least Squares
  Material Point Method (Hu et al. 2018, docs/RESEARCH.md). No neighbour
  search, so it runs on the GPU (`kke::MpmWaterGpu`, four compute passes
  a substep); on the CPU it splits over the cores.
- `kke::WaterBodyLink`: the crates and the paddle are ordinary Jolt
  bodies. Each step their boxes go to the water, and what the water did to
  them comes back as velocity changes: buoyancy, being pushed by a wave,
  drag. Jolt keeps gravity and contacts.
- `kke::FluidSurfaceRenderer` with `Settings::clearWater`: the particles
  drawn as one see-through surface, tinted by how much water you look
  through (red goes first, as in real water), bending what's behind,
  reflecting the sky, with foam where the water is fast.

Start here for water a player can splash in: pools, flooding rooms,
buckets, boats on a pond, a dam breaking in a level. For the open sea use
`kke::Ocean` (games/sea_demo); for lava and small pours with temperature,
`kke::ParticleFluid` (games/melt_demo).

## Run it

The executable is `water_demo` ([CMakeLists.txt](CMakeLists.txt)). The
root `CMakeLists.txt` adds it when Jolt is on (`KKE_ENABLE_JOLT`, the
default).

```bash
cmake --build build --target water_demo
cd build/bin
./water_demo
KKE_SKIP_INTRO=1 ./water_demo           # skip the logo intro
KKE_WATER_CELL=0.04 ./water_demo        # finer water (8x the particles of 8 cm)
```

| Variable | Effect |
|---|---|
| `KKE_WATER_CELL=metres` | the water's grid spacing, 0.02 to 0.2. Default 0.05 on a GPU (~77,000 particles), 0.07 on the CPU (~28,000). Halving it gives 8x the particles |
| `KKE_MPM_GPU=0` / `1` | keep the water on the CPU / put it on the GPU even on a software one (lavapipe) |
| `KKE_MPM_GPU_CHECK=1` | every 120th step runs on both the GPU and the CPU from the same state and logs how far apart the particles end up (`check:` lines) |

Every 5 s it logs a `water:` line: particles, step time, substeps, GPU or
CPU, bodies in the water, the fastest particle, GPU steps and late ones.

## Controls

Actions (rebindable in `water_demo_input.json`, keyboard or controller):

| Action | Keyboard | Controller |
|---|---|---|
| Make a wave | Space | A / Cross |
| Drop a crate | C | B / Circle |
| Drop a stone block | V | Right shoulder |
| Reset (the dam breaks again) | R | X / Square |
| Clear water / raw particles | L | Y / Triangle |

The panel (F3, or a controller's View button) has the same buttons, the
water's thickness (viscosity: 0 is water, 0.01 is syrup), how murky it is,
how strongly it bends what's behind, and the live numbers.

## How it fits together

`WaterDemoModule::fixedUpdate` runs after `RigidBodyModule` (a declared
dependency), so the crates have already moved this step:

```cpp
world.moveKinematic(paddle, ...);   // the wave paddle
link.pull(world, water);            // bodies in the tank -> water.bodies()
water.step(dt);                     // substeps on the GPU or the CPU
link.push(world, water);            // what the water did -> addVelocity
```

Drawing: `prepass` hands every particle to the surface renderer (radius
0.55 of a cell, foam from speed); `renderTranslucent` calls `drawWater`,
after everything opaque, so the floor and the crates show through.

The tank's water box is taller than its walls (an invisible lid at 2 m),
so a splash can rise over the rim and fall back in. The open front is a
wall for the water and the crates all the same.

## Numbers

docs/OPTIMIZATION.md #46 and docs/HARDWARE_TESTS.md HW-021.
