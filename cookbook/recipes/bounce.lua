-- bounce.lua: what the physics settings do, side by side. Front row:
-- balls from no bounce to very bouncy. Back row: the same push on boxes
-- from icy to grippy, sliding different distances. R drops them again.
-- (docs/cookbook/physics.md)

input.define("bounce.again", "Drop them again", "R")
local made = {}

local function drop()
  for _, id in ipairs(made) do physics.remove(id) end
  made = {}
  for i = 0, 5 do
    local bounce = i / 5 * 0.9                               -- 0 .. 0.9
    table.insert(made, physics.sphere { pos = Vec(-3 + i * 1.2, 4, 2), radius = 0.25, bounce = bounce,
                                        color = Vec(0.3, 0.4 + bounce * 0.6, 1) })
    local friction = 0.02 + i / 5 * 0.8                      -- ice .. rubber
    local box = physics.box { pos = Vec(-3 + i * 1.2, 0.26, -1), size = Vec(0.5, 0.5, 0.5), friction = friction,
                              density = 300, color = Vec(1, 0.4 + friction * 0.6, 0.3) }
    physics.setVelocity(box, Vec(0, 0, -6))                  -- the same shove for all
    table.insert(made, box)
  end
end

drop()
hook.Add("Think", "bounce.keys", function()
  if input.pressed("bounce.again") then drop() end
end)
