-- zone_door.lua: a trigger zone. Stand on the green square and the door
-- opens; step off and it shuts two seconds later. Zones are just "is the
-- player inside this box?", checked every frame. (docs/cookbook/gameplay.md)

local ZONE_MIN, ZONE_MAX = Vec(-1, -0.5, 1), Vec(1, 2.5, 3)   -- a box: two corners
local DOOR_AT = Vec(0, 1.25, -2)

physics.box { pos = Vec(0, 0.01, 2), size = Vec(2, 0.02, 2), color = Vec(0.3, 0.9, 0.4), static = true }
-- The wall with a gap for the door.
physics.box { pos = Vec(-2.25, 1.25, -2), size = Vec(2.5, 2.5, 0.3), color = Vec(0.5, 0.5, 0.55), static = true }
physics.box { pos = Vec(2.25, 1.25, -2), size = Vec(2.5, 2.5, 0.3), color = Vec(0.5, 0.5, 0.55), static = true }

local door = nil
local function setDoor(closed)
  if closed and not door then
    door = physics.box { pos = DOOR_AT, size = Vec(2, 2.5, 0.2), color = Vec(0.6, 0.4, 0.25), static = true }
  elseif not closed and door then
    physics.remove(door)
    door = nil
  end
end
setDoor(true)

local function inside(p, lo, hi)
  return p.x >= lo.x and p.x <= hi.x and p.y >= lo.y and p.y <= hi.y and p.z >= lo.z and p.z <= hi.z
end

local wasInside = false
hook.Add("Think", "zone.check", function()
  local now = inside(player.position(), ZONE_MIN, ZONE_MAX)
  if now and not wasInside then            -- just stepped in
    timer.Remove("zone.close")             -- cancel a pending close
    setDoor(false)
    print("Door open")
  elseif wasInside and not now then        -- just stepped out
    timer.Create("zone.close", 2, 1, function() setDoor(true) end)
  end
  wasInside = now
end)
