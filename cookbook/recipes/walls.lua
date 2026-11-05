-- walls.lua: invisible walls and glass. Balls bounce around a pen whose
-- walls you can't see; beside it, a clear glass box and a milky glass
-- ball. H shows the invisible walls (for building a level).
-- (docs/cookbook/physics.md)

input.define("walls.show", "Show the invisible walls", "H")

-- A 4 x 4 m pen with a lid: static boxes that collide but aren't drawn.
local function wall(pos, size)
  physics.box { pos = pos, size = size, static = true, visible = false }
end
wall(Vec(0, 1, -2), Vec(4, 2, 0.2))
wall(Vec(0, 1, 2), Vec(4, 2, 0.2))
wall(Vec(-2, 1, 0), Vec(0.2, 2, 4))
wall(Vec(2, 1, 0), Vec(0.2, 2, 4))
wall(Vec(0, 2.1, 0), Vec(4.2, 0.2, 4.2)) -- the lid: nothing bounces out over the top

-- Glass: see-through, tinted by its colour; cloudy = 0 (clear) .. 1 (milky).
physics.box { pos = Vec(-3.5, 0.75, 0), size = Vec(1.5, 1.5, 1.5), static = true,
              glass = true, color = Vec(0.4, 0.85, 1.0) }
physics.sphere { pos = Vec(3.5, 0.8, 0), radius = 0.8, static = true,
                 glass = true, cloudy = 0.5, color = Vec(1.0, 0.7, 0.4) }

-- Balls that never leave the pen, however hard they're thrown.
balls = {}
for i = 0, 5 do
  balls[#balls + 1] = physics.sphere { pos = Vec(-1.2 + i * 0.5, 0.5 + i * 0.3, 0), radius = 0.2, bounce = 0.9,
                                friction = 0.1, color = Vec(1, 0.9 - i * 0.12, 0.2) }
  physics.setVelocity(balls[#balls], Vec(math.cos(i) * 9, 4, math.sin(i) * 9))
end

hook.Add("Think", "walls.keys", function()
  if input.pressed("walls.show") then physics.showHidden(not physics.showHidden()) end
end)
