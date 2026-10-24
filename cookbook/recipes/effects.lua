-- effects.lua: particle effects by name. A campfire burns on a stone, F
-- throws fireworks from it, and a puff written in Lua drifts in the wind.
-- (docs/cookbook/gameplay.md)

input.define("fireworks", "Fireworks", "F")

-- A flat stone for the fire to sit on.
physics.box { pos = Vec(-1.5, 0.15, 3), size = Vec(1.4, 0.3, 1.4), color = Vec(0.45, 0.43, 0.4), static = true }

-- A stream keeps going until fx.stop; it follows fx.move.
local fire, why = fx.start("campfire", Vec(-1.5, 0.3, 3))
if not fire then kke.log(why) end

-- Your own effect: the same fields as the YAML in docs/PARTICLE_EFFECTS.md.
local ok, err = fx.define("lazy_puff", {
  description = "slow grey puffs that drift with the wind",
  layers = { { kind = "smoke", rate = 3, speed = { 0.2, 0.5 }, spread = 20, up = true, colors = { { 0.6, 0.6, 0.62 } },
               radius = { 0.2, 0.3 }, growth = 0.4, opacity = 0.4, life = { 3, 4 } } },
})
if not ok then kke.log(err) end
local puff = fx.start("lazy_puff", Vec(1.5, 0.5, 3))

hook.Add("Think", "effects.play", function()
  local t = kke.time()
  fx.wind(Vec(math.sin(t * 0.3) * 1.5, 0, 0)) -- smoke leans with it
  if puff then fx.move(puff, Vec(1.5, 0.5 + math.sin(t) * 0.3, 3)) end
  if input.pressed("fireworks") then
    fx.play("fireworks", Vec(-1.5, 1, 3), { scale = 1.5 })
  end
end)
