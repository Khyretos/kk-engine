-- pet.lua: a ball that follows you around like a pet. It steers toward
-- a spot just behind you, slows down as it arrives, and hops when you
-- get far ahead. (docs/cookbook/moving.md)

local pet = physics.sphere { pos = Vec(1.5, 0.5, 6), radius = 0.25, density = 200, color = Vec(0.4, 0.9, 0.5) }
local FOLLOW = 1.8   -- metres it keeps from you
local SPEED = 7      -- top speed, m/s

hook.Add("Think", "pet.follow", function(dt)
  local me = player.position()
  local at = physics.position(pet)
  local toMe = Vec(me.x - at.x, 0, me.z - at.z)    -- flat: steer, don't fly
  local distance = toMe:length()

  local v = physics.velocity(pet)
  if distance > FOLLOW then
    -- "Arrive": full speed far away, slowing to a stop as it gets close.
    local speed = math.min(SPEED, (distance - FOLLOW) * 3)
    local want = toMe:normalized() * speed
    -- Turn the current velocity toward the wanted one, a bit each frame.
    local blend = math.min(1, 8 * dt)
    v = Vec(v.x + (want.x - v.x) * blend, v.y, v.z + (want.z - v.z) * blend)
  end
  -- Left far behind (or you jumped up somewhere): hop.
  if distance > 6 and math.abs(v.y) < 0.1 then v = v + Vec(0, 5, 0) end
  physics.setVelocity(pet, v)

  if at.y < -10 then           -- fell off: back beside you
    physics.remove(pet)
    pet = physics.sphere { pos = me + Vec(1, 1, 0), radius = 0.25, density = 200, color = Vec(0.4, 0.9, 0.5) }
  end
end)
