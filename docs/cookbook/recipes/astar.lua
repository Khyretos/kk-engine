-- astar.lua: a chaser that finds its way to you around walls with A*
-- (A-star), the path-finding most games use. The map is text; the path
-- it plans is shown as dots and re-planned twice a second as you move.
-- (docs/cookbook/algorithms.md)

local MAP = {
  "##############",
  "#............#",
  "#..######....#",
  "#.......#....#",
  "#.......#..###",
  "#..###..#....#",
  "#....#.......#",
  "#..####......#",
  "#............#",
  "#..#......#..#",
  "#..#......#..#",
  "#............#",
  "##############",
}
local ORIGIN = Vec(-7, 0, -1)   -- world position of the map's top-left cell
local SPEED = 3.5

local W, H = #MAP[1], #MAP
local function wallAt(x, z) return MAP[z]:sub(x, x) == "#" end
local function cellOf(p) return math.floor(p.x - ORIGIN.x) + 1, math.floor(p.z - ORIGIN.z) + 1 end
local function centre(x, z) return ORIGIN + Vec(x - 0.5, 0, z - 0.5) end

for z = 1, H do
  for x = 1, W do
    if wallAt(x, z) then
      physics.box { pos = centre(x, z) + Vec(0, 0.5, 0), size = Vec(1, 1, 1), color = Vec(0.5, 0.52, 0.6), static = true }
    end
  end
end

-- A*: explore cells cheapest-first, where cost = steps so far (g) plus a
-- guess of the steps left (h, the Manhattan distance, never too high).
local function findPath(sx, sz, gx, gz)
  local function key(x, z) return z * 1000 + x end
  local open = { { x = sx, z = sz, g = 0, f = math.abs(gx - sx) + math.abs(gz - sz) } }
  local came, best = {}, { [key(sx, sz)] = 0 }
  while #open > 0 do
    -- Take the open cell with the lowest f (a heap is faster for big maps).
    local bi = 1
    for i = 2, #open do if open[i].f < open[bi].f then bi = i end end
    local cur = table.remove(open, bi)
    if cur.x == gx and cur.z == gz then
      local path, k = {}, key(gx, gz)       -- walk back from the goal
      while k do
        table.insert(path, 1, { x = k % 1000, z = k // 1000 })
        k = came[k]
      end
      return path
    end
    for _, d in ipairs({ { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } }) do
      local nx, nz = cur.x + d[1], cur.z + d[2]
      if nx >= 1 and nx <= W and nz >= 1 and nz <= H and not wallAt(nx, nz) then
        local g = cur.g + 1
        local k = key(nx, nz)
        if best[k] == nil or g < best[k] then
          best[k], came[k] = g, key(cur.x, cur.z)
          table.insert(open, { x = nx, z = nz, g = g, f = g + math.abs(gx - nx) + math.abs(gz - nz) })
        end
      end
    end
  end
  return nil   -- no way through
end

local chaser = physics.sphere { pos = centre(2, 2) + Vec(0, 0.4, 0), radius = 0.3, density = 300, color = Vec(0.95, 0.3, 0.3) }
local path, dots = nil, {}

local function showPath()
  for _, id in ipairs(dots) do physics.remove(id) end
  dots = {}
  for _, c in ipairs(path or {}) do
    table.insert(dots, physics.sphere { pos = centre(c.x, c.z) + Vec(0, 0.05, 0), radius = 0.06,
                                        color = Vec(1, 0.8, 0.3), static = true })
  end
end

local function plan()
  local cx, cz = cellOf(physics.position(chaser))
  local px, pz = cellOf(player.position())
  if px < 1 or px > W or pz < 1 or pz > H or wallAt(px, pz) then path = nil
  else path = findPath(cx, cz, px, pz) end
  showPath()
end

timer.Create("astar.plan", 0.5, 0, plan)   -- the first plan in half a second

hook.Add("Think", "astar.chase", function()
  local v = physics.velocity(chaser)
  if not path or #path < 2 then
    physics.setVelocity(chaser, Vec(0, v.y, 0))
    return
  end
  -- Head for the next cell on the path; drop cells as they're reached.
  local at = physics.position(chaser)
  local goal = centre(path[2].x, path[2].z)
  local to = Vec(goal.x - at.x, 0, goal.z - at.z)
  if to:length() < 0.2 then table.remove(path, 1) return end
  local step = to:normalized() * SPEED
  physics.setVelocity(chaser, Vec(step.x, v.y, step.z))
end)
