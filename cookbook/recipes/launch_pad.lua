-- launch_pad.lua: anything that lands on the pad is fired into the air.
-- The Contact hook says when two bodies touch. (docs/cookbook/moving.md)

local pad = physics.box { pos = Vec(-2, 0.05, 1), size = Vec(1.6, 0.1, 1.6), color = Vec(0.2, 0.9, 0.9), static = true }
local LAUNCH = 11   -- m/s upward

hook.Add("Contact", "launch_pad.touch", function(c)
  -- c.a and c.b are the two bodies; one of them must be the pad.
  local other = nil
  if c.a == pad then other = c.b elseif c.b == pad then other = c.a end
  if not other then return end
  -- Scripts can only push bodies scripts made: pcall turns "not yours"
  -- (the level, say) into a quiet false instead of an error.
  local v = physics.velocity(other)
  pcall(physics.setVelocity, other, Vec(v.x, LAUNCH, v.z))
end)

-- Something to launch: a ball dropped on the pad every two seconds.
timer.Create("launch_pad.feed", 2, 0, function()
  local ball = physics.sphere { pos = Vec(-2 + math.random() * 0.6 - 0.3, 3, 1), radius = 0.2, color = Vec(1, 0.5, 0.1) }
  timer.Simple(8, function() physics.remove(ball) end)
end)
