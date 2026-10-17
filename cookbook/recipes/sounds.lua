-- sounds.lua: one crate of every sound material, dropped in a row. Each
-- hit plays the material's impact sound, louder the faster it hits; the
-- Contact hook prints who hit what. O drops them again.
-- (docs/cookbook/audio.md)

if not audio then
  print("sounds.lua: this game has no AudioModule")
  return
end

local names = {}
for name, id in pairs(audio.materials()) do table.insert(names, { name = name, id = id }) end
table.sort(names, function(a, b) return a.id < b.id end)   -- a stable order

local crates, nameOf = {}, {}
local function drop()
  for _, id in ipairs(crates) do physics.remove(id) end
  crates, nameOf = {}, {}
  for i, m in ipairs(names) do
    local id = physics.box { pos = Vec(-4 + i * 1.1, 2 + i * 0.4, 1), size = Vec(0.5, 0.5, 0.5), material = m.id,
                             color = Vec(0.3 + (i % 3) * 0.25, 0.5, 0.9 - (i % 4) * 0.15) }
    table.insert(crates, id)
    nameOf[id] = m.name
  end
end

-- The engine plays the sounds by itself; this only reports them.
hook.Add("Contact", "sounds.report", function(c)
  local who = nameOf[c.a] or nameOf[c.b]
  if who and c.speed > 1 then print(string.format("%s hit at %.1f m/s", who, c.speed)) end
end)

-- Your own sound, anywhere: audio.impact(position, material, loudness 0..1).
input.define("sounds.again", "Drop the crates again", "O")
input.define("sounds.ping", "Play a metal ping", "P")
hook.Add("Think", "sounds.keys", function()
  if input.pressed("sounds.again") then drop() end
  if input.pressed("sounds.ping") then audio.impact(camera.position() + camera.forward() * 2, "Metal", 0.8) end
end)
drop()
