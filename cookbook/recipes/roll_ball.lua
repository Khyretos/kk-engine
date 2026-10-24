-- roll_ball.lua: a ball you steer with the arrow keys, relative to where
-- the camera looks, and hop with Right Ctrl. Movement is forces on a
-- physics body, so it rolls, bumps and falls for real.
-- (docs/cookbook/moving.md)

input.define("ball.forward", "Ball forward", "Up")
input.define("ball.back", "Ball back", "Down")
input.define("ball.left", "Ball left", "Left")
input.define("ball.right", "Ball right", "Right")
input.define("ball.hop", "Ball hop", "Right Ctrl")

local ball = physics.sphere { pos = Vec(0, 1, 2), radius = 0.35, density = 300, bounce = 0.3, color = Vec(0.9, 0.2, 0.4) }
local PUSH = 6           -- how hard the arrows push (m/s per second)

hook.Add("Think", "ball.move", function(dt)
  -- Flatten the camera's forward so "up" never pushes into the ground.
  local f = camera.forward()
  local forward = Vec(f.x, 0, f.z):normalized()
  local right = forward:cross(Vec(0, 1, 0))

  local wish = Vec(0, 0, 0)
  if input.held("ball.forward") then wish = wish + forward end
  if input.held("ball.back") then wish = wish - forward end
  if input.held("ball.right") then wish = wish + right end
  if input.held("ball.left") then wish = wish - right end

  local v = physics.velocity(ball)
  if wish:length() > 0 then
    v = v + wish:normalized() * (PUSH * dt)
  end
  -- Hop only when nearly still vertically (on the ground, more or less).
  if input.pressed("ball.hop") and math.abs(v.y) < 0.2 then
    v = v + Vec(0, 5, 0)
  end
  physics.setVelocity(ball, v)

  -- Fell off the world? Back to the start.
  if physics.position(ball).y < -10 then
    physics.remove(ball)
    ball = physics.sphere { pos = Vec(0, 1, 2), radius = 0.35, density = 300, bounce = 0.3, color = Vec(0.9, 0.2, 0.4) }
  end
end)
