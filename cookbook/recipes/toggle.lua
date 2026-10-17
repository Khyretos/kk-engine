-- toggle.lua: one key, two ways. L switches a lamp on and off (pressed:
-- once per press), and holding the engine's own "sprint" action makes it
-- glow brighter while held. (docs/cookbook/input.md)

input.define("lamp", "Lamp on/off", "L")

local lamp = nil        -- the lamp's body id while it's on
local bright = false

local function setLamp(on, glow)
  if lamp then physics.remove(lamp); lamp = nil end
  if on then
    local c = glow and Vec(1, 1, 0.7) or Vec(0.9, 0.7, 0.3)
    lamp = physics.sphere { pos = Vec(-2, 1.2, 3), radius = glow and 0.4 or 0.3, color = c, static = true }
  end
end

setLamp(true, false)

hook.Add("Think", "toggle.keys", function()
  if input.pressed("lamp") then
    setLamp(lamp == nil, bright)           -- on if it was off, off if on
  end
  -- The standard character actions (move, jump, sprint, ...) are there
  -- too: read them the same way.
  local sprinting = input.held("sprint")
  if lamp and sprinting ~= bright then
    bright = sprinting
    setLamp(true, bright)
  end
end)
