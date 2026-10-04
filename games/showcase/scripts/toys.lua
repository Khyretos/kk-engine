-- toys.lua: a small Garry's Mod-style script for kke_demo (docs/SCRIPTING.md).
-- Edit and save this file while the demo runs: it reloads by itself, and
-- the towers and balls it made are replaced, not duplicated.
--
--   H  build a tower of metal crates in front of you
--   B  throw a rubber ball where you're looking
--   N  clear everything this script made (or hold B a second)
-- On a controller the spawn menu (RB) has towers and balls too; this
-- script is the Lua way of doing the same.

input.define("toys.tower", "Build a crate tower", "H")
input.define("toys.ball", "Throw a ball (hold: clear)", "B")
input.define("toys.clear", "Clear toys", "N")

local M = audio and audio.materials() or {}
local spawned = {}
local hits = 0

local function ahead(distance)
  local f = camera.forward()
  local flat = Vec(f.x, 0, f.z):normalized()
  local p = camera.target() + flat * distance
  -- Land on whatever is there, not inside it.
  local hit = physics.raycast(p + Vec(0, 10, 0), Vec(0, -1, 0), 30)
  if hit then p = hit.pos end
  return p
end

local function tower()
  local base = ahead(3)
  for level = 0, 5 do
    for k = 0, 1 do
      local off = (level % 2 == 0) and Vec(k * 0.52 - 0.26, 0, 0) or Vec(0, 0, k * 0.52 - 0.26)
      spawned[#spawned + 1] = physics.box {
        pos = base + off + Vec(0, 0.26 + level * 0.52, 0),
        size = Vec(0.5, 0.5, 0.5),
        density = 700,
        material = M.Metal or 0,
        color = Vec(0.55, 0.6, 0.68),
      }
    end
  end
  print("tower of " .. 12 .. " crates; " .. physics.count() .. " script bodies now")
end

local function ball()
  local f = camera.forward()
  spawned[#spawned + 1] = physics.sphere {
    pos = camera.target() + Vec(0, 1.2, 0) + f * 0.8,
    radius = 0.2,
    velocity = f * 14 + Vec(0, 2, 0),
    density = 900,
    bounce = 0.7,
    material = M.Rubber or 0,
    color = Vec(0.9, 0.25, 0.2),
  }
end

local function clear()
  for _, id in ipairs(spawned) do physics.remove(id) end
  spawned = {}
  print("cleared")
end

-- How long the ball button has been held: let go within a second and a
-- ball flies; hold it a second and everything clears instead (a
-- controller has no spare button for Clear).
local ballHeld = 0

hook.Add("Think", "toys.keys", function(dt)
  if input.pressed("toys.tower") then tower() end
  if input.held("toys.ball") then
    ballHeld = ballHeld + dt
    if ballHeld >= 1 and ballHeld - dt < 1 then clear() end
  else
    if ballHeld > 0 and ballHeld < 1 then ball() end
    ballHeld = 0
  end
  if input.pressed("toys.clear") then clear() end
end)

-- Every hard hit, counted: contacts carry both bodies, speed, materials, point.
hook.Add("Contact", "toys.count", function(c)
  if c.speed > 6 then
    hits = hits + 1
    if hits % 10 == 0 then print(hits .. " hard hits so far") end
  end
end)

print("toys.lua loaded: H tower, B ball, N clear")
