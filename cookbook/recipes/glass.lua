-- glass.lua: things that really break (FEMFX). A pane of glass, a
-- wooden plank and a stone slab; press F to throw an iron ball at
-- whatever you look at. The Break hook says when one comes apart.
-- Needs a build with FEMFX (the "everything" preset, docs/BUILDING.md).
-- (docs/cookbook/physics.md)

if not breakable then
  print("glass.lua: no breakable table in this build (configure with the 'everything' preset)")
  return
end

breakable.box { pos = Vec(-2, 1.1, -1), size = Vec(1.2, 1.6, 0.06), material = "glass" }
breakable.box { pos = Vec(0, 1.1, -1), size = Vec(0.2, 1.6, 0.9), material = "wood", pattern = "splinters" }
breakable.box { pos = Vec(2, 0.8, -1), size = Vec(1, 1, 0.3), material = "stone" }

input.define("glass.throw", "Throw an iron ball", "F")
hook.Add("Think", "glass.keys", function()
  if input.pressed("glass.throw") then
    breakable.ball { pos = camera.position() + camera.forward(), radius = 0.12, velocity = camera.forward() * 18 }
  end
end)

hook.Add("Break", "glass.broke", function(id)
  print(string.format("Crash! (%d pieces)", breakable.pieces(id)))
end)
