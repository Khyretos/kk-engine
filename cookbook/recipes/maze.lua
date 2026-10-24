-- maze.lua: a new maze every time you press M. The recursive backtracker:
-- walk from cell to random unvisited cell, knocking down the wall between,
-- and back up when stuck. Every cell ends up reachable, by exactly one
-- path. (docs/cookbook/algorithms.md)

local W, H = 8, 8               -- cells across and deep
local CELL = 1.8                -- metres
local ORIGIN = Vec(-18, 0, 5)   -- the maze's corner in the level
local WALL_H, THICK = 1.4, 0.2

local walls = {}                -- body ids, to clear the old maze
local seed = 7

local function generate()
  math.randomseed(seed)
  -- open[x][z] = { east = bool, south = bool }: the walls knocked down.
  local open, visited = {}, {}
  for x = 1, W do
    open[x], visited[x] = {}, {}
    for z = 1, H do open[x][z] = { east = false, south = false } end
  end

  local stack = { { 1, 1 } }
  visited[1][1] = true
  while #stack > 0 do
    local x, z = stack[#stack][1], stack[#stack][2]
    -- The neighbours not visited yet.
    local choices = {}
    if x > 1 and not visited[x - 1][z] then table.insert(choices, { x - 1, z }) end
    if x < W and not visited[x + 1][z] then table.insert(choices, { x + 1, z }) end
    if z > 1 and not visited[x][z - 1] then table.insert(choices, { x, z - 1 }) end
    if z < H and not visited[x][z + 1] then table.insert(choices, { x, z + 1 }) end
    if #choices == 0 then
      table.remove(stack)              -- dead end: back up
    else
      local n = choices[math.random(#choices)]
      local nx, nz = n[1], n[2]
      -- Knock down the wall between (x, z) and (nx, nz).
      if nx > x then open[x][z].east = true
      elseif nx < x then open[nx][z].east = true
      elseif nz > z then open[x][z].south = true
      else open[x][nz].south = true end
      visited[nx][nz] = true
      table.insert(stack, { nx, nz })
    end
  end
  return open
end

local function wall(cx, cz, sx, sz)
  table.insert(walls, physics.box { pos = ORIGIN + Vec(cx, WALL_H / 2, cz), size = Vec(sx, WALL_H, sz),
                                    color = Vec(0.45, 0.5, 0.65), static = true })
end

local function build()
  for _, id in ipairs(walls) do physics.remove(id) end
  walls = {}
  local open = generate()
  -- The outer walls, with a way in at the east side of the first row.
  wall(W * CELL / 2, 0, W * CELL, THICK)                         -- north
  wall(W * CELL / 2, H * CELL, W * CELL, THICK)                  -- south
  wall(0, H * CELL / 2, THICK, H * CELL)                         -- west
  wall(W * CELL, (H * CELL + CELL) / 2, THICK, (H - 1) * CELL)   -- east, minus the entrance
  -- Inside: each cell's east and south wall, unless knocked down.
  for x = 1, W do
    for z = 1, H do
      if x < W and not open[x][z].east then wall(x * CELL, (z - 0.5) * CELL, THICK, CELL + THICK) end
      if z < H and not open[x][z].south then wall((x - 0.5) * CELL, z * CELL, CELL + THICK, THICK) end
    end
  end
  print(string.format("Maze %d: %d walls", seed, #walls))
end

build()
input.define("maze.new", "New maze", "M")
hook.Add("Think", "maze.keys", function()
  if input.pressed("maze.new") then seed = seed + 1; build() end
end)
