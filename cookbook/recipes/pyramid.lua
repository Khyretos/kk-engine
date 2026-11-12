-- pyramid.lua: loops inside loops build a pyramid of crates, and G
-- rebuilds it after you've knocked it down. (docs/cookbook/first-steps.md)

local SIZE = 0.5
local LAYERS = 6
local crates = {}

local function build()
  for _, id in ipairs(crates) do physics.remove(id) end
  crates = {}
  for layer = 0, LAYERS - 1 do
    local across = LAYERS - layer             -- fewer crates on each layer up
    for x = 0, across - 1 do
      for z = 0, across - 1 do
        local offset = (across - 1) * SIZE / 2  -- centre each layer
        table.insert(crates, physics.box {
          pos = Vec(2.5 + x * SIZE - offset, SIZE / 2 + layer * SIZE, -0.5 + z * SIZE - offset),
          size = Vec(SIZE, SIZE, SIZE) * 0.98,  -- a hair apart, so they settle
          density = 120,
          color = Vec(0.55 + layer * 0.07, 0.4, 0.25),
        })
      end
    end
  end
  print("Built " .. #crates .. " crates.")
end

build()
input.define("pyramid.rebuild", "Rebuild the pyramid", "G")
hook.Add("Think", "pyramid.keys", function()
  if input.pressed("pyramid.rebuild") then build() end
end)
