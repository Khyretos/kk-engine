# World and level streaming

ENG-F78 and ENG-F64. A big world is not loaded all at once: the parts near the players are, and they come and go as the players move.
`world.*` in Lua does it, on top of `kke::WorldStreamer` (`kke/WorldStream.h`, pure logic, unit-tested), which decides; the engine loads and unloads.

A **cell** is a box of the world: a square of an open world's grid, or a level volume a player walks into (a cellar, a dungeon, the next arena).
A cell is loaded while an observer (a player, or the camera) is within the **load radius** of its box, stays loaded until every observer is beyond
the **unload radius** (so walking back and forth over the edge does not load and unload it again and again), and is unloaded farthest first.

## An open world as a grid

```lua
world.config{ loadRadius = 150, unloadRadius = 200, loads = 1, resident = 40 }
world.grid{
    size = 100,                                         -- metres across a cell; cell (x, z) covers x*100 .. (x+1)*100
    scene = function(x, z) return chunkName(x, z) end,  -- a scene to load at the cell's corner (nil: none) ...
    load = function(x, z) spawnTrees(x, z) end,         -- ... and/or build the chunk yourself
    unload = function(x, z) forgetTrees(x, z) end,      -- (the scene is unloaded for you; remove what `load` made)
}
```
Cells exist where an observer's load radius reaches and are forgotten once unloaded and out of reach, so the world can be as large as you like:
the streamer only ever holds the cells near the players. Unbounded above and below: a grid cell is a column.

## Levels as volumes

```lua
world.cell("cellar",  { box = {min = Vec(10, -8, 10), max = Vec(30, 0, 30)}, scene = "cellar", radius = 0 })   -- while a player is inside
world.cell("arena_2", { center = Vec(400, 0, 0), size = Vec(120, 60, 120), scene = "arena_2", radius = 60, priority = 2 })
world.cell("hud_level", { center = Vec(0, 0, 0), size = 10, always = true, load = function(id) buildHud() end }) -- a persistent level
```
`scene` is a scene of the game's `scenes/` folder (docs/SCENES.md) loaded at `origin` (default: the box's minimum corner); `load` and `unload` are your functions
(called with the cell's id): use either or both. `radius` is how near an observer must come to load it (0 = inside the volume; default: `world.config`'s),
`priority` loads it before nearer, less important cells and keeps it the longest when the budget is short, `always` loads it whatever the observers do and never
unloads it for room.

## Who is watching

By default the camera. `world.observer(Vec)` names where the player is (call it every frame, or when it changes), `world.observers({Vec, Vec})` several (split screen, online
players: a cell is wanted if any of them is near it), `world.observer(nil)` goes back to the camera.

## Budgets: a trickle, not a hitch

Loading a scene means reading files and making models, so loads are spread out: `loads` (default 1) start per frame and `inFlight` (1) at once, nearest and most important first;
`unloads` (2) per frame; `resident` (default 0 = any) caps the cells that are loaded at once: to load a nearer or more important cell when it is full, the streamer first unloads the
farthest, less important one. A load that fails (a missing scene, an error in your `load` function) is reported, leaves nothing half done (the scene is unloaded again) and is tried
again after `retry` seconds (5).

## Events and questions

```lua
hook.Add("CellLoaded",   "my.id", function(id) end)
hook.Add("CellUnloaded", "my.id", function(id) end)
world.state(id)     -- "unknown" | "unloaded" | "loading" | "loaded" | "failed"
world.loaded(id)    -- true when it is loaded
world.cells()       -- the ids loaded now (grid cells are "x,z")
world.stats()       -- {cells, loaded, loading, failed, loads, unloads, failures}
world.remove(id)    -- unload it now (your unload function runs) and forget it
```
A script's cells go with it when it is reloaded or removed (their scenes are unloaded with the rest of what it made; no `unload` functions run then).
Streamed scenes do not count against `maxScenesPerScript`.

## How it is tested

`tests/test_world_stream.cpp` covers the radii and hysteresis, nearest-first spread-out loads and never asking twice, the grid making and forgetting cells, persistent cells,
the resident cap, priorities, retry after failure, several observers and level volumes. In the engine: a grid of 20 m cells with a marker per cell, an observer walking 200 m: 16
cells unloaded and their markers removed, 16 new ones loaded, the persistent cell untouched, a load function that errored once reported and retried.

## Not done

- Loading is on the main thread, spread over frames. Reading and decoding a scene's models on a worker thread before the frame that builds them (so a large scene costs the main
  thread almost nothing) is not there yet, nor is streaming textures (ENG-F61).
- A scene's physics bodies are made when it loads; there is no "collision first, visuals later" stage.
- The grid is on the ground plane (x, z); a world that streams in three dimensions (a space game) uses `world.cell` boxes.
- Nothing saves which cells a player has changed: that is the game's save data (docs/SAVING.md), keyed by the cell id.
