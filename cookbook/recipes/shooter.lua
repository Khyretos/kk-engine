-- shooter.lua: click to shoot where the camera looks. A ray finds what
-- it hits; a hit crate gets knocked back, and a spark marks the spot.
-- "fire" is one of the engine's standard actions (left mouse, right
-- trigger). (docs/cookbook/gameplay.md)

-- A wall of crates to shoot at.
for row = 0, 3 do
  for col = 0, 5 do
    physics.box { pos = Vec(-2.5 + col * 0.62, 0.3 + row * 0.62, -1), size = Vec(0.6, 0.6, 0.6), density = 100,
                  color = Vec(0.5 + row * 0.1, 0.35, 0.2) }
  end
end

local sparks = {}

local function spark(at)
  local id = physics.sphere { pos = at, radius = 0.06, color = Vec(1, 0.9, 0.3), static = true }
  table.insert(sparks, id)
  timer.Simple(0.3, function() physics.remove(id) end)
end

hook.Add("Think", "shooter.fire", function()
  if not input.pressed("fire") then return end
  local from = camera.position()
  local dir = camera.forward()
  -- raycast(from, direction, max distance) -> what it hit, or nil.
  local hit = physics.raycast(from, dir, 60)
  if not hit then return end
  spark(hit.pos)
  -- Push what was hit, at the point it was hit (so it spins too). Only
  -- script bodies can be pushed; pcall skips the rest (walls, the floor).
  pcall(physics.impulse, hit.body, dir * 4, hit.pos)
end)
