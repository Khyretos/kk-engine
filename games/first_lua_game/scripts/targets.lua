-- Break the targets: a whole (small) game in one Lua file.
-- Walkthrough: games/first_lua_game/README.md. Runs in kke_demo.
--
--   T            start a round (or start again)
--   left mouse   shoot (the demo's own "fire" action)
--
-- Targets are FEMFX breakables on iron pedestals, placed in an arc in
-- front of you. Break one: points, a sound, and a little extra time.
-- The score, time and a results screen are an RmlUi document
-- (targets_hud.rml, next to this file).

local ROUND_SECONDS = 45
local TARGETS = 8
local BONUS_SECONDS = 2

-- Each target: what it's made of, its size, and what it's worth.
local KINDS = {
  { material = "glass", sound = "Glass", size = Vec(0.7, 0.7, 0.06), points = 150 },
  { material = "stone", sound = "Stone", size = Vec(0.55, 0.55, 0.55), points = 100 },
  { material = "wood",  sound = "Wood",  size = Vec(0.8, 0.35, 0.35), points = 120 },
  { material = "ice",   sound = "Glass", size = Vec(0.5, 0.5, 0.5), points = 80 },
}

input.define("targets.start", "Start break-the-targets", "T")

local hud = ui and ui.load("targets_hud.rml")
local state = "idle"   -- idle, playing, done
local score, shots, broken = 0, 0, 0
local timeLeft = 0
local best = 0
local targets = {}     -- breakable id -> { kind, pos } (false once broken)
local pedestals = {}   -- breakable ids of the (unbreakable) iron pedestals
local left = 0

local function show(id, text) if hud then ui.text(hud, id, text) end end

local function refresh()
  show("score", score)
  show("time", state == "playing" and string.format("%d", math.ceil(timeLeft)) or "-")
  show("left", state == "playing" and left or "-")
  if hud then ui.class(hud, "time", "low", state == "playing" and timeLeft < 10) end
end

-- Everything this round made goes away (FEMFX pieces included).
local function clear()
  for id in pairs(targets) do breakable.remove(id) end
  for _, id in ipairs(pedestals) do breakable.remove(id) end
  targets, pedestals, left = {}, {}, 0
end

local function finish(title)
  state = "done"
  timer.Remove("targets.clock")
  if score > best then best = score end
  shared.targetsBest = best -- other scripts can read it (scripts share on purpose, through `shared`)
  if hud then
    ui.text(hud, "result-title", title)
    ui.text(hud, "result-score", "Score: " .. score)
    local accuracy = shots > 0 and math.floor(broken / shots * 100 + 0.5) or 0
    ui.text(hud, "result-shots", string.format("%d targets with %d shots (%d%%)", broken, shots, accuracy))
    ui.text(hud, "result-best", "Best: " .. best)
    ui.class(hud, "results", "shown", true)
    show("hint", "Press T to play again.")
  end
  print(string.format("round over: %d points, %d/%d targets, %d shots", score, broken, TARGETS, shots))
  refresh()
end

local function start()
  clear()
  score, shots, broken = 0, 0, 0
  timeLeft = ROUND_SECONDS
  state = "playing"
  -- An arc in front of the player, 7 to 13 m out, on the ground.
  local f = camera.forward()
  local ahead = Vec(f.x, 0, f.z):normalized()
  local side = Vec(-ahead.z, 0, ahead.x)
  local origin = camera.target()
  origin = Vec(origin.x, 0, origin.z)
  for i = 1, TARGETS do
    local kind = KINDS[(i - 1) % #KINDS + 1]
    local across = (i - (TARGETS + 1) / 2) * 1.6
    local out = 7 + ((i * 7) % 5) * 1.5
    local at = origin + ahead * out + side * across
    local pedestal = breakable.box { pos = at + Vec(0, 0.45, 0), size = Vec(0.3, 0.9, 0.3), material = "iron", cells = Vec(1, 3, 1) }
    if pedestal then pedestals[#pedestals + 1] = pedestal end
    local top = at + Vec(0, 0.9 + kind.size.y / 2 + 0.01, 0)
    local id, why = breakable.box {
      pos = top,
      size = kind.size,
      material = kind.material,
    }
    if id then
      targets[id] = { kind = kind, pos = top }
      left = left + 1
    else
      print("couldn't place a target: " .. tostring(why))
    end
  end
  -- Where the targets are, for any other script that wants to know (a
  -- minimap, a bot, a test): scripts share data through `shared`.
  local positions = {}
  for _, t in pairs(targets) do positions[#positions + 1] = t.pos end
  shared.targetPositions = positions
  if hud then
    ui.class(hud, "results", "shown", false)
    show("hint", "Break them all before the time runs out!")
  end
  timer.Create("targets.clock", 0.1, 0, function()
    timeLeft = timeLeft - 0.1
    if timeLeft <= 0 then
      timeLeft = 0
      finish("Time!")
    end
    refresh()
  end)
  refresh()
  print("round started: " .. left .. " targets")
end

hook.Add("Think", "targets.input", function()
  if input.pressed("targets.start") then start() end
  if state == "playing" and input.pressed("fire") then shots = shots + 1 end
end)

-- A script breakable came apart (the engine checks every frame).
hook.Add("Break", "targets.hit", function(id)
  local target = targets[id]
  if not target or state ~= "playing" then return end
  local kind = target.kind
  targets[id] = false -- counted; keep it in the list so clear() removes the pieces
  broken = broken + 1
  left = left - 1
  score = score + kind.points + math.floor(timeLeft) * 5
  timeLeft = timeLeft + BONUS_SECONDS
  if audio then audio.impact(target.pos, kind.sound, 1.0) end
  refresh()
  if left == 0 then finish("All targets down!") end
end)

if hud then ui.onClick(hud, "again", start) end
-- Other scripts (or the console) can start a round: hook.Run("TargetsStart")
hook.Add("TargetsStart", "targets.start", start)
refresh()
print("press T to play break-the-targets")
