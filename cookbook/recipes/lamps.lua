-- lamps.lua: eight coloured point lights in a ring plus one warm lamp above
-- a pillar, with smooth range falloff and optional shadows. (docs/cookbook/gameplay.md)

input.define("lamps", "Lamps on/off", "L")

-- A white pillar for the lamps to light.
physics.box { pos = Vec(0, 1.5, 6), size = Vec(0.6, 3, 0.6), color = Vec(0.9, 0.9, 0.9), static = true }

local sun, why = light.point { pos = Vec(0, 4.5, 6), color = Vec(1, 0.85, 0.6), intensity = 1.5, range = 9, shadows = true }
if not sun then kke.log("warm lamp: " .. why) end

local ring = {}
local colours = { Vec(1, 0, 0), Vec(1, 0.5, 0), Vec(1, 1, 0), Vec(0, 1, 0), Vec(0, 1, 1), Vec(0, 0, 1), Vec(0.8, 0, 1), Vec(1, 0.2, 0.8) }
for i = 1, 8 do
  local c = colours[i]
  local id, why = light.point { pos = Vec(3, 0.6, 6), color = c, intensity = 2, range = 3 }
  if not id then kke.log("ring lamp " .. i .. ": " .. why) break end
  ring[i] = id
end

local on = true

hook.Add("Think", "lamps.spin", function()
  local t = kke.time()
  for i, id in ipairs(ring) do
    local a = t * 0.5 + i * math.pi / 4
    light.move(id, Vec(math.cos(a) * 3, 0.6, 6 + math.sin(a) * 3))
  end
  if input.pressed("lamps") then
    on = not on
    for _, id in ipairs(ring) do
      light.set(id, { on = on })
    end
  end
end)
