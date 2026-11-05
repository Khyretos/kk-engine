-- ik_arm.lua: two-bone inverse kinematics, worked out in Lua so you can
-- see the maths. Give it a shoulder, two bone lengths and a target, and
-- the law of cosines says where the elbow goes; a "pole" says which way
-- it bends. The engine's kke::solveTwoBone does the same for skeletons.
-- (docs/cookbook/animation.md)

local SHOULDER = Vec(0, 2.2, 0)
local UPPER, LOWER = 1.2, 1.0         -- bone lengths, metres
local POLE = Vec(0, 3.5, -2)          -- the elbow bends toward this point
local BEADS = 5                       -- spheres drawn along each bone

local function moveTo(id, target, dt)
  physics.setVelocity(id, (target - physics.position(id)) / math.max(dt, 1 / 240))
end

-- The solve: returns the elbow and hand positions.
local function solveTwoBone(s, a, b, target, pole)
  local toTarget = target - s
  local d = math.max(0.001, math.min(toTarget:length(), a + b - 0.001))   -- can't reach further than a + b
  local dir = toTarget:normalized()
  -- Law of cosines: the angle at the shoulder between dir and the upper bone.
  local cosA = (a * a + d * d - b * b) / (2 * a * d)
  local sinA = math.sqrt(math.max(0, 1 - cosA * cosA))
  -- The bend direction: the pole, minus its part along dir.
  local toPole = pole - s
  local bend = toPole - dir * toPole:dot(dir)
  if bend:length() < 0.001 then bend = Vec(0, 1, 0) end
  bend = bend:normalized()
  local elbow = s + dir * (a * cosA) + bend * (a * sinA)
  local hand = s + dir * d
  return elbow, hand
end

physics.sphere { pos = SHOULDER, radius = 0.15, color = Vec(0.9, 0.9, 0.9), static = true }
local beads = {}
for i = 1, BEADS * 2 do
  beads[i] = physics.sphere { pos = SHOULDER + Vec(0, -i * 0.2, 0), radius = 0.08, density = 50, color = Vec(0.95, 0.6, 0.2) }
end
local targetBall = physics.sphere { pos = Vec(1, 1, 1), radius = 0.12, density = 50, color = Vec(0.3, 0.9, 0.4) }

local t = 0
hook.Add("Think", "ik_arm.solve", function(dt)
  t = t + dt
  -- The target circles in front of the shoulder, sometimes out of reach.
  local target = SHOULDER + Vec(math.cos(t) * 1.6, math.sin(t * 1.3) * 0.9 - 0.4, 0.9 + math.sin(t * 0.7) * 0.7)
  moveTo(targetBall, target, dt)
  local elbow, hand = solveTwoBone(SHOULDER, UPPER, LOWER, target, POLE)
  -- Beads along shoulder->elbow, then elbow->hand.
  for i = 1, BEADS do
    moveTo(beads[i], SHOULDER + (elbow - SHOULDER) * (i / BEADS), dt)
    moveTo(beads[BEADS + i], elbow + (hand - elbow) * (i / BEADS), dt)
  end
end)
