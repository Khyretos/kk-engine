-- cameras.lua: pick a camera with the number keys, from Lua
-- (docs/cookbook/cameras.md). view.* comes from CookbookPlayer.cpp.

-- --8<-- [start:keys]
local cameras = { "first", "third", "orbit", "topdown", "iso", "side", "fixed", "cinematic" }
for i, name in ipairs(cameras) do
  input.define("view." .. name, name .. " camera", tostring(i)) -- keys 1 to 8
end

hook.Add("Think", "cameras.keys", function()
  for _, name in ipairs(cameras) do
    if input.pressed("view." .. name) then
      view.mode(name)
      print("camera: " .. name)
    end
  end
end)
-- --8<-- [end:keys]

-- --8<-- [start:path]
-- P or the d-pad up: fly over the stairs and back, then carry on with
-- third person.
input.define("view.tour", "Camera tour", "P", "dpad_up")
hook.Add("Think", "cameras.tour", function()
  if input.pressed("view.tour") then
    view.path({
      { pos = Vec(-12, 3, 4), target = Vec(-6, 1, -4), time = 0 },
      { pos = Vec(-8, 5, -1), target = Vec(-6, 2, -6), time = 3 },
      { pos = Vec(-1, 4, -3), target = Vec(-6, 1, -6), time = 6 },
    }, false)
    timer.Simple(6.5, function() view.mode("third") end)
  end
end)
-- --8<-- [end:path]

-- --8<-- [start:shake]
-- K or the d-pad left: a bump. Shake adds up, so a few quick presses make
-- a big one.
input.define("view.shake", "Shake the camera", "K", "dpad_left")
hook.Add("Think", "cameras.shake", function()
  if input.pressed("view.shake") then view.shake(0.4) end
end)
-- --8<-- [end:shake]

hook.Add("Init", "cameras.hello", function()
  print("Cameras: 1-8 pick one, Tab or d-pad right cycles, P or d-pad up plays a camera path, K or d-pad left shakes.")
end)
