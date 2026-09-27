-- throw.lua: your own controls. Hold T to charge, let go to throw; the
-- longer you hold, the harder it flies. Players can rebind T in the
-- settings like any other action. (docs/cookbook/input.md)

-- define(id, label, default key): once per action. The label is what
-- the rebinding screen shows.
input.define("throw", "Throw a ball (hold to charge)", "T")

local charging = false
local charge = 0              -- seconds held
local MAX_CHARGE = 1.5

hook.Add("Think", "throw.update", function(dt)
  if input.held("throw") then
    -- Held: build up charge while the key is down.
    charging = true
    charge = math.min(charge + dt, MAX_CHARGE)
  elseif charging then
    -- Not held any more, but it was last frame: it was just released.
    charging = false
    local power = 4 + 16 * (charge / MAX_CHARGE)  -- 4 to 20 m/s
    local from = camera.position() + camera.forward() * 1.5
    physics.sphere {
      pos = from,
      radius = 0.18,
      density = 800,
      velocity = camera.forward() * power + Vec(0, 2, 0),
      color = Vec(1, 1 - charge / MAX_CHARGE, 0.2),  -- yellow to red with charge
    }
    print(string.format("Thrown at %.0f m/s", power))
    charge = 0
  end
end)
