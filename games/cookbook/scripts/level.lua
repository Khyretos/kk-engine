-- level.lua: the cookbook's playground. The same blocks as the starter
-- game (games/template/scripts/game.lua), plus pillars to hide behind
-- and a low step under the mannequin's foot (docs/cookbook/animation.md).

local M = audio and audio.materials() or {}

local function block(pos, size, color, material)
  return physics.box { pos = pos, size = size, color = color, material = material or M.Stone or 0, static = true }
end

block(Vec(0, -0.25, 0), Vec(40, 0.5, 40), Vec(0.36, 0.42, 0.36))
local wall = Vec(0.55, 0.52, 0.48)
block(Vec(0, 0.5, -20), Vec(40, 1, 0.5), wall)
block(Vec(0, 0.5, 20), Vec(40, 1, 0.5), wall)
block(Vec(-20, 0.5, 0), Vec(0.5, 1, 40), wall)
block(Vec(20, 0.5, 0), Vec(0.5, 1, 40), wall)

-- Stairs up to a platform.
local accent = Vec(0.85, 0.55, 0.25)
for step = 0, 7 do
  local h = 0.25 * (step + 1)
  block(Vec(-6, h / 2, -2 - step * 0.4), Vec(3, h, 0.4), accent)
end
block(Vec(-6, 1, -6.2), Vec(3, 2, 2.4), wall)

-- A fence to vault and a block to climb.
block(Vec(4, 0.5, -3), Vec(3, 1, 0.2), Vec(0.75, 0.62, 0.35), M.Wood)
block(Vec(4, 0.7, -8), Vec(3, 1.4, 1.5), Vec(0.5, 0.55, 0.62))

-- Pillars: something for the cameras to look past.
for i = 0, 3 do
  block(Vec(10, 1.5, -6 + i * 4), Vec(0.8, 3, 0.8), Vec(0.62, 0.6, 0.72))
end

-- The step the standing mannequin rests one foot on (at x 3, z 1).
block(Vec(3.14, 0.09, 1.0), Vec(0.22, 0.18, 0.5), Vec(0.4, 0.5, 0.7))
