-- caterpillar.lua: procedural animation with no animation at all. The
-- head follows a figure eight; every segment keeps a fixed distance
-- from the one in front ("follow the leader"), so the body winds along
-- behind it, and each segment bobs a little out of step with the last.
-- (docs/cookbook/animation.md)

local SEGMENTS = 12
local GAP = 0.42            -- metres between segment centres
local CENTRE = Vec(0, 0, 1)

-- Moving a physics body to exactly where you want it, every frame:
-- give it the velocity that gets it there in this frame.
local function moveTo(id, target, dt)
  physics.setVelocity(id, (target - physics.position(id)) / math.max(dt, 1 / 240))
end

local body = {}
for i = 1, SEGMENTS do
  local r = i == 1 and 0.22 or 0.2 - i * 0.006   -- thinner toward the tail, never touching
  local green = 0.55 + 0.35 * ((i % 2 == 0) and 1 or 0)
  body[i] = { id = physics.sphere { pos = CENTRE + Vec(-i * GAP, r, 0), radius = r, density = 50, color = Vec(0.35, green, 0.25) },
              at = CENTRE + Vec(-i * GAP, 0, 0), r = r }
end

local t = 0
hook.Add("Think", "caterpillar.move", function(dt)
  t = t + dt
  -- The head: a figure eight (a Lissajous curve), 4 m by 2 m.
  body[1].at = CENTRE + Vec(math.sin(t * 0.5) * 4, 0, math.sin(t) * 2)
  -- Everyone else: pulled to exactly GAP behind the segment in front.
  for i = 2, SEGMENTS do
    local ahead = body[i - 1].at
    local offset = body[i].at - ahead
    if offset:length() > 0.001 then body[i].at = ahead + offset:normalized() * GAP end
  end
  for i, s in ipairs(body) do
    local bob = math.max(0, math.sin(t * 8 - i * 0.7)) * 0.12   -- a wave running down the body
    moveTo(s.id, s.at + Vec(0, s.r + bob, 0), dt)
  end
end)
