-- rain.lua: a timer drops a ball every fifth of a second, somewhere
-- random, and the oldest ones go so there are never more than 60.
-- (docs/cookbook/first-steps.md)

local MAX = 60
local balls = {}  -- a list, oldest first

timer.Create("rain.drop", 0.2, 0, function()   -- 0 repetitions = forever
  local x = math.random() * 16 - 8                -- -8 .. 8
  local z = math.random() * 16 - 8
  local id = physics.sphere {
    pos = Vec(x, 8, z),
    radius = 0.15 + math.random() * 0.15,
    color = Vec(0.3, 0.6 + math.random() * 0.4, 1.0),
    bounce = 0.5,
  }
  table.insert(balls, id)                         -- newest at the end
  if #balls > MAX then
    physics.remove(table.remove(balls, 1))        -- the oldest from the front
  end
end)

-- Stop after 30 seconds: timer.Simple runs once.
timer.Simple(30, function()
  timer.Remove("rain.drop")
  print("The rain stopped.")
end)
