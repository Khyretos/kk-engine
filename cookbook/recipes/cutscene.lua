-- cutscene.lua: a fly-around of a crate tower that ends on a close-up and
-- holds it. C plays it again, or (while it's on) gives the camera back.
-- (docs/cookbook/cameras.md)

input.define("cutscene", "Cutscene", "C")

local tower = Vec(0, 0, 8)
for i = 0, 5 do
  local shade = 0.5 + i * 0.08
  physics.box { pos = tower + Vec(0, 0.5 + i, 0), size = Vec(1, 1, 1), color = Vec(shade, 0.35, 0.2), static = true }
end
local top = tower + Vec(0, 5.5, 0)

local function flyAround()
  local shots = {}
  for i = 0, 8 do
    local a = i / 8 * math.pi * 1.5      -- three quarters of the way round,
    local height = 6 - i * 0.4           -- coming down as it goes
    shots[#shots + 1] = { pos = tower + Vec(math.cos(a) * 9, height, math.sin(a) * 9), look = tower + Vec(0, 3, 0) }
  end
  -- The close-up: near the top, zoomed in.
  shots[#shots + 1] = { pos = top + Vec(2.5, 0.5, -2.5), look = top, fov = 35 }
  camera.path(shots, { seconds = 8, blend = 1, hold = true })
end

hook.Add("Init", "cutscene.start", flyAround)

hook.Add("Think", "cutscene.keys", function()
  if input.pressed("cutscene") then
    if camera.directing() then camera.release(1) else flyAround() end
  end
end)

hook.Add("CameraDone", "cutscene.done", function()
  kke.log("the fly-around is over: C gives the camera back")
end)
