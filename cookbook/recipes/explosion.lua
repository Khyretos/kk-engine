-- explosion.lua: press B to blow up the heap. Every body nearby is
-- pushed away from the centre, harder the closer it is; there's a sound,
-- and the camera shakes where the game has one that can.
-- (docs/cookbook/gameplay.md)

input.define("boom", "Explosion", "B")
local RADIUS = 5
local FORCE = 12   -- m/s at the centre

-- Things to blow up: a heap of crates and balls, rebuilt every time.
local things = {}
local function heap(at)
  for _, id in ipairs(things) do physics.remove(id) end
  things = {}
  for i = 1, 30 do
    local p = at + Vec(math.random() * 2 - 1, 0.3 + i * 0.1, math.random() * 2 - 1)
    local id
    if i % 3 == 0 then
      id = physics.sphere { pos = p, radius = 0.2, color = Vec(0.3, 0.7, 1) }
    else
      id = physics.box { pos = p, size = Vec(0.4, 0.4, 0.4), density = 150, color = Vec(0.8, 0.6, 0.3) }
    end
    table.insert(things, id)
  end
end

local function explode(centre)
  for _, id in ipairs(things) do
    local offset = physics.position(id) - centre
    local distance = offset:length()
    if distance < RADIUS then
      local falloff = 1 - distance / RADIUS                 -- 1 at the centre, 0 at the edge
      local away = (offset + Vec(0, 0.5, 0)):normalized()    -- a little upward: things lift
      physics.setVelocity(id, physics.velocity(id) + away * (FORCE * falloff))
    end
  end
  if audio then audio.impact(centre, "Stone", 1.0) end
  if view then view.shake(0.8) end   -- only in games with a view table (the cookbook game)
end

local HEAP = Vec(0, 0, 2)
heap(HEAP)
hook.Add("Think", "explosion.keys", function()
  if input.pressed("boom") then
    explode(HEAP + Vec(0, 0.2, 0))
    timer.Simple(3, function() heap(HEAP) end)   -- a new heap to blow up
  end
end)
