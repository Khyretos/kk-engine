-- round.lua: a whole game round. Knock all the pillars over before the
-- clock runs out; a HUD shows the time and what's left, you win or lose,
-- the best time is saved, and a button starts again.
-- (docs/cookbook/gameplay.md)

local TIME = 45
local SPOTS = { Vec(-3, 0, -1), Vec(0, 0, -2), Vec(3, 0, -1), Vec(-1.5, 0, 1), Vec(1.5, 0, 1) }

local hud = ui and ui.open([[
<rml><head><style>
  body { width: 100%; height: 100%; font-family: Noto Sans; color: #ffffff; pointer-events: none; }
  #panel { position: absolute; left: 20dp; top: 20dp; padding: 8dp 16dp; background-color: #10131ecc;
           border-radius: 8dp; font-size: 20dp; }
  #time { color: #ffd166; }
  #result { position: absolute; top: 40%; width: 100%; text-align: center; font-size: 40dp; }
  #again { position: absolute; top: 55%; left: 50%; margin-left: -80dp; width: 160dp; padding: 8dp;
           text-align: center; background-color: #ef8354; border-radius: 8dp; pointer-events: auto; }
  #again:hover { background-color: #f4a261; }
  .hidden { display: none; }
</style></head>
<body>
  <div id="panel">Time <span id="time"></span> · Standing <span id="left"></span> · Best <span id="best"></span></div>
  <div id="result"></div>
  <div id="again" class="hidden">Play again</div>
</body></rml>
]])

local pillars, timeLeft, playing = {}, 0, false

local function show(id, text) if hud then ui.text(hud, id, text) end end

local function standing()
  local n = 0
  for _, p in ipairs(pillars) do
    -- Still standing = its middle is still up high and near where it was.
    local at = physics.position(p.id)
    if at.y > 0.9 and (Vec(at.x, 0, at.z) - p.home):length() < 0.5 then n = n + 1 end
  end
  return n
end

local function start()
  for _, p in ipairs(pillars) do physics.remove(p.id) end
  pillars = {}
  for _, home in ipairs(SPOTS) do
    local id = physics.box { pos = home + Vec(0, 1, 0), size = Vec(0.4, 2, 0.4), density = 60, color = Vec(0.9, 0.35, 0.35) }
    table.insert(pillars, { id = id, home = home })
  end
  timeLeft, playing = TIME, true
  show("result", "")
  if hud then ui.class(hud, "again", "hidden", true) end
  local best = store.load("round.best")          -- nil until someone wins
  show("best", best and string.format("%.1f s", best) or "-")
end

local function finish(won)
  playing = false
  if won then
    local took = TIME - timeLeft
    show("result", string.format("You did it in %.1f seconds!", took))
    local best = store.load("round.best")
    if not best or took < best then
      store.save("round.best", took)
      show("best", string.format("%.1f s", took))
    end
  else
    show("result", "Out of time!")
  end
  if hud then ui.class(hud, "again", "hidden", false) end
end

hook.Add("Think", "round.tick", function(dt)
  if not playing then return end
  timeLeft = math.max(0, timeLeft - dt)
  local left = standing()
  show("time", string.format("%.0f", math.ceil(timeLeft)))
  show("left", tostring(left))
  if left == 0 then finish(true)
  elseif timeLeft == 0 then finish(false) end
end)

if hud then ui.onClick(hud, "again", start) end
start()
