-- valley.lua: an open valley with the engine's terrain and a day going
-- by in a minute. The land comes from a seed, a pad is flattened where
-- the player stands, a dirt path is painted towards a hill, and the clock
-- runs fast enough to watch the sun go down. N makes a new valley.
-- (docs/TERRAIN.md, docs/MOODS.md "Time of day")

local seed = 1

local function build()
  -- 240 m across, one height a metre, hills up to 18 m, sunk so the
  -- starter level (40 x 40 m round the middle, floor at y = 0) sits on
  -- a flattened pad.
  terrain.create{ size = 240, resolution = 241, height = 18, seed = seed, pos = Vec(0, -6, 0) }
  terrain.flatten(Vec(0, 0, 0), 40, -0.05)          -- level out to 24 m, just under the floor
  for i = 0, 30 do                                  -- a winding path from the corner to a hill
    local p = Vec(20 + i * 1.6, 0, 20 + i * 2 + math.sin(i * 0.4) * 4)
    terrain.paint(p, 2.5, "dirt")
  end
  terrain.raise(Vec(70, 0, 90), 22, 14)             -- the hill
  terrain.smooth(Vec(70, 0, 90), 26, 0.6)
end

build()
clock.start{ hour = 15, dayLength = 60 }            -- a whole day in a minute

input.define("valley.new", "New valley", "N")
hook.Add("Think", "valley", function()
  if input.pressed("valley.new") then
    seed = seed + 1
    build()
  end
end)
hook.Add("Dusk", "valley", function() print("the sun is down at " .. clock.time()) end)
hook.Add("Dawn", "valley", function() print("morning, " .. clock.time()) end)
