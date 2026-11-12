-- flocking.lua: boids. Thirty balls that flock like birds or fish from
-- three simple rules, each looking only at its neighbours: don't crowd
-- (separation), go the way they go (alignment), stay with them
-- (cohesion). They also keep inside a pen and drift after you.
-- (docs/cookbook/algorithms.md)

local COUNT = 30
local SEE = 2.5             -- how far a boid sees its neighbours, metres
local SPEED = 3
local PEN_MIN, PEN_MAX = Vec(-12, 0, -12), Vec(12, 0, 12)

local boids = {}
for i = 1, COUNT do
  local a = i / COUNT * math.pi * 2
  table.insert(boids, physics.sphere { pos = Vec(math.cos(a) * 4, 0.3, math.sin(a) * 4), radius = 0.2, density = 100,
                                       friction = 0, color = Vec(0.3 + 0.7 * i / COUNT, 0.6, 1 - 0.6 * i / COUNT) })
end

hook.Add("Think", "flocking.update", function(dt)
  -- Read everyone first, then move: every boid reacts to the same moment.
  local pos, vel = {}, {}
  for i, id in ipairs(boids) do
    local p, v = physics.position(id), physics.velocity(id)
    pos[i], vel[i] = Vec(p.x, 0, p.z), Vec(v.x, 0, v.z)
  end
  local me = player.position()

  for i, id in ipairs(boids) do
    local apart, heading, middle, n = Vec(0, 0, 0), Vec(0, 0, 0), Vec(0, 0, 0), 0
    for j = 1, COUNT do
      if j ~= i then
        local offset = pos[i] - pos[j]
        local d = offset:length()
        if d < SEE and d > 0.001 then
          apart = apart + offset / (d * d)       -- closer pushes harder
          heading = heading + vel[j]
          middle = middle + pos[j]
          n = n + 1
        end
      end
    end
    local steer = Vec(0, 0, 0)
    if n > 0 then
      steer = steer + apart * 1.5                              -- separation
      steer = steer + (heading / n - vel[i]) * 0.5             -- alignment
      steer = steer + (middle / n - pos[i]) * 0.4              -- cohesion
    end
    steer = steer + (Vec(me.x, 0, me.z) - pos[i]):normalized() * 0.3   -- drift toward you
    -- The pen: turn back near its edges.
    if pos[i].x < PEN_MIN.x then steer = steer + Vec(3, 0, 0) end
    if pos[i].x > PEN_MAX.x then steer = steer - Vec(3, 0, 0) end
    if pos[i].z < PEN_MIN.z then steer = steer + Vec(0, 0, 3) end
    if pos[i].z > PEN_MAX.z then steer = steer - Vec(0, 0, 3) end

    local v = vel[i] + steer * (dt * 4)
    if v:length() > SPEED then v = v:normalized() * SPEED end
    physics.setVelocity(id, Vec(v.x, physics.velocity(id).y, v.z))
  end
end)
