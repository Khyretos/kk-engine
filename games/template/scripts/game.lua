-- game.lua: the starter game's level and rules. Edit and save this file
-- while the game runs: it reloads by itself, and everything it built is
-- replaced, not duplicated.
--
-- The tutorials (docs/tutorials/) grow this game step by step. The full
-- Lua API is in docs/SCRIPTING.md.

-- Sound materials by name: M.Stone, M.Wood, M.Metal, ... (the engine plays
-- an impact sound when bodies with a material hit something).
local M = audio and audio.materials() or {}

-- A static box: part of the level. `size` is the full size in metres.
local function block(pos, size, color, material)
  return physics.box { pos = pos, size = size, color = color, material = material or M.Stone or 0, static = true }
end

-- The ground and four low walls around it.
block(Vec(0, -0.25, 0), Vec(40, 0.5, 40), Vec(0.36, 0.42, 0.36))
local wall = Vec(0.55, 0.52, 0.48)
block(Vec(0, 0.5, -20), Vec(40, 1, 0.5), wall)
block(Vec(0, 0.5, 20), Vec(40, 1, 0.5), wall)
block(Vec(-20, 0.5, 0), Vec(0.5, 1, 40), wall)
block(Vec(20, 0.5, 0), Vec(0.5, 1, 40), wall)

-- Stairs up to a platform: the character climbs 25 cm steps by itself.
local accent = Vec(0.85, 0.55, 0.25)
for step = 0, 7 do
  local h = 0.25 * (step + 1)
  block(Vec(-6, h / 2, -2 - step * 0.4), Vec(3, h, 0.4), accent)
end
block(Vec(-6, 1, -6.2), Vec(3, 2, 2.4), wall)

-- A fence to vault (press jump in front of it) and a block to climb.
block(Vec(4, 0.5, -3), Vec(3, 1, 0.2), Vec(0.75, 0.62, 0.35), M.Wood)
block(Vec(4, 0.7, -8), Vec(3, 1.4, 1.5), Vec(0.5, 0.55, 0.62))

-- A pile of crates to push around (walk into them).
for i = 0, 5 do
  physics.box {
    pos = Vec(8 + (i % 3) * 0.62, 0.3 + math.floor(i / 3) * 0.62, 2),
    size = Vec(0.6, 0.6, 0.6),
    density = 150,
    material = M.Wood or 0,
    color = Vec(0.62, 0.45, 0.28),
  }
end

hook.Add("Init", "game.hello", function()
  print("Welcome! Click the view, then WASD to move, Space to jump, Shift to sprint.")
end)
