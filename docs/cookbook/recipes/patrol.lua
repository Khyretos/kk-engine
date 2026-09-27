-- patrol.lua: a guard that walks between waypoints, waits at each, and
-- turns back. "Go to the next point" is the heart of most enemy and
-- moving-platform scripts. (docs/cookbook/moving.md)

local WAYPOINTS = { Vec(-3, 0, -1), Vec(3, 0, -1), Vec(3, 0, 3), Vec(-3, 0, 3) }
local SPEED = 2.5   -- m/s
local WAIT = 1.0    -- seconds at each point

local guard = physics.box { pos = WAYPOINTS[1] + Vec(0, 0.5, 0), size = Vec(0.6, 1, 0.6), density = 400,
                            friction = 0, color = Vec(0.8, 0.25, 0.25) }
local target = 2      -- index of the waypoint it's heading for
local waiting = 0

hook.Add("Think", "patrol.walk", function(dt)
  local at = physics.position(guard)
  local goal = WAYPOINTS[target]
  local toGoal = Vec(goal.x - at.x, 0, goal.z - at.z)
  local v = physics.velocity(guard)

  if waiting > 0 then
    waiting = waiting - dt
    physics.setVelocity(guard, Vec(0, v.y, 0))
  elseif toGoal:length() < 0.15 then
    -- Arrived: wait, then head for the next one (wrapping round to 1).
    waiting = WAIT
    target = target % #WAYPOINTS + 1
  else
    local step = toGoal:normalized() * SPEED
    physics.setVelocity(guard, Vec(step.x, v.y, step.z))  -- keep falling if it falls
  end
end)
