# Terrain

Rolling hills, mountains and valleys for open levels: a heightmap you
make from a seed or a greyscale picture, then sculpt and paint while the
game runs. It is drawn in chunks that get simpler with distance, and it
collides like any other level geometry, so the player walks on it, balls
roll down it, rays hit it, AI creatures stand on it and `nav.build` finds
paths over it.

## From Lua

```lua
terrain.create{ size = 240, resolution = 257, height = 22, seed = 3, pos = Vec(0, -8, 0) }

terrain.flatten(Vec(0, 0, 0), 10, 0)        -- a level pad at height 0 (for a start, a house)
terrain.raise(Vec(-25, 0, 30), 14, 10)      -- a hill 10 m high, 14 m round
terrain.lower(Vec(40, 0, 0), 6, 3)          -- a dip
terrain.smooth(Vec(40, 0, 0), 8)            -- soften what's there
terrain.paint(Vec(5, 0, 8), 2.5, "dirt")    -- a path: layer by name or number (1-4)

local y = terrain.height(10, 20)            -- ground height at x 10, z 20
local spot = terrain.ground(Vec(10, 0, 20)) -- the same point, put on the ground
local up = terrain.normal(10, 20)           -- which way the ground faces
local steep = terrain.slope(10, 20)         -- degrees: 0 flat, 90 a wall
```

`terrain.create{...}` takes:

| Field | Default | Does |
|---|---|---|
| `size` | 256 | metres across (it is square) |
| `resolution` | 257 | heights per side; the spacing is `size / (resolution - 1)` (257 over 256 m = one height per metre). At most 1025 from scripts |
| `pos` | `Vec(0, 0, 0)` | the middle; its y is the lowest the land goes |
| `height` | 20 | metres from the lowest to the highest point |
| `seed` | 1 | the same seed makes the same land on every machine |
| `feature` | a quarter of the size | metres across the biggest hills |
| `octaves`, `roughness` | 5, 0.5 | layers of finer detail, and how much each adds (0.3 soft, 0.7 rocky) |
| `ridged` | false | mountain ridges instead of rolling hills |
| `image` | none | a greyscale picture next to the scripts or in the game's folder (8- or 16-bit PNG and the rest stb_image reads): black is the lowest point, white `height` metres up |
| `flat` | false | no hills at all: sculpt it yourself |
| `layers` | grass, rock, dirt, snow | `{ {name = "grass", color = Vec(0.2, 0.32, 0.1)}, ... }`, up to four |
| `rock`, `rockSlope` | true, 38 | slopes steeper than `rockSlope` degrees show layer 2 (rock), whatever was painted |

It returns true, or nil and the reason (a picture that isn't there).
Calling it again from the same script replaces the terrain.

- One terrain per game. The script that made it changes it; every script
  can read it (`height`, `normal`, `slope`, `ground`, `info`). Unloading
  that script takes it away, as with everything a script makes.
- Brushes fall off smoothly to nothing at their radius. `flatten` keeps
  the inner 60% exactly level and blends the rest. Its height defaults to
  the point's y.
- Sculpting shows at once. The collision follows a quarter of a second
  after the brush stops (and at least once a second while it goes on),
  so a script can sculpt every frame.
- `terrain.layer(layer, {name, color})` renames or recolours a layer.
  `terrain.info()` gives `size`, `resolution`, `spacing`, `corner`,
  `centre` and `owner`. `terrain.remove()` takes it away.

## Trees, rocks and bushes

`terrain.scatter` puts copies of a model over the terrain by rule: only
on a layer, only where it isn't too steep, only between two heights.

```lua
local pine = models.load("SM_Env_Tree_Pine_01")
local forest, n = terrain.scatter{ model = pine, density = 3, maxSlope = 25, scale = { 0.8, 1.5 },
                                   solid = 0.25, clear = { { pos = Vec(0, 0, 0), radius = 12 } } }
terrain.scatter{ model = models.load("SM_Env_Rock_01"), layer = "rock", density = 6, align = true }
terrain.scatter{ model = models.load("SM_Env_Bush_01"), layer = "dirt", density = 20, scale = 0.5 }
terrain.unscatter(forest)                  -- all of that group gone again
print(terrain.scattered())                 -- how many things in all
```

| Field | Default | Does |
|---|---|---|
| `model` | (needed) | a model from `models.load` |
| `density` | 1 | things per 100 m² where they're allowed (3 = a light forest) |
| `layer`, `minWeight` | anywhere, 0.5 | only where that layer is painted at least this much; thinner towards the edge |
| `maxSlope` | 30 | degrees |
| `minHeight`, `maxHeight` | any | world heights (trees below the snow line) |
| `scale` | `{0.8, 1.2}` | a random size between the two, or one number |
| `align` | false | lean with the ground (rocks, grass) instead of standing straight (trees) |
| `pos`, `radius` | the whole terrain | only inside this circle |
| `clear` | none | circles left empty: `{ {pos = Vec, radius = r}, ... }` (a start, a camp, a road) |
| `solid` | 0 | a trunk this many metres thick that blocks the player and bodies (capsule up the model's height) |
| `seed` | the group's number | the same seed puts everything in the same place on every machine |
| `max` | 5000 | at most this many in the group |

- It returns the group's number and how many it placed. A game has at
  most 50,000 scattered things (`ScriptTerrain::maxScattered`). Each is
  an ordinary model instance, drawn instanced and culled outside the
  view by the model module.
- Sculpting moves what stands there with the ground (solid trunks too).
- Points come from a jittered grid (one per cell, anywhere in it), so
  things never pile up on each other and never line up in rows.

## From C++

`kke::Terrain` (`kke/Terrain.h`) is the heightmap alone: `generate`,
`loadImage`, `raise`, `smooth`, `flatten`, `paint`, `heightAt`,
`normalAt`, `scatter` (the placements by rule), and `buildMesh` for a
chunk's vertices. `kke::TerrainChunks`
decides which chunk is drawn at which detail. `RigidWorld::Shape::HeightField`
collides with exactly the triangles `heightAt` uses (each cell split on
the diagonal from (x, z) to (x+1, z+1), as Jolt does). `kke::ScriptTerrain`
puts the three together for Lua, and `ScriptModule::terrain()` hands it to
a game's C++.

## How it is drawn

- Chunks of 32 x 32 cells (more on terrains over 513 heights a side, so
  there are never more than 16 x 16), each its own mesh, drawn only when
  it's in view (and in the sun's shadow view). Nearby chunks use every
  height. From 48 cells away they use every 2nd, from 96 every 4th, from
  192 every 8th. A curtain (a skirt) hangs from every chunk edge,
  so neighbours at different detail never show a crack.
- A sculpted chunk is rebuilt at once. Chunks that changed detail as the
  camera moved are rebuilt nearest first, eight a frame. A new terrain is
  built whole on its first frame.
- Colour is per height sample: the painted layers blended, rock on steep
  slopes, and a faint speckle so a field of one colour doesn't look like
  plastic. Normals always come from the full-detail heights, so far
  chunks are lit like near ones.

## Not yet

- Textured layers (grass and rock pictures blended per pixel). The layers
  are colours today.
- Holes (caves, tunnels), and several terrains side by side for very
  large worlds (ENG-F64/F78 streaming).
- Saving sculpted terrain with the level, and multiplayer: every machine
  makes the same land from the same seed, but sculpting is local. Sculpt
  from an `sv_` script and every player's copy won't follow yet.
- Grass blades drawn by the GPU (thousands per metre, swaying). Scatter
  places models, which is right for trees, rocks and bushes but not for
  grass. Scattered things don't sway in the wind yet either.
